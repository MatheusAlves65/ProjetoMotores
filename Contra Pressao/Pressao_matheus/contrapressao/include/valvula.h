/*
  Válvula de escape: driver BTS7960, estimador de posição, autocalibração
  e malha interna de posição.

  Posição em % de FECHAMENTO: 0 % = aberta (batente), 100 % = fechada.
  Fechar aumenta a contrapressão, então o PID trabalha com ganho positivo.

  ESTIMADOR (não há sensor de posição)
    A cada 2 ms:  V = D·Vbat,  i = corrente medida no IS
                  velocidade = (V - R·i) / Ke      (força contraeletromotriz)
                  posição   += velocidade · dt
    Como usa a corrente medida, a carga maior perto dos extremos já entra
    sozinha na conta: a válvula fica pesada -> a corrente sobe -> a
    velocidade estimada cai, sem precisar de um modelo de torque.
    O erro acumulado é zerado toda vez que a válvula encosta num batente.

  AUTOCALIBRAÇÃO (homing, ~4 s, ao ligar e no comando 'h')
    Faz curso completo nos dois sentidos com PWM fixo e, para cada lado:
     - escala do IS (k): pela corrente no batente, que vale D·Vbat/R
     - Ke: ∫(V - R·i)dt ao longo do curso = Ke · 100 %
*/
#pragma once
#include <Arduino.h>
#include <esp_arduino_version.h>
#include <math.h>
#include "config.h"

enum FalhaValvula : uint8_t { FV_OK = 0, FV_IS, FV_HOMING, FV_BLOQUEIO, FV_TEMPO };
enum Batente : int8_t { B_ABERTA = -1, B_NENHUM = 0, B_FECHADA = 1 };

const char *nomeFalhaValvula(FalhaValvula f) {
  switch (f) {
    case FV_OK:       return "ok";
    case FV_IS:       return "IS (falha sinalizada pela BTS7960)";
    case FV_HOMING:   return "autocalibracao";
    case FV_BLOQUEIO: return "valvula travada no meio do curso";
    case FV_TEMPO:    return "movimento longo demais (posicao perdida)";
  }
  return "?";
}

struct Valvula {
  // estado visível
  float pos = 0.0f;          // % fechamento estimado
  float ref = 0.0f;          // % desejado
  int   pwm = 0;             // assinado: + fecha, - abre
  float corrente = 0.0f;     // A (estimada a partir do IS)
  float vbat = VBAT_FIXA;
  Batente batente = B_NENHUM;
  FalhaValvula falha = FV_OK;
  bool  motorBloqueado = false;   // falha grave: motor desligado de vez
  bool  calibrada = false;

  // calibração por lado
  float kIsFechar = K_IS_FECHAR_PADRAO, kIsAbrir = K_IS_ABRIR_PADRAO;
  float keFechar = KE_FECHAR_PADRAO,    keAbrir = KE_ABRIR_PADRAO;
  uint32_t cursoFecharMs = 0,    cursoAbrirMs = 0;

  // internos
  uint32_t tInicioMov = 0;
  float wEst = 0.0f;          // velocidade estimada (%/s, com sinal)
  float wAtrito = 0.0f;       // velocidade "perdida" para o atrito (R·i/Ke), usada na frenagem
  float iFilt = 0.0f;
  float razaoFilt = 0.0f;
  bool  acimaBatente = false;
  uint32_t tAcima = 0;
  uint8_t contIs = 0;

  // acionamento direto (comando 'pwm'): PWM fixo por um tempo, sem a malha de posição
  bool  diretoAtivo = false;
  int   diretoPwm = 0;
  uint32_t diretoFim = 0;
  uint32_t diretoInicio = 0;
} valv;

// ------------------------------------------------------------ PWM (core 2.x e 3.x)
#if ESP_ARDUINO_VERSION_MAJOR < 3
static int canalPwm(int pino) { return (pino == PIN_PWM_FECHAR) ? 0 : 1; }
#endif

static void pwmIniciar(int pino) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(pino, PWM_FREQ, PWM_RES);
#else
  ledcSetup(canalPwm(pino), PWM_FREQ, PWM_RES);
  ledcAttachPin(pino, canalPwm(pino));
#endif
}

static void pwmEscrever(int pino, int valor) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pino, valor);
#else
  ledcWrite(canalPwm(pino), valor);
#endif
}

// + fecha, - abre, 0 freia (com EN alto, os dois lados ficam no GND)
static void aplicarPwm(int p) {
  if (p > 255) p = 255;
  if (p < -255) p = -255;
  if (p > 0)      { pwmEscrever(PIN_PWM_ABRIR, 0);  pwmEscrever(PIN_PWM_FECHAR, p); }
  else if (p < 0) { pwmEscrever(PIN_PWM_FECHAR, 0); pwmEscrever(PIN_PWM_ABRIR, -p); }
  else            { pwmEscrever(PIN_PWM_FECHAR, 0); pwmEscrever(PIN_PWM_ABRIR, 0); }
  valv.pwm = p;
}

static void habilitarPonte(bool on) {
  digitalWrite(PIN_EN_R, on ? HIGH : LOW);
  digitalWrite(PIN_EN_L, on ? HIGH : LOW);
}

// ------------------------------------------------------------ Medições
static uint32_t lerIsMv(int dir) {
  int pino = (dir > 0) ? PIN_IS_FECHAR : PIN_IS_ABRIR;
  uint32_t s = 0;
  for (int k = 0; k < 4; k++) s += analogReadMilliVolts(pino);
  return s / 4;
}

static float lerVbat() {
#if USAR_VBAT_MEDIDA
  float v = analogReadMilliVolts(PIN_VBAT) / 1000.0f * VBAT_DIVISOR;
  valv.vbat += 0.02f * (v - valv.vbat);
  return valv.vbat;
#else
  return valv.vbat;     // valor digitado no boot ou pelo comando 'b'
#endif
}

void iniciarValvula() {
  pinMode(PIN_EN_R, OUTPUT);
  pinMode(PIN_EN_L, OUTPUT);
  habilitarPonte(false);
  pwmIniciar(PIN_PWM_FECHAR);
  pwmIniciar(PIN_PWM_ABRIR);
  aplicarPwm(0);
  analogSetPinAttenuation(PIN_IS_FECHAR, ADC_11db);
  analogSetPinAttenuation(PIN_IS_ABRIR, ADC_11db);
#if USAR_VBAT_MEDIDA
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);
  valv.vbat = analogReadMilliVolts(PIN_VBAT) / 1000.0f * VBAT_DIVISOR;
#endif
}

static void falharValvula(FalhaValvula f) {
  valv.diretoAtivo = false;
  aplicarPwm(0);
  if (f == FV_IS || valv.falha != FV_OK) {
    // falha do chip, ou segunda falha seguida: desliga o motor de vez
    valv.motorBloqueado = true;
    habilitarPonte(false);
  }
  if (valv.falha == FV_OK) valv.falha = f;
  valv.ref = 0.0f;   // estado seguro: válvula aberta
  Serial.printf("# FALHA valvula: %s%s\n", nomeFalhaValvula(f),
                valv.motorBloqueado ? " -> motor desligado" : " -> abrindo a valvula");
}

// ------------------------------------------------------------ Autocalibração
static const int N_BUF = T_HOMING_MS / 2 + 20;
static uint16_t bufT[N_BUF];
static uint16_t bufMv[N_BUF];

// Aciona com PWM fixo por 'ms' gravando o IS. Retorna nº de amostras, -1 se falha do IS
static int acionarRegistrando(int pwmAssinado, uint32_t ms) {
  int dir = (pwmAssinado > 0) ? 1 : -1;
  int n = 0, contFalha = 0;
  aplicarPwm(0);
  delay(5);
  uint32_t t0 = micros(), prox = t0;
  aplicarPwm(pwmAssinado);
  while (true) {
    while ((int32_t)(micros() - prox) < 0) { }
    prox += 2000;
    uint32_t t = (micros() - t0) / 1000;
    uint32_t mv = lerIsMv(dir);
    if (n < N_BUF) { bufT[n] = t; bufMv[n] = mv; n++; }
    if (mv > IS_FALHA_MV) { if (++contFalha >= N_IS_FALHA) { aplicarPwm(0); return -1; } }
    else contFalha = 0;
    if (t >= ms) break;
  }
  aplicarPwm(0);
  return n;
}

// Acha o batente na curva: patamar = média dos últimos 60 ms; fim do curso =
// instante a partir do qual a corrente fica acima de 85 % do patamar.
static bool analisarCurso(int n, int *kBat, float *patamarMv) {
  int nPat = 30;
  if (n < nPat + 20) return false;
  float s = 0;
  for (int k = n - nPat; k < n; k++) s += bufMv[k];
  *patamarMv = s / nPat;
  float limiar = 0.85f * (*patamarMv);
  *kBat = -1;
  for (int k = n - 1; k >= 1; k--) {
    if (bufT[k] < 40) break;
    if (0.5f * (bufMv[k] + bufMv[k - 1]) < limiar) { *kBat = k + 1; break; }
  }
  if (*kBat < 0 || *kBat >= n - nPat) return false;
  return true;
}

// k do IS e Ke a partir de um curso completo
static bool calcularLado(int n, int kBat, float patamarMv, float D, float vbat,
                         float *kIs, float *ke) {
  float iTrav = D * vbat / R_MOTOR;           // corrente de travamento real
  *kIs = patamarMv / (D * iTrav);             // mV = k · D · i
  float soma = 0;
  for (int j = 1; j < kBat; j++) {
    float dt = (bufT[j] - bufT[j - 1]) / 1000.0f;
    float i = bufMv[j] / (*kIs * D);
    soma += (D * vbat - R_MOTOR * i) * dt;    // ∫(V - R·i)dt
  }
  *ke = soma / 100.0f;                        // V por (%/s)
  return (*kIs > 200.0f && *kIs < 5000.0f && *ke > 0.005f && *ke < 0.5f);
}

// Explica por que um curso foi rejeitado (chamado só na falha)
static void diagnosticarCurso(int n, float D, float vbat) {
  int kBat; float pat = 0, kIs = 0, ke = 0;
  if (n < 50) { Serial.printf("#   so %d amostras\n", n); return; }
  bool achou = analisarCurso(n, &kBat, &pat);
  Serial.printf("#   amostras %d | patamar final %.0f mV | IS em t=100/500/1000 ms: %u/%u/%u mV\n",
                n, pat, bufMv[min(50, n - 1)], bufMv[min(250, n - 1)], bufMv[min(500, n - 1)]);
  if (!achou) {
    Serial.printf("#   batente NAO achado (curso mais longo que %u ms? fonte fraca?)\n",
                  (unsigned)(T_HOMING_MS - 60));
    return;
  }
  calcularLado(n, kBat, pat, D, vbat, &kIs, &ke);
  Serial.printf("#   batente em %u ms | k_IS %.0f mV/A (200..5000) | Ke %.4f (0.005..0.5)\n",
                bufT[kBat], kIs, ke);
  // k_IS calculado = k_real * Vbat_real / Vbat_assumida: dá uma estimativa da fonte
  Serial.printf("#   Vbat real estimada ~%.1f V (k_IS nominal ~1330)\n", vbat * kIs / 1330.0f);
}

bool autocalibrarValvula() {
  if (POS_MAX_FECHAMENTO < 100.0f) {
    // a autocalibração fecha até o batente: proibida enquanto houver limite
    Serial.printf("# Autocalibracao BLOQUEADA: ela fecha a valvula ate o batente e o limite de\n"
                  "#   fechamento e %.0f %% (POS_MAX_FECHAMENTO no config.h; 100 = sem limite).\n",
                  POS_MAX_FECHAMENTO);
    return false;
  }
  Serial.println("# Autocalibracao: abre, fecha e abre de novo (~4 s)...");
  valv.falha = FV_OK;
  valv.motorBloqueado = false;
  valv.calibrada = false;
  habilitarPonte(true);
  float vbat = lerVbat();
  float D = PWM_CAL / 255.0f;
  int n, kBat;
  float pat, kIs, ke;

  // 1) garante o ponto de partida no batente aberto
  if (acionarRegistrando(-PWM_CAL, T_HOMING_MS) < 0) { falharValvula(FV_IS); return false; }
  delay(300);

  // 2) fechar: mede o lado RPWM
  n = acionarRegistrando(+PWM_CAL, T_HOMING_MS);
  if (n < 0) { falharValvula(FV_IS); return false; }
  if (!analisarCurso(n, &kBat, &pat) || !calcularLado(n, kBat, pat, D, vbat, &kIs, &ke)) {
    Serial.println("# Curso de fechamento invalido (batente nao encontrado ou valores fora da faixa)");
    diagnosticarCurso(n, D, vbat);
    falharValvula(FV_HOMING);
    return false;
  }
  valv.kIsFechar = kIs; valv.keFechar = ke; valv.cursoFecharMs = bufT[kBat];
  float patFechar = pat;
  delay(300);

  // 3) abrir: mede o lado LPWM e termina no batente aberto
  n = acionarRegistrando(-PWM_CAL, T_HOMING_MS);
  if (n < 0) { falharValvula(FV_IS); return false; }
  if (!analisarCurso(n, &kBat, &pat) || !calcularLado(n, kBat, pat, D, vbat, &kIs, &ke)) {
    Serial.println("# Curso de abertura invalido (batente nao encontrado ou valores fora da faixa)");
    diagnosticarCurso(n, D, vbat);
    falharValvula(FV_HOMING);
    return false;
  }
  valv.kIsAbrir = kIs; valv.keAbrir = ke; valv.cursoAbrirMs = bufT[kBat];
  float patAbrir = pat;

  valv.pos = 0.0f;
  valv.wEst = 0.0f;
  valv.ref = 0.0f;
  valv.batente = B_ABERTA;
  valv.calibrada = true;
  Serial.printf("# Calibrada: curso fechar %u ms, abrir %u ms | Vbat %.2f V\n",
                (unsigned)valv.cursoFecharMs, (unsigned)valv.cursoAbrirMs, vbat);
  Serial.printf("#   k_IS fechar %.0f, abrir %.0f mV/A | Ke fechar %.4f, abrir %.4f V/(%%/s)\n",
                valv.kIsFechar, valv.kIsAbrir, valv.keFechar, valv.keAbrir);
  // pior caso: travado com PWM_MAX e bateria a 14,5 V
  float dMax = PWM_MAX / 255.0f;
  float kMax = fmaxf(valv.kIsFechar, valv.kIsAbrir);
  float piorMv = kMax * dMax * (dMax * 14.5f / R_MOTOR);
  Serial.printf("#   IS no batente: fechar %.0f mV, abrir %.0f mV | pior caso (PWM %d, 14,5 V): %.0f mV\n",
                patFechar, patAbrir, PWM_MAX, piorMv);
  if (piorMv > IS_ALERTA_MV)
    Serial.println("#   AVISO: sinal do IS perto da saturacao. Reduza PWM_MAX ou inverta o divisor do IS (20k em cima, 10k embaixo).");
  return true;
}

// ------------------------------------------------------------ Diagnóstico de hardware ('diag')
// Não depende da calibração: mostra o que chega em cada pino analógico com o
// motor parado e acionado 300 ms em cada sentido. Serve para separar
// "motor não gira" de "sinal IS não chega ao pino" e de mau contato.
static void diagMovimento(const char *nome, int pwm) {
  uint32_t mnF = UINT32_MAX, mxF = 0, sF = 0, mnA = UINT32_MAX, mxA = 0, sA = 0;
  uint32_t n = 0;
  aplicarPwm(0);
  delay(50);
  aplicarPwm(pwm);
  uint32_t t0 = millis();
  while (millis() - t0 < 300) {
    uint32_t f = analogReadMilliVolts(PIN_IS_FECHAR);
    uint32_t a = analogReadMilliVolts(PIN_IS_ABRIR);
    if (f < mnF) mnF = f;
    if (f > mxF) mxF = f;
    if (a < mnA) mnA = a;
    if (a > mxA) mxA = a;
    sF += f; sA += a; n++;
    delay(5);
  }
  aplicarPwm(0);
  Serial.printf("#   %-8s PWM %+4d | IS_FECHAR GPIO%d: %4u/%4u/%4u mV | IS_ABRIR GPIO%d: %4u/%4u/%4u mV  (min/media/max)\n",
                nome, pwm, PIN_IS_FECHAR, (unsigned)mnF, (unsigned)(sF / n), (unsigned)mxF,
                PIN_IS_ABRIR, (unsigned)mnA, (unsigned)(sA / n), (unsigned)mxA);
}

void diagnosticoHardware() {
  Serial.println("# DIAG: le os pinos parado e aciona o motor 300 ms para cada lado.");
  Serial.println("#   Observe se a VALVULA SE MEXE nos dois sentidos.");
  habilitarPonte(true);
  aplicarPwm(0);
  delay(50);
  Serial.printf("#   parado   GPIO%d IS_FECHAR %u mV | GPIO%d IS_ABRIR %u mV | GPIO%d pressao %u mV | GPIO%d vbat %u mV\n",
                PIN_IS_FECHAR, (unsigned)analogReadMilliVolts(PIN_IS_FECHAR),
                PIN_IS_ABRIR,  (unsigned)analogReadMilliVolts(PIN_IS_ABRIR),
                PIN_PRESSAO,   (unsigned)analogReadMilliVolts(PIN_PRESSAO),
                PIN_VBAT,      (unsigned)analogReadMilliVolts(PIN_VBAT));
  // abre primeiro e depois fecha o mesmo tempo: o saldo de movimento é ~zero,
  // então o diag não leva a válvula além de onde ela já estava (limite de fechamento)
  diagMovimento("abrindo", -PWM_CAL);
  delay(300);
  diagMovimento("fechando", +PWM_CAL);
  Serial.println("#   Esperado: parado ~0-150 mV nos IS; movendo, o IS do lado ativo sobe");
  Serial.println("#   (centenas de mV a ~2 V) e o do outro lado fica ~0-150 mV. >3000 mV = saturado/falha.");
  if (valv.motorBloqueado) habilitarPonte(false);
  if (valv.calibrada) {
    // a válvula andou sem o estimador: volta ao batente aberto para referenciar
    valv.pos = 0.0f; valv.wEst = 0.0f; valv.ref = 0.0f; valv.batente = B_NENHUM;
    Serial.println("#   Posicao perdida: voltando ao batente aberto para referenciar.");
  }
}

// ------------------------------------------------------------ Partida sem autocalibração
// Usa os valores *_PADRAO do config.h. A posição real é desconhecida: começa
// com ref = 0 e batente desconhecido, então a malha interna vai ao batente
// aberto (PWM_CAL) e zera a posição lá, como depois de uma calibração.
void iniciarSemCalibracao() {
  valv.falha = FV_OK;
  valv.motorBloqueado = false;
  valv.kIsFechar = K_IS_FECHAR_PADRAO; valv.kIsAbrir = K_IS_ABRIR_PADRAO;
  valv.keFechar  = KE_FECHAR_PADRAO;   valv.keAbrir  = KE_ABRIR_PADRAO;
  valv.pos = 0.0f;
  valv.wEst = 0.0f;
  valv.ref = 0.0f;
  valv.batente = B_NENHUM;          // força a busca do batente aberto
  habilitarPonte(true);
  valv.calibrada = true;            // tem parâmetros para operar
  Serial.println("# Autocalibracao DESLIGADA (AUTOCALIBRAR_NA_PARTIDA = 0): usando valores padrao");
  Serial.printf("#   k_IS fechar %.0f, abrir %.0f mV/A | Ke fechar %.4f, abrir %.4f V/(%%/s)\n",
                valv.kIsFechar, valv.kIsAbrir, valv.keFechar, valv.keAbrir);
  Serial.println("#   Indo ao batente aberto para referenciar a posicao. Use 'h' para calibrar.");
}

// ------------------------------------------------------------ Rampa lenta de PWM ('rampa')
// Ensaios 3.1 e 3.4 de Loh et al. (2013), adaptados para quem não tem sensor
// de posição. O PWM sobe devagar, de 0 até pwmMax, e o IS é gravado a cada 4 ms.
//   - Motor parado (válvula num batente, ou antes de descolar): ω = 0, então
//     i = V/R  -> uma reta. Confere R e a linearidade do modelo do IS (mV = k·D·i).
//   - Válvula no meio do curso: no instante em que ela descola, a força
//     contraeletromotriz aparece e a corrente sai da reta V/R. Esse ponto dá
//     a tensão e a corrente de descolamento -> torque de atrito em cada sentido.
// Bloqueante (no máximo 5 s + impressão). Qualquer tecla interrompe.
// No fim a posição é desconhecida: a malha volta ao batente aberto para referenciar.
static const uint16_t N_RAMPA = 1300;        // 5,2 s a 4 ms
static uint16_t rampT[N_RAMPA], rampMv[N_RAMPA];
static uint8_t  rampPwm[N_RAMPA];

bool rampaPwm(int sentido, float taxa, int pwmMax) {
  if (valv.motorBloqueado || !valv.calibrada || valv.falha != FV_OK) return false;
  int dir = (sentido > 0) ? 1 : -1;
  if (pwmMax > PWM_MAX) pwmMax = PWM_MAX;
  if (pwmMax < 20) pwmMax = 20;
  if (taxa <= 0) taxa = 100;
  if (pwmMax / taxa > 5.0f) taxa = pwmMax / 5.0f;   // no máximo 5 s de rampa

  if (dir > 0 && valv.pos >= POS_MAX_FECHAMENTO - 5.0f) {
    Serial.printf("# Recusado: valvula ja perto do limite de fechamento (%.0f %%). Use 'm 50' antes.\n",
                  POS_MAX_FECHAMENTO);
    return true;                                   // recusa tratada aqui (não é falha)
  }

  valv.diretoAtivo = false;
  aplicarPwm(0);
  // descarta o '\n' que pode ter sobrado do comando (senão interromperia na hora)
  delay(30);
  while (Serial.available()) { int c = Serial.peek(); if (c == '\r' || c == '\n') Serial.read(); else break; }

  float vbat = lerVbat();
  float k = (dir > 0) ? valv.kIsFechar : valv.kIsAbrir;
  float ke = (dir > 0) ? valv.keFechar : valv.keAbrir;
  float posEst = valv.pos;                         // estimador simples só para o limite
  bool noLimite = false;
  Serial.printf("# RAMPA %s: 0 -> %d em %.1f s (%.0f PWM/s). Qualquer tecla interrompe.\n",
                dir > 0 ? "FECHAR" : "ABRIR", pwmMax, pwmMax / taxa, taxa);

  uint16_t n = 0;
  uint8_t contFalha = 0;
  bool interrompida = false;
  uint32_t t0 = micros(), prox = t0;
  while (true) {
    while ((int32_t)(micros() - prox) < 0) { }
    prox += 4000;
    float t = (micros() - t0) / 1e6f;
    int p = (int)(taxa * t);
    if (p > pwmMax) break;
    aplicarPwm(dir * p);
    uint32_t mv = (p > 0) ? lerIsMv(dir) : 0;
    if (mv > IS_FALHA_MV) {
      if (++contFalha >= N_IS_FALHA) { falharValvula(FV_IS); return false; }
    } else contFalha = 0;
    if (n < N_RAMPA) { rampT[n] = (uint16_t)(t * 1000); rampPwm[n] = (uint8_t)p; rampMv[n] = (uint16_t)mv; n++; }
    if (Serial.available()) { interrompida = true; break; }

    // limite de fechamento: mesma estimativa da malha interna, ω = (V - R·i)/Ke
    if (dir > 0 && p > 0) {
      float D = p / 255.0f;
      float w = (D * vbat - R_MOTOR * mv / (k * D)) / ke;   // %/s
      if (w > 0) posEst += w * 0.004f;
      if (posEst >= POS_MAX_FECHAMENTO) { noLimite = true; break; }
    }
  }
  aplicarPwm(0);

  // posição perdida: volta ao batente aberto para referenciar
  valv.pos = 0.0f; valv.wEst = 0.0f; valv.ref = 0.0f; valv.batente = B_NENHUM;

  Serial.printf("# RAMPA %s %s: %u amostras | Vbat %.2f V | k_IS %.0f mV/A | R_MOTOR %.2f ohm\n",
                dir > 0 ? "FECHAR" : "ABRIR",
                interrompida ? "INTERROMPIDA" : (noLimite ? "LIMITE" : "concluida"),
                n, vbat, k, R_MOTOR);
  if (noLimite)
    Serial.printf("#   parada no limite de fechamento (%.0f %% estimado)\n", POS_MAX_FECHAMENTO);
  Serial.println("R,sentido,t_ms,pwm,v_motor_V,is_mV,i_A");
  for (uint16_t j = 0; j < n; j++) {
    float D = rampPwm[j] / 255.0f;
    float i = (D > 0) ? rampMv[j] / (k * D) : 0.0f;
    Serial.printf("R,%d,%u,%u,%.3f,%u,%.4f\n", dir, rampT[j], rampPwm[j], D * vbat, rampMv[j], i);
  }
  Serial.println("# RAMPA fim. A valvula volta ao batente aberto para referenciar a posicao.");
  return true;
}

// ------------------------------------------------------------ Malha interna (a cada TS_INTERNO_US)
void setRefValvula(float r) {
  if (r < 0) r = 0;
  if (r > POS_MAX_FECHAMENTO) r = POS_MAX_FECHAMENTO;   // nunca fecha além do limite
  if (valv.falha == FV_OK) valv.ref = r;
}

float posValvula() { return fminf(fmaxf(valv.pos, 0.0f), 100.0f); }

// ---- Acionamento direto ('pwm <v> <ms>'): + fecha, - abre.
// Roda dentro da malha de 500 Hz, então continuam valendo o estimador de
// posição, a detecção de batente e a proteção do IS. Ao terminar, a válvula
// fica parada onde chegou (ref = posição estimada).
//
// Para o ensaio de velocidades (Loh et al. 2013, seção 3.5): a corrente é
// gravada a cada 2 ms e, no fim, sai uma linha
//   P,pwm,v_motor_V,parada_ms,motivo,i_meio_A,amostras
// parada_ms = instante em que o motor travou (batente), -1 se não travou;
// i_meio_A = corrente média entre 25 % e 60 % do tempo até travar (regime,
// longe da partida e do aumento de carga no fim do curso).
// motivo: B = batente, M = parou no meio, T = tempo acabou, I = interrompido.
static const uint16_t N_BUF_DIRETO = 5000;   // 10 s a 500 Hz
static uint16_t bufIDireto[N_BUF_DIRETO];    // mA
static uint16_t nIDireto = 0;

static void encerrarDireto(const char *motivo, char codigo, int32_t tParadaMs) {
  if (!valv.diretoAtivo) return;
  valv.diretoAtivo = false;
  aplicarPwm(0);
  valv.ref = posValvula();
  Serial.printf("# PWM direto: %s | posicao estimada %.1f %%\n", motivo, posValvula());

  // resumo para o ensaio de velocidades
  uint32_t tRef = (tParadaMs > 0) ? (uint32_t)tParadaMs : (uint32_t)nIDireto * 2;
  uint16_t k0 = (uint16_t)(0.25f * tRef / 2), k1 = (uint16_t)(0.60f * tRef / 2);
  if (k1 > nIDireto) k1 = nIDireto;
  float soma = 0; uint16_t n = 0;
  for (uint16_t k = k0; k < k1; k++) { soma += bufIDireto[k]; n++; }
  float iMeio = n ? soma / n / 1000.0f : NAN;
  float vMotor = abs(valv.diretoPwm) / 255.0f * valv.vbat;
  Serial.println("P,pwm,v_motor_V,parada_ms,motivo,i_meio_A,amostras");
  Serial.printf("P,%d,%.3f,%ld,%c,%.4f,%u\n", valv.diretoPwm, vMotor,
                (long)(tParadaMs > 0 ? tParadaMs : -1), codigo, iMeio, n);
}

// pwm: valor bruto -255..255 (+ fecha, - abre). Retorna o PWM aplicado
// (já limitado a ±PWM_MAX), ou 0 se recusado.
int acionarPwmDireto(int pwm, uint32_t ms) {
  if (valv.motorBloqueado || !valv.calibrada || valv.falha != FV_OK) return 0;
  if (pwm > PWM_MAX)  pwm = PWM_MAX;
  if (pwm < -PWM_MAX) pwm = -PWM_MAX;
  if (pwm == 0 || ms == 0) return 0;
  valv.diretoPwm = pwm;
  valv.diretoInicio = millis();
  valv.diretoFim = valv.diretoInicio + ms;
  nIDireto = 0;
  valv.diretoAtivo = true;
  return pwm;
}

void cancelarPwmDireto() { encerrarDireto("interrompido", 'I', -1); }

void passoValvula(float dt) {
  if (valv.motorBloqueado || !valv.calibrada) { aplicarPwm(0); valv.corrente = 0; return; }

  uint32_t agora = millis();
  int dir = (valv.pwm > 0) - (valv.pwm < 0);
  float vbat = lerVbat();

  // ---- 1) corrente e estimativa de posição (só com o motor acionado)
  bool eventoBatente = false;
  if (dir != 0) {
    float D = abs(valv.pwm) / 255.0f;
    uint32_t mv = lerIsMv(dir);
    if (mv > IS_FALHA_MV) {
      if (++valv.contIs >= N_IS_FALHA) { falharValvula(FV_IS); return; }
    } else valv.contIs = 0;

    float k  = (dir > 0) ? valv.kIsFechar : valv.kIsAbrir;
    float ke = (dir > 0) ? valv.keFechar  : valv.keAbrir;
    float i  = mv / (k * D);
    valv.corrente = i;
    if (valv.diretoAtivo && nIDireto < N_BUF_DIRETO)
      bufIDireto[nIDireto++] = (uint16_t)fminf(fmaxf(i, 0.0f) * 1000.0f, 65535.0f);
    float w = (D * vbat - R_MOTOR * i) / ke;        // %/s
    if (w < 0) w = 0;
    valv.wEst = dir * w;
    valv.iFilt += (dt / (0.01f + dt)) * (i - valv.iFilt);
    valv.wAtrito = R_MOTOR * valv.iFilt / ke;

    // razão corrente / corrente de travamento: ~1 no batente
    float r = i / (D * vbat / R_MOTOR);
    valv.razaoFilt += (dt / (TAU_RAZAO_S + dt)) * (r - valv.razaoFilt);
    if (agora - valv.tInicioMov > T_BLANK_MS) {
      if (valv.razaoFilt > R_BATENTE) {
        if (!valv.acimaBatente) { valv.acimaBatente = true; valv.tAcima = agora; }
        else if (agora - valv.tAcima >= T_BATENTE_MS) eventoBatente = true;
      } else valv.acimaBatente = false;
    }
  } else {
    // freando: o IS não mede, mas o motor ainda desliza um pouco.
    // dω/dt = -(ω + ω_atrito)/τ  -> a frenagem elétrica e o atrito somados
    valv.corrente = 0;
    valv.iFilt = 0;
    if (valv.wEst != 0) {
      float s = (valv.wEst > 0) ? 1.0f : -1.0f;
      float mag = (fabsf(valv.wEst) + valv.wAtrito) * expf(-dt / TAU_FREIO_S) - valv.wAtrito;
      valv.wEst = (mag > 0) ? s * mag : 0.0f;
    }
  }
  valv.pos += valv.wEst * dt;
  if (valv.pos < -2.0f)  valv.pos = -2.0f;
  if (valv.pos > 102.0f) valv.pos = 102.0f;

  float ref = valv.ref;
  bool querAberta  = ref <= POS_FIM;
  bool querFechada = ref >= 100.0f - POS_FIM;

  // ---- 2) chegou a um batente (ou travou)
  if (eventoBatente) {
    aplicarPwm(0);
    valv.wEst = 0;
    if (dir > 0 && valv.pos > 100.0f - ZONA_FIM) {
      Serial.printf("# Batente fechado: estimativa estava em %.1f %% (erro %+.1f %%)\n", valv.pos, valv.pos - 100.0f);
      valv.pos = 100.0f; valv.batente = B_FECHADA;
    } else if (dir < 0 && valv.pos < ZONA_FIM) {
      Serial.printf("# Batente aberto: estimativa estava em %.1f %% (erro %+.1f %%)\n", valv.pos, valv.pos);
      valv.pos = 0.0f; valv.batente = B_ABERTA;
    } else if (valv.diretoAtivo) {
      // no modo direto, PWM baixo demais para vencer o atrito também "trava":
      // não é falha, só encerra o acionamento
      encerrarDireto("motor parou no meio do curso (PWM baixo demais ou travou)", 'M',
                     (int32_t)(valv.tAcima - valv.diretoInicio));
      return;
    } else {
      falharValvula(FV_BLOQUEIO);
    }
    encerrarDireto("chegou ao batente", 'B', (int32_t)(valv.tAcima - valv.diretoInicio));
    return;
  }

  // ---- 2b) acionamento direto: PWM fixo até acabar o tempo
  if (valv.diretoAtivo) {
    if ((int32_t)(agora - valv.diretoFim) >= 0) { encerrarDireto("tempo concluido", 'T', -1); return; }
    if (valv.diretoPwm > 0 && valv.pos >= POS_MAX_FECHAMENTO) {
      encerrarDireto("limite de fechamento (POS_MAX_FECHAMENTO)", 'L', -1);
      return;
    }
    int novoDir = (valv.diretoPwm > 0) - (valv.diretoPwm < 0);
    if (dir != 0 && novoDir != dir) { aplicarPwm(0); return; }   // inversão: freia um ciclo antes
    if (novoDir != dir) {                       // começou a mover
      valv.tInicioMov = agora;
      valv.razaoFilt = 0;
      valv.acimaBatente = false;
      valv.batente = B_NENHUM;
    }
    aplicarPwm(valv.diretoPwm);
    return;
  }

  // ---- 3) decide o novo PWM
  int novo = 0;
  bool buscando = false;
  if (querAberta) {
    if (valv.batente != B_ABERTA) { novo = -PWM_CAL; buscando = true; }
  } else if (querFechada) {
    if (valv.batente != B_FECHADA) { novo = +PWM_CAL; buscando = true; }
  } else {
    float e = ref - valv.pos;
    float banda = (dir != 0) ? BANDA_PARAR : BANDA_PARTIR;
    if (fabsf(e) > banda) {
      int mag = PWM_MIN + (int)(KP_POS * fabsf(e));
      if (mag > PWM_MAX) mag = PWM_MAX;
      novo = (e > 0) ? mag : -mag;
    }
  }

  int novoDir = (novo > 0) - (novo < 0);
  if (novoDir != 0 && dir != 0 && novoDir != dir) novo = 0;   // inversão: freia um ciclo antes
  novoDir = (novo > 0) - (novo < 0);

  if (novoDir != 0 && novoDir != dir) {        // começou a mover
    valv.tInicioMov = agora;
    valv.razaoFilt = 0;
    valv.acimaBatente = false;
    valv.batente = B_NENHUM;
  }

  // ---- 4) proteção por tempo
  if (novoDir != 0) {
    uint32_t tMov = agora - valv.tInicioMov;
    if (buscando && tMov > T_BUSCA_MS) {
      // já empurrou mais que um curso inteiro: está no batente
      aplicarPwm(0);
      valv.wEst = 0;
      if (novoDir > 0) { valv.pos = 100.0f; valv.batente = B_FECHADA; }
      else             { valv.pos = 0.0f;   valv.batente = B_ABERTA; }
      Serial.println("# Aviso: batente assumido por tempo (corrente nao passou do limiar)");
      return;
    }
    if (!buscando && tMov > T_MAX_MOV_MS) { falharValvula(FV_TEMPO); return; }
  }

  aplicarPwm(novo);
}