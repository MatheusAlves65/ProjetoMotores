/*
  Configuração do controle de contrapressão (ESP32 + BTS7960 + válvula de 2 fios)
  Tudo o que se ajusta à mão fica aqui.
*/
#pragma once
#include <stdint.h>

// ============================================================ Pinos
// Montagem conferida em 12/11/2026. Cada PWM tem de estar com o IS do MESMO chip.
#define PIN_PWM_FECHAR   25    // -> LPWM do BTS7960 (lado L fecha nesta polaridade do motor)
#define PIN_PWM_ABRIR    26    // -> RPWM (lado R abre)
#define PIN_EN_R         27    // -> R_EN
#define PIN_EN_L         14    // -> L_EN
#define PIN_IS_FECHAR    35    // <- L_IS (corrente do lado L = fechar)
#define PIN_IS_ABRIR     34    // <- R_IS (corrente do lado R = abrir)
// Montagem (12/11/2026): IS ligados DIRETO no GPIO; VCC do módulo em 3,3 V.
// O VCC alimenta só o buffer lógico do módulo; o IS vem do BTS7960 (alimentado
// pelos 12 V). Na operação normal fica < 3,3 V graças ao resistor IS->GND do
// módulo. PENDENTE: medir esse resistor e conferir o pior caso (corrente de IS
// em falha, datasheet). Recomendado 1k-4,7k em série no IS (não muda a leitura).
// A escala (mV/A) é medida na autocalibração (k_IS), não precisa estar aqui.
#define PIN_PRESSAO      32    // sensor XDB305 via divisor 10k/15k
#define PIN_VBAT         33    // opcional: tensão da bateria via divisor

// ============================================================ Alimentação
// Recomendado medir a bateria: com o carro ligado ela vai a ~14 V e o
// estimador de posição usa essa tensão. Divisor sugerido: 47 kΩ em cima,
// 10 kΩ embaixo (14,4 V -> 2,5 V no pino).
#ifndef USAR_VBAT_MEDIDA
#define USAR_VBAT_MEDIDA 0      // 1 = mede a bateria no PIN_VBAT (recomendado no carro)
#endif
const float VBAT_FIXA      = 12.93f;                 // V, valor inicial; substituído pelo digitado no boot
const float VBAT_DIVISOR   = (47000.0f + 10000.0f) / 10000.0f;

// Verificação da bateria na partida: o sistema pede a tensão medida no
// multímetro (bateria em repouso) e NÃO inicia abaixo de VBAT_MIN_PARTIDA.
const float VBAT_MIN_PARTIDA = 11.8f;   // V
const float VBAT_MIN_ACEITA  = 5.0f;    // V, abaixo disso é erro de digitação
const float VBAT_MAX_ACEITA  = 16.0f;   // V, acima disso é erro de digitação

// ============================================================ Motor da válvula
// Resistência vista pelo motor. Com VBAT_FIXA (tensão em vazio), inclua a
// resistência da fonte/fiação: 4,2 Ω da válvula + 0,23 Ω = 4,43 Ω.
// Medindo a bateria perto da ponte H, use só a da válvula + fios (~4,2 Ω).
const float R_MOTOR        = 4.43f;

const int   PWM_FREQ       = 20000;
const int   PWM_RES        = 8;      // 0..255
const int   PWM_CAL        = 200;    // autocalibração e busca de batente
const int   PWM_MIN        = 150;    // abaixo disso a leitura de corrente fica ruim
const int   PWM_MAX        = 200;    // = PWM_CAL: acima disso a partida satura o IS
const float KP_POS         = 8.0f;   // PWM por % de erro de posição

const float BANDA_PARTIR   = 1.5f;   // % de erro para começar a mover
const float BANDA_PARAR    = 0.7f;   // % de erro para parar
const float POS_FIM        = 1.0f;   // ref <= 1 % -> vai ao batente aberto; >= 99 % -> fechado

// LIMITE DE FECHAMENTO: nenhum comando leva a válvula além disso
// (m, a/PID, ensaio, degrau, pwm, rampa, diag). Com valor < 100:
//   - a autocalibração 'h' fica BLOQUEADA (ela precisa ir ao batente fechado);
//   - o ensaio de velocidades no sentido de fechar não tem curso completo.
// A posição é ESTIMADA (erro de alguns %, já se viu até ~14 %): deixe margem.
// 100 = sem limite.
const float POS_MAX_FECHAMENTO = 90.0f;   // % de fechamento
const float ZONA_FIM       = 25.0f;  // travamento a menos de 25 % de um extremo = batente

// Detecção de batente em tempo real: corrente / corrente de travamento
const float    R_BATENTE      = 0.85f;
const uint32_t T_BATENTE_MS   = 60;    // tempo acima do limiar para confirmar
const uint32_t T_BLANK_MS     = 80;    // ignora o pico de partida
const float    TAU_RAZAO_S    = 0.02f; // filtro da razão de corrente
const float    TAU_FREIO_S    = 0.008f;// queda da velocidade ao frear (≈ τm identificado, 7-11 ms)

const uint32_t T_HOMING_MS    = 1800;  // acionamento fixo na autocalibração (curso chegou a 1232 ms com fonte fraca)
const uint32_t T_BUSCA_MS     = 2000;  // tempo máximo procurando um batente
const uint32_t T_MAX_MOV_MS   = 2500;  // movimento contínuo máximo (fora da busca)

const uint32_t IS_FALHA_MV    = 3050;  // tensão no pino IS que indica falha do chip (ADC satura ~3,1 V)
const uint8_t  N_IS_FALHA     = 10;    // amostras seguidas (10 x 2 ms = 20 ms): ignora o pico de partida
const uint32_t IS_ALERTA_MV   = 2850;  // aviso na autocalibração: sinal do IS perto da saturação

// Autocalibração na partida (abre, fecha e abre, ~4 s).
//   0 = desligada (por enquanto): usa os valores *_PADRAO abaixo e só vai ao
//       batente aberto para referenciar a posição. O comando 'h' continua valendo.
//   1 = calibra a cada boot.
#ifndef AUTOCALIBRAR_NA_PARTIDA
#define AUTOCALIBRAR_NA_PARTIDA 0
#endif

// Valores usados sem autocalibração (ou até a primeira). Vêm da última
// calibração bem-sucedida (12/11/2026). Rode 'h' e atualize aqui quando
// mudar a fiação, o módulo ou a fonte.
const float K_IS_FECHAR_PADRAO = 1037.0f;  // mV no pino por A, por unidade de duty
const float K_IS_ABRIR_PADRAO  = 992.0f;
const float KE_FECHAR_PADRAO   = 0.0332f;  // V por (%/s)
const float KE_ABRIR_PADRAO    = 0.0410f;

// ============================================================ Sensor de pressão
const float P_DIV_CIMA     = 10000.0f;
const float P_DIV_BAIXO    = 15000.0f;
const float V_SENSOR_MIN   = 0.60f;    // abaixo: sensor desligado/defeito
const float V_SENSOR_MAX   = 5.15f;    // acima: fora da faixa do ADC
const float ZERO_TOL_V     = 0.08f;    // zero automático só se estiver a ±0,2 bar do zero da calibração
const float TAU_FILTRO_P   = 0.10f;    // s, filtro passa-baixa da pressão (pulsos do escapamento)

// Compensação térmica do zero (bar por °C). 0 até medir no teste de aquecimento.
const float COEF_ZERO_BAR_POR_C = 0.0f;
const float TEMP_FIXA      = 25.0f;    // °C, enquanto o sensor de temperatura não estiver ligado

// ============================================================ Laços de controle
const uint32_t TS_INTERNO_US  = 2000;  // malha de posição: 500 Hz
const uint32_t TS_EXTERNO_MS  = 20;    // PID de pressão: 50 Hz

// PID de pressão: saída em % de fechamento, erro em bar
//   Kp: %/bar   Ki: %/(bar·s)   Kd: %·s/bar
const float KP_PADRAO      = 50.0f;
const float KI_PADRAO      = 20.0f;
const float KD_PADRAO      = 0.0f;
const float TF_DERIV_S     = 0.05f;    // filtro da derivada
const float P_SP_MAX       = 2.0f;     // setpoint máximo aceito (bar)
const float PID_SAIDA_MAX  = POS_MAX_FECHAMENTO;    // % máx. de fechamento em AUTO: evita o batente fechado
                                       // (a válvula já emperrou ao sair dele) e a região de ganho altíssimo

// Desarme por sobrepressão: vale em QUALQUER modo (inclusive MANUAL nos ensaios).
// Abre a válvula e volta ao MANUAL. AJUSTE ao limite de contrapressão do motor
// (manual do fabricante / responsável pelo dinamômetro) antes de rodar com carga.
const float   P_DESARME_BAR   = P_SP_MAX + 0.3f;
const uint8_t N_DESARME       = 3;     // passos seguidos acima do limite (3 x 20 ms)