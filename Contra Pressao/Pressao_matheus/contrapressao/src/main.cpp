/*
  Controle de contrapressão do escapamento - PID em cascata
  ESP32 + BTS7960 + válvula de 2 fios + sensor XDB305

    malha externa (50 Hz):  PID de pressão  -> posição desejada da válvula (%)
    malha interna (500 Hz): controle de posição com estimador (sem sensor)

  Ao ligar: pede a tensão da bateria (multímetro) e NÃO inicia abaixo de
  VBAT_MIN_PARTIDA (11,8 V). Depois: zero do sensor de pressão (motor do carro
  DESLIGADO) e autocalibração da válvula (se AUTOCALIBRAR_NA_PARTIDA = 1; com 0,
  usa os valores padrão do config.h e só vai ao batente aberto). Começa em
  MANUAL com a válvula aberta.

  Comandos no Monitor Serial (115200, terminar com Enter):
    m <pos>     manual: posição da válvula em % de fechamento (0 = aberta)
    a <bar>     automático: liga o PID com esse setpoint de pressão
    s           para: volta ao manual com a válvula aberta
    kp <v>      ganho proporcional   (%/bar)
    ki <v>      ganho integral       (%/(bar·s))
    kd <v>      ganho derivativo     (%·s/bar)
    salvar      grava kp, ki e kd na memória (carregados ao ligar)
    t <ms>      período da telemetria (0 desliga)
    h           refaz a autocalibração (também limpa falhas)
    z           zera o sensor de pressão (motor do carro DESLIGADO)
    b <V>       informa a tensão atual da bateria (abaixo de 11,8 V bloqueia AUTO e 'h')
    pwm <v> <ms>  aciona o motor com PWM fixo (0..255) por um tempo, sem a malha de
                posição (+ fecha, - abre; limitado a PWM_MAX). Ex.: pwm 150 300, pwm -120 1000
                No fim imprime a linha "P," (tempo até travar e corrente de giro).
    rampa <1|-1> [PWM/s] [max]
                rampa lenta de PWM (1 fecha, -1 abre), padrão 60 PWM/s até PWM_MAX.
                Imprime as linhas "R," para o test/atuador.py (R, IS e atrito).
    ensaio [passo] [tempo] [max]
                mapa estático automático: 0 -> max -> 0 % em degraus de 'passo' %,
                'tempo' s por ponto (padrão: ensaio 10 8 90). Imprime linhas "E,".
    degrau <de> <para> [tempo]
                um único degrau de posição: estabiliza em 'de', salta para 'para' e
                calcula K, τ e L (Smith). Padrão tempo = 8 s. Imprime a linha "D,".
    diag        diagnostico de hardware: le os pinos e move o motor 300 ms p/ cada lado
    ?           estado e ajuda

  Telemetria (linhas que começam com "T,"):
    T,tempo_ms,pressao_bar,pos_%,pwm
    (pressao_bar = filtrada τ=0,1 s; pos_% = posição estimada, 0 = aberta;
     pwm = valor aplicado na ponte H, -255..255: + fecha, - abre, 0 = parado/freio)
    Para os ensaios de identificação use "t 20".

  Desarme por sobrepressão (P_DESARME_BAR, em qualquer modo): abre a válvula.
*/
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "pid.h"
#include "sensor_pressao.h"
#include "valvula.h"

enum Modo : uint8_t { MANUAL, AUTO, FALHA };
const char LETRA_MODO[] = {'M', 'A', 'F'};

Modo modo = MANUAL;
float setpoint = 0.0f;        // bar
float pressao = 0.0f;         // bar, filtrada
float pressaoBruta = 0.0f;    // bar, média do bloco de 20 ms (antes do filtro)
float vSensor = 0.0f;         // V na saída do sensor (mesmo se inválida), para diagnóstico
uint8_t contSobrepressao = 0;
float temperatura = TEMP_FIXA;
bool  pressaoOk = false;
uint8_t contPressaoRuim = 0;
uint32_t periodoTelemetria = 100;
bool  bateriaOk = false;      // tensão informada/medida >= VBAT_MIN_PARTIDA

Pid pid(KP_PADRAO, KI_PADRAO, KD_PADRAO, TF_DERIV_S, 0.0f, PID_SAIDA_MAX);
Preferences prefs;

// ------------------------------------------------------------ Temperatura
// TODO: ligar o sensor de temperatura (no corpo do sensor de pressão) e
// devolver a leitura em °C aqui. Enquanto isso, usa um valor fixo.
float lerTemperaturaC() {
  return TEMP_FIXA;
}

// ------------------------------------------------------------ Ganhos salvos
void carregarGanhos() {
  prefs.begin("contrap", false);   // RW: cria o namespace na 1a vez (evita "nvs_open failed")
  pid.kp = prefs.getFloat("kp", KP_PADRAO);
  pid.ki = prefs.getFloat("ki", KI_PADRAO);
  pid.kd = prefs.getFloat("kd", KD_PADRAO);
  prefs.end();
}

void salvarGanhos() {
  prefs.begin("contrap", false);
  prefs.putFloat("kp", pid.kp);
  prefs.putFloat("ki", pid.ki);
  prefs.putFloat("kd", pid.kd);
  prefs.end();
  Serial.println("# Ganhos salvos");
}

// ------------------------------------------------------------ Bateria
// Lê uma linha do Serial esperando o tempo que for preciso; repete o pedido
// a cada 10 s (para quem abrir o monitor depois do boot).
static void lerLinhaBloqueante(char *buf, size_t tam, const char *pedido) {
  size_t n = 0;
  uint32_t tAviso = millis();
  Serial.print(pedido);
  while (true) {
    if (!Serial.available()) {
      if (millis() - tAviso > 10000) { Serial.print(pedido); tAviso = millis(); }
      delay(10);
      continue;
    }
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (n > 0) { buf[n] = 0; return; }
    } else if (n < tam - 1) {
      buf[n++] = c;
    }
  }
}

// Converte "12,6" ou "12.6" em número. Retorna false se não for número.
static bool textoParaFloat(char *s, float *v) {
  for (char *p = s; *p; p++) if (*p == ',') *p = '.';
  return sscanf(s, "%f", v) == 1;
}

// Aplica a tensão da bateria (V). Retorna true se permite operar.
bool definirBateria(float v) {
  if (v < VBAT_MIN_ACEITA || v > VBAT_MAX_ACEITA) {
    Serial.printf("# Valor invalido: %.2f V (use a tensao em volts, ex.: 12.6)\n", v);
    return false;
  }
  valv.vbat = v;
  bateriaOk = (v >= VBAT_MIN_PARTIDA);
  if (bateriaOk) {
    Serial.printf("# Bateria: %.2f V -> ok\n", v);
  } else {
    Serial.printf("# BATERIA BAIXA: %.2f V < %.1f V. Carregue a bateria.\n", v, VBAT_MIN_PARTIDA);
    if (modo == AUTO) { modo = MANUAL; setRefValvula(0); Serial.println("# AUTO desligado, valvula aberta"); }
  }
  return bateriaOk;
}

// No boot: não deixa o sistema iniciar com a bateria abaixo do mínimo.
// A ponte H fica desabilitada enquanto espera (iniciarValvula já desabilitou).
void verificarBateriaNaPartida() {
#if USAR_VBAT_MEDIDA
  while (true) {
    float v = analogReadMilliVolts(PIN_VBAT) / 1000.0f * VBAT_DIVISOR;
    if (definirBateria(v)) return;
    Serial.println("# Sistema NAO iniciado. Nova leitura em 5 s...");
    delay(5000);
  }
#else
  char buf[24];
  char pedido[96];
  snprintf(pedido, sizeof(pedido),
           "# >> Digite a tensao da bateria medida no multimetro (V) e Enter (minimo %.1f V):\n",
           VBAT_MIN_PARTIDA);
  while (true) {
    lerLinhaBloqueante(buf, sizeof(buf), pedido);
    float v;
    if (!textoParaFloat(buf, &v)) { Serial.printf("# Nao entendi '%s'. Exemplo: 12.6\n", buf); continue; }
    if (definirBateria(v)) return;
    Serial.println("# Sistema NAO iniciado.");
  }
#endif
}

// ------------------------------------------------------------ Modos
void entrarManual(float pos) {
  if (valv.falha != FV_OK) { Serial.println("# Em falha: a valvula fica aberta. Use 'h' para recalibrar."); return; }
  modo = MANUAL;
  setRefValvula(pos);
  if (pos > POS_MAX_FECHAMENTO)
    Serial.printf("# Aviso: limitado a %.0f %% (POS_MAX_FECHAMENTO)\n", POS_MAX_FECHAMENTO);
  Serial.printf("# MANUAL: valvula em %.1f %%\n", valv.ref);
}

void entrarAuto(float sp) {
  if (!bateriaOk) { Serial.printf("# Recusado: bateria abaixo de %.1f V (informe com 'b <V>')\n", VBAT_MIN_PARTIDA); return; }
  if (!valv.calibrada || valv.falha != FV_OK) { Serial.println("# Recusado: valvula sem calibracao ou em falha (use 'h')"); return; }
  if (!pressaoOk) { Serial.println("# Recusado: leitura de pressao invalida"); return; }
  if (sp < 0 || sp > P_SP_MAX) { Serial.printf("# Recusado: setpoint deve estar entre 0 e %.1f bar\n", P_SP_MAX); return; }
  setpoint = sp;
  if (modo != AUTO) pid.reset(posValvula(), setpoint, pressao);   // entra sem tranco
  modo = AUTO;
  Serial.printf("# AUTO: setpoint %.3f bar\n", setpoint);
}

void imprimirEstado() {
  Serial.println("# ---------------- estado");
  Serial.printf("# modo %c | setpoint %.3f bar | pressao %.3f bar (%s) | temp %.1f C\n",
                LETRA_MODO[modo], setpoint, pressao, pressaoOk ? "ok" : "INVALIDA", temperatura);
  Serial.printf("# sensor: %.3f V na saida = %.0f mV no GPIO%d | faixa valida %.2f..%.2f V (0 bar = %.3f V)\n",
                vSensor, vSensor / P_DIVISOR * 1000.0f, PIN_PRESSAO, V_SENSOR_MIN, V_SENSOR_MAX, pV0);
  if (vSensor < V_SENSOR_MIN)
    Serial.println("#   sem sinal no pino: confira alimentacao do sensor, GND comum com o ESP32, fio de sinal e divisor");
  Serial.printf("# valvula: ref %.1f %% | pos %.1f %% | %s | falha: %s\n",
                valv.ref, posValvula(), valv.calibrada ? "calibrada" : "SEM CALIBRACAO",
                nomeFalhaValvula(valv.falha));
  Serial.printf("# PID: kp %.3f  ki %.3f  kd %.3f | zero do sensor %.4f V\n",
                pid.kp, pid.ki, pid.kd, pV0);
  Serial.printf("# bateria: %.2f V (%s, minimo %.1f V) | limite de fechamento %.0f %%\n", valv.vbat,
                bateriaOk ? "ok" : "BAIXA", VBAT_MIN_PARTIDA, POS_MAX_FECHAMENTO);
  Serial.println("# comandos: m <pos> | a <bar> | s | kp/ki/kd <v> | salvar | t <ms> | h | z | b <V> | diag |");
  Serial.println("#           ensaio [passo%] [tempo_s] [max%] | degrau <de%> <para%> [tempo_s] |");
  Serial.println("#           pwm <0..255> <ms> | rampa <1|-1> [PWM/s] [max] | ?");
}

// ------------------------------------------------------------ Ensaio do mapa estático
// Escada automática em MANUAL: 0 -> max (fechando aos poucos) e max -> 0
// (abrindo aos poucos). Em cada ponto espera 'tempo' s e faz a média da
// pressão nos últimos segundos. Imprime uma linha "E," por ponto (a tabela que
// seria anotada à mão) e liga a telemetria a 20 ms para o identificacao.py.
// Qualquer comando (menos '?') interrompe; desarme, falha ou sensor inválido também.
struct Ensaio {
  bool ativo = false;
  float passo = 10, tempoS = 8, maxPos = 90, tMediaS = 3;
  float ref = 0;
  int8_t sentido = +1;          // +1 fechando (subida), -1 abrindo (descida)
  uint16_t ponto = 0;
  uint32_t tInicio = 0;
  float somaP = 0, somaP2 = 0, somaPos = 0;
  uint32_t n = 0;
  uint32_t telemAnterior = 100;
} ens;

void terminarEnsaio(const char *motivo) {
  if (!ens.ativo) return;
  ens.ativo = false;
  periodoTelemetria = ens.telemAnterior;
  setRefValvula(0);
  Serial.printf("# ENSAIO terminado: %s. Valvula abrindo; telemetria de volta a %u ms.\n",
                motivo, (unsigned)periodoTelemetria);
}

void iniciarEnsaio(float passo, float tempoS, float maxPos) {
  if (!bateriaOk)                              { Serial.println("# Recusado: bateria baixa ('b <V>')"); return; }
  if (!valv.calibrada || valv.falha != FV_OK)  { Serial.println("# Recusado: valvula sem calibracao ou em falha ('h')"); return; }
  if (!pressaoOk)                              { Serial.println("# Recusado: leitura de pressao invalida"); return; }
  if (modo == AUTO)                            { Serial.println("# Recusado: saia do AUTO antes ('s')"); return; }

  ens.passo  = (passo  >= 1 && passo  <= 50)  ? passo  : 10;
  ens.tempoS = (tempoS >= 2 && tempoS <= 120) ? tempoS : 8;
  ens.maxPos = (maxPos >= ens.passo && maxPos <= 100) ? maxPos : 90;
  if (ens.maxPos > POS_MAX_FECHAMENTO) {
    ens.maxPos = POS_MAX_FECHAMENTO;
    Serial.printf("# Aviso: maximo do ensaio limitado a %.0f %% (POS_MAX_FECHAMENTO)\n", POS_MAX_FECHAMENTO);
  }
  if (ens.maxPos < ens.passo) { Serial.println("# Recusado: passo maior que o limite de fechamento"); return; }
  ens.tMediaS = fminf(3.0f, 0.5f * ens.tempoS);
  ens.ref = 0; ens.sentido = +1; ens.ponto = 0;
  ens.somaP = ens.somaP2 = ens.somaPos = 0; ens.n = 0;
  ens.tInicio = millis();
  ens.telemAnterior = periodoTelemetria;
  periodoTelemetria = 20;
  modo = MANUAL;
  setRefValvula(0);
  ens.ativo = true;

  int nSubida = (int)(ens.maxPos / ens.passo + 0.001f);
  int nPontos = 2 * nSubida + 1;
  Serial.printf("# ENSAIO: 0 -> %.0f -> 0 %%, passo %.1f %%, %.0f s por ponto (media nos ultimos %.1f s)\n",
                nSubida * ens.passo, ens.passo, ens.tempoS, ens.tMediaS);
  Serial.printf("#   %d pontos, ~%.1f min. Qualquer comando interrompe (menos '?'). Desarme em %.2f bar.\n",
                nPontos, nPontos * ens.tempoS / 60.0f, P_DESARME_BAR);
  Serial.println("E,ponto,sentido,ref_%,pos_media_%,p_media_bar,p_desvio_bar,amostras");
}

// Chamado a cada passo da malha externa (50 Hz) enquanto o ensaio está ativo
void passoEnsaio() {
  if (!ens.ativo) return;
  if (!pressaoOk)          { terminarEnsaio("sensor de pressao invalido"); return; }
  if (valv.falha != FV_OK) { terminarEnsaio("falha da valvula"); return; }

  float t = (millis() - ens.tInicio) / 1000.0f;
  if (t >= ens.tempoS - ens.tMediaS) {             // janela final: já estabilizou
    ens.somaP += pressao;
    ens.somaP2 += pressao * pressao;
    ens.somaPos += posValvula();
    ens.n++;
  }
  if (t < ens.tempoS) return;

  // fecha o ponto: imprime a linha da tabela
  float media = ens.n ? ens.somaP / ens.n : NAN;
  float var = ens.n ? ens.somaP2 / ens.n - media * media : 0;
  float desvio = sqrtf(fmaxf(var, 0.0f));
  float posMedia = ens.n ? ens.somaPos / ens.n : NAN;
  Serial.printf("E,%u,%s,%.1f,%.1f,%.4f,%.4f,%u\n", ens.ponto,
                ens.sentido > 0 ? "fechando" : "abrindo", ens.ref, posMedia, media, desvio,
                (unsigned)ens.n);

  // próximo ponto
  if (ens.sentido > 0) {
    if (ens.ref + ens.passo <= ens.maxPos + 0.01f) ens.ref += ens.passo;
    else { ens.sentido = -1; ens.ref -= ens.passo; }
  } else {
    ens.ref -= ens.passo;
  }
  ens.ref = roundf(ens.ref * 10.0f) / 10.0f;
  if (ens.ref < -0.01f) { terminarEnsaio("concluido"); return; }
  if (ens.ref < 0) ens.ref = 0;

  setRefValvula(ens.ref);
  ens.ponto++;
  ens.tInicio = millis();
  ens.somaP = ens.somaP2 = ens.somaPos = 0;
  ens.n = 0;
}

// ------------------------------------------------------------ Degrau único
// 'degrau <de> <para> [tempo_s]': vai para 'de', espera 'tempo' s (estabiliza
// e mede p0), dá UM degrau para 'para', registra 'tempo' s e calcula:
//   K = Δp/Δpos,  t28 e t63 (28,3 % e 63,2 % de Δp),
//   τ = 1,5·(t63 - t28),  L = t63 - τ   (método dos dois pontos de Smith)
// Usa a pressão filtrada (a mesma do PID): o filtro de 0,1 s entra em τ.
// No fim a válvula FICA em 'para'. Qualquer comando (menos '?') interrompe.
const uint16_t N_BUF_DEGRAU = 1600;               // 32 s a 50 Hz
static float    bufPDeg[N_BUF_DEGRAU];
static uint16_t bufTDeg[N_BUF_DEGRAU];             // ms desde o degrau

struct Degrau {
  bool ativo = false;
  uint8_t fase = 0;            // 0 = estabilizando em 'de', 1 = após o degrau
  float de = 0, para = 0, tempoS = 8, tMediaS = 3;
  uint32_t tInicio = 0;
  float somaP = 0, somaP2 = 0; uint32_t n = 0;   // janela final da fase 0
  float p0 = 0, desvio0 = 0;
  uint16_t nBuf = 0;
  uint32_t telemAnterior = 100;
} deg;

void terminarDegrau(const char *motivo) {
  if (!deg.ativo) return;
  deg.ativo = false;
  periodoTelemetria = deg.telemAnterior;
  Serial.printf("# DEGRAU terminado: %s. Valvula em %.1f %%; telemetria de volta a %u ms.\n",
                motivo, valv.ref, (unsigned)periodoTelemetria);
}

void iniciarDegrau(float de, float para, float tempoS) {
  if (!bateriaOk)                              { Serial.println("# Recusado: bateria baixa ('b <V>')"); return; }
  if (!valv.calibrada || valv.falha != FV_OK)  { Serial.println("# Recusado: valvula sem calibracao ou em falha ('h')"); return; }
  if (!pressaoOk)                              { Serial.println("# Recusado: leitura de pressao invalida"); return; }
  if (modo == AUTO)                            { Serial.println("# Recusado: saia do AUTO antes ('s')"); return; }
  if (de < 0 || para < 0 || de > POS_MAX_FECHAMENTO || para > POS_MAX_FECHAMENTO) {
    Serial.printf("# Recusado: posicoes entre 0 e %.0f %% (POS_MAX_FECHAMENTO)\n", POS_MAX_FECHAMENTO);
    return;
  }
  if (fabsf(para - de) < 0.5f)                 { Serial.println("# Recusado: 'de' e 'para' iguais"); return; }

  deg.de = de; deg.para = para;
  deg.tempoS = (tempoS >= 2 && tempoS <= 30) ? tempoS : 8;
  deg.tMediaS = fminf(3.0f, 0.5f * deg.tempoS);
  deg.fase = 0;
  deg.somaP = deg.somaP2 = 0; deg.n = 0; deg.nBuf = 0;
  deg.tInicio = millis();
  deg.telemAnterior = periodoTelemetria;
  periodoTelemetria = 20;
  modo = MANUAL;
  setRefValvula(de);
  deg.ativo = true;
  Serial.printf("# DEGRAU: indo para %.1f %% e estabilizando %.0f s; depois degrau para %.1f %% e registro de %.0f s\n",
                de, deg.tempoS, para, deg.tempoS);
}

static void analisarDegrau() {
  // p final = média dos últimos tMedia s
  uint16_t nFim = 0; float soma = 0;
  uint16_t tFimMs = bufTDeg[deg.nBuf - 1];
  for (uint16_t k = 0; k < deg.nBuf; k++)
    if (bufTDeg[k] + deg.tMediaS * 1000.0f >= tFimMs) { soma += bufPDeg[k]; nFim++; }
  float pf = soma / nFim;
  float dp = pf - deg.p0;
  float du = deg.para - deg.de;
  float K = dp / du;

  Serial.printf("# DEGRAU resultado: %.1f -> %.1f %%\n", deg.de, deg.para);
  Serial.printf("#   p inicial %.4f bar (desvio %.4f) | p final %.4f bar | delta %+.4f bar\n",
                deg.p0, deg.desvio0, pf, dp);

  float t28 = NAN, t63 = NAN;
  if (fabsf(dp) > 3.0f * deg.desvio0 + 1e-4f) {
    for (uint16_t k = 0; k < deg.nBuf; k++) {
      float frac = (bufPDeg[k] - deg.p0) / dp;
      if (isnan(t28) && frac >= 0.283f) t28 = bufTDeg[k] / 1000.0f;
      if (isnan(t63) && frac >= 0.632f) { t63 = bufTDeg[k] / 1000.0f; break; }
    }
  } else {
    Serial.println("#   AVISO: resposta menor que 3x o ruido. Use um degrau maior.");
  }
  float tau = NAN, L = NAN;
  if (!isnan(t28) && !isnan(t63)) {
    tau = 1.5f * (t63 - t28);
    L = fmaxf(t63 - tau, 0.0f);
  }
  Serial.printf("#   K = %.5f bar/%% | t28 %.2f s | t63 %.2f s | tau %.3f s | L %.3f s  (Smith)\n",
                K, t28, t63, tau, L);
  Serial.println("D,de_%,para_%,p0_bar,pf_bar,dp_bar,K_bar_por_%,t28_s,t63_s,tau_s,L_s");
  Serial.printf("D,%.1f,%.1f,%.4f,%.4f,%.4f,%.5f,%.3f,%.3f,%.3f,%.3f\n",
                deg.de, deg.para, deg.p0, pf, dp, K, t28, t63, tau, L);
}

// Chamado a cada passo da malha externa (50 Hz)
void passoDegrau() {
  if (!deg.ativo) return;
  if (!pressaoOk)          { terminarDegrau("sensor de pressao invalido"); return; }
  if (valv.falha != FV_OK) { terminarDegrau("falha da valvula"); return; }

  uint32_t tMs = millis() - deg.tInicio;
  float t = tMs / 1000.0f;

  if (deg.fase == 0) {
    if (t >= deg.tempoS - deg.tMediaS) {            // já estabilizou: mede p0
      deg.somaP += pressao; deg.somaP2 += pressao * pressao; deg.n++;
    }
    if (t < deg.tempoS) return;
    deg.p0 = deg.somaP / deg.n;
    deg.desvio0 = sqrtf(fmaxf(deg.somaP2 / deg.n - deg.p0 * deg.p0, 0.0f));
    setRefValvula(deg.para);                        // o degrau
    deg.fase = 1;
    deg.tInicio = millis();
    deg.nBuf = 0;
    Serial.printf("# DEGRAU: %.1f -> %.1f %% agora (p0 = %.4f bar, t = %lu ms)\n",
                  deg.de, deg.para, deg.p0, (unsigned long)deg.tInicio);
    return;
  }

  if (deg.nBuf < N_BUF_DEGRAU) {
    bufPDeg[deg.nBuf] = pressao;
    bufTDeg[deg.nBuf] = (uint16_t)tMs;
    deg.nBuf++;
  }
  if (t < deg.tempoS) return;
  analisarDegrau();
  terminarDegrau("concluido");
}

// ------------------------------------------------------------ Comandos
void tratarComando(char *linha) {
  char cmd[12] = {0};
  float v = 0, v2 = NAN, v3 = NAN;
  int n = sscanf(linha, "%11s %f %f %f", cmd, &v, &v2, &v3);
  if (n < 1) return;

  if (ens.ativo && strcmp(cmd, "?") != 0) terminarEnsaio("interrompido por comando");
  if (deg.ativo && strcmp(cmd, "?") != 0) terminarDegrau("interrompido por comando");
  if (valv.diretoAtivo && strcmp(cmd, "?") != 0) cancelarPwmDireto();

  if (!strcmp(cmd, "degrau")) {
    if (n < 3) { Serial.println("# Uso: degrau <de %> <para %> [tempo_s]   Ex.: degrau 40 50 8"); return; }
    iniciarDegrau(v, v2, n >= 4 ? v3 : 8);
    return;
  }

  if (!strcmp(cmd, "rampa")) {
    if (n < 2 || (v != 1 && v != -1)) {
      Serial.println("# Uso: rampa <1|-1> [PWM/s] [pwm_max]   (1 fecha, -1 abre). Ex.: rampa 1 60 200");
      return;
    }
    if (!bateriaOk)   { Serial.println("# Recusado: bateria baixa ('b <V>')"); return; }
    if (modo == AUTO) { Serial.println("# Recusado: saia do AUTO antes ('s')"); return; }
    if (!rampaPwm((int)v, n >= 3 ? v2 : 60, n >= 4 ? (int)v3 : PWM_MAX))
      Serial.println("# Recusado: valvula sem calibracao/em falha ('h')");
    return;
  }

  if (!strcmp(cmd, "pwm")) {
    if (n < 3)                  { Serial.println("# Uso: pwm <0..255> <ms>   (+ fecha, - abre). Ex.: pwm 150 300, pwm -120 500"); return; }
    if (!bateriaOk)             { Serial.println("# Recusado: bateria baixa ('b <V>')"); return; }
    if (modo == AUTO)           { Serial.println("# Recusado: saia do AUTO antes ('s')"); return; }
    if (v < -255 || v > 255)    { Serial.println("# Recusado: PWM deve estar entre -255 e 255"); return; }
    if (v2 < 1 || v2 > 10000)   { Serial.println("# Recusado: tempo deve estar entre 1 e 10000 ms"); return; }
    int pedido = (int)lroundf(v);
    if (pedido > 0 && valv.pos >= POS_MAX_FECHAMENTO - 1.0f) {
      Serial.printf("# Recusado: valvula ja no limite de fechamento (%.0f %%)\n", POS_MAX_FECHAMENTO);
      return;
    }
    int aplicado = acionarPwmDireto(pedido, (uint32_t)v2);
    if (aplicado == 0) {
      Serial.println("# Recusado: PWM 0, ou valvula sem calibracao/em falha ('h')");
      return;
    }
    if (abs(aplicado) < abs(pedido))
      Serial.printf("# Aviso: limitado a %d (PWM_MAX no config.h, evita saturar o IS)\n", abs(aplicado));
    Serial.printf("# PWM direto: %+d/255 (%s) por %u ms\n", aplicado,
                  aplicado > 0 ? "fechando" : "abrindo", (unsigned)v2);
    return;
  }

  if (!strcmp(cmd, "ensaio")) {
    iniciarEnsaio(n >= 2 ? v : 10, n >= 3 ? v2 : 8, n >= 4 ? v3 : 90);
    return;
  }

  if      (!strcmp(cmd, "m") && n == 2) entrarManual(v);
  else if (!strcmp(cmd, "a") && n == 2) entrarAuto(v);
  else if (!strcmp(cmd, "s"))           entrarManual(0);
  else if (!strcmp(cmd, "kp") && n == 2) { pid.kp = v; Serial.printf("# kp = %.3f\n", v); }
  else if (!strcmp(cmd, "ki") && n == 2) { pid.ki = v; Serial.printf("# ki = %.3f\n", v); }
  else if (!strcmp(cmd, "kd") && n == 2) { pid.kd = v; Serial.printf("# kd = %.3f\n", v); }
  else if (!strcmp(cmd, "salvar"))      salvarGanhos();
  else if (!strcmp(cmd, "t") && n == 2) { periodoTelemetria = (uint32_t)v; Serial.printf("# telemetria a cada %u ms\n", (unsigned)periodoTelemetria); }
  else if (!strcmp(cmd, "b") && n == 2) definirBateria(v);
  else if (!strcmp(cmd, "h")) {
    if (!bateriaOk) { Serial.printf("# Recusado: bateria abaixo de %.1f V (informe com 'b <V>')\n", VBAT_MIN_PARTIDA); return; }
    modo = MANUAL;
    if (autocalibrarValvula()) entrarManual(0);
  }
  else if (!strcmp(cmd, "z")) {
    if (modo == AUTO) { Serial.println("# Recusado: saia do AUTO antes de zerar"); return; }
    float vm;
    if (zerarSensorPressao(temperatura, &vm)) Serial.printf("# Zero do sensor: %.4f V\n", vm);
    else Serial.printf("# Zero recusado: %.4f V esta longe do zero da calibracao (motor ligado?)\n", vm);
  }
  else if (!strcmp(cmd, "diag")) {
    if (modo == AUTO) { Serial.println("# Recusado: saia do AUTO antes ('s')"); return; }
    diagnosticoHardware();
  }
  else if (!strcmp(cmd, "?"))           imprimirEstado();
  else Serial.printf("# Comando desconhecido: %s (use ?)\n", linha);
}

void lerSerial() {
  static char buf[48];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (len > 0) { buf[len] = 0; tratarComando(buf); len = 0; }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

// ------------------------------------------------------------ Malha externa
void passoExterno(float dt) {
  temperatura = lerTemperaturaC();
  float v = tensaoMediaPressao();                        // média das amostras de 500 Hz
  vSensor = v;

  if (tensaoPressaoValida(v)) {
    contPressaoRuim = 0;
    float p = tensaoParaBar(v, temperatura);
    pressaoBruta = p;
    if (!pressaoOk) pressao = p;                         // primeira leitura boa
    pressao += (dt / (TAU_FILTRO_P + dt)) * (p - pressao);
    pressaoOk = true;
  } else if (++contPressaoRuim >= 5) {                   // 100 ms seguidos
    if (pressaoOk) Serial.printf("# FALHA sensor de pressao: %.2f V\n", v);
    pressaoOk = false;
  }

  // Desarme por sobrepressão (qualquer modo): abre a válvula e sai do AUTO.
  // Enquanto a pressão ficar acima do limite, qualquer comando de fechar é anulado.
  static bool avisouDesarme = false;
  if (pressaoOk && pressaoBruta > P_DESARME_BAR) {
    if (contSobrepressao < N_DESARME) contSobrepressao++;
    if (contSobrepressao >= N_DESARME) {
      if (modo == AUTO) modo = MANUAL;
      terminarEnsaio("DESARME por sobrepressao");
      terminarDegrau("DESARME por sobrepressao");
      cancelarPwmDireto();
      setRefValvula(0);
      if (!avisouDesarme) {
        Serial.printf("# DESARME: pressao %.3f bar > %.2f bar -> valvula aberta, modo %c\n",
                      pressaoBruta, P_DESARME_BAR, LETRA_MODO[modo]);
        avisouDesarme = true;
      }
      return;
    }
  } else {
    contSobrepressao = 0;
    avisouDesarme = false;
  }

  if (valv.falha != FV_OK && modo != FALHA) {
    modo = FALHA;
    Serial.println("# Modo FALHA: PID desligado, valvula aberta. Use 'h' para recalibrar.");
  }

  if (modo == AUTO) {
    if (!pressaoOk) { entrarManual(0); Serial.println("# PID desligado por falha no sensor"); return; }
    setRefValvula(pid.passo(setpoint, pressao, dt));
  }

  passoEnsaio();
  passoDegrau();
}

void imprimirTelemetria() {
  Serial.printf("T,%lu,%.3f,%.1f,%d\n", (unsigned long)millis(), pressao, posValvula(), valv.pwm);
}

// ------------------------------------------------------------ setup / loop
void setup() {
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  delay(500);
  Serial.println("\n# ===== Controle de contrapressao =====");

  carregarGanhos();
  iniciarValvula();            // ponte H desabilitada até a calibração
  iniciarSensorPressao();
  verificarBateriaNaPartida(); // bloqueia aqui se a bateria estiver abaixo do mínimo

  temperatura = lerTemperaturaC();
  float vm;
  if (zerarSensorPressao(temperatura, &vm)) Serial.printf("# Zero do sensor de pressao: %.4f V\n", vm);
  else if (vm < V_SENSOR_MIN)
    Serial.printf("# Zero NAO feito: %.3f V = SEM SINAL do sensor (esperado ~1,0 V a 0 bar). "
                  "Confira alimentacao, GND comum e o fio do GPIO%d\n", vm, PIN_PRESSAO);
  else Serial.printf("# Zero NAO feito (%.4f V): usando o da calibracao. Motor ligado?\n", vm);

#if AUTOCALIBRAR_NA_PARTIDA
  if (POS_MAX_FECHAMENTO < 100.0f) iniciarSemCalibracao();   // calibração bloqueada pelo limite
  else autocalibrarValvula();
#else
  iniciarSemCalibracao();      // valores padrão do config.h; 'h' calibra quando quiser
#endif
  entrarManual(0);
  passoExterno(TS_EXTERNO_MS / 1000.0f);   // primeira leitura de pressão
  imprimirEstado();
  Serial.println("T,tempo_ms,pressao_bar,pos_%,pwm");
}

void loop() {
  static uint32_t tInterno = micros();
  static uint32_t tExterno = millis();
  static uint32_t tTelem = millis();

  uint32_t us = micros();
  if ((uint32_t)(us - tInterno) >= TS_INTERNO_US) {
    float dt = (us - tInterno) / 1e6f;
    tInterno = us;
    passoValvula(dt);
    amostrarPressao();          // 500 Hz, média de bloco na malha externa
  }

  uint32_t ms = millis();
  if (ms - tExterno >= TS_EXTERNO_MS) {
    float dt = (ms - tExterno) / 1000.0f;
    tExterno = ms;
    passoExterno(dt);
  }

  if (periodoTelemetria > 0 && ms - tTelem >= periodoTelemetria) {
    tTelem = ms;
    imprimirTelemetria();
  }

  lerSerial();
}