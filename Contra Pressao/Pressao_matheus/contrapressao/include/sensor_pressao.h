/*
  Sensor de pressão XIDIBEI XDB305, 0-10 bar, saída 1-5 V
  Calibração LTMD /001/2026 (21,4 °C), ajuste linear dos 66 pontos:
      V = V0 + S * P      ->      P = (V - V0) / S
  Erro máximo do ajuste: 7 mV (0,018 bar).

  Zero automático: com o motor do carro DESLIGADO o escapamento está na
  pressão ambiente; zerarSensorPressao() mede esse zero e corrige o
  deslocamento do dia (temperatura de partida, envelhecimento).
*/
#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"

const float P_V0_CAL  = 0.99696f;   // V com 0 bar (calibração)
const float P_S       = 0.39776f;   // V por bar
const float P_DIVISOR = (P_DIV_CIMA + P_DIV_BAIXO) / P_DIV_BAIXO;

static float pV0   = P_V0_CAL;      // zero em uso
static float tZero = TEMP_FIXA;     // temperatura no momento do zero

void iniciarSensorPressao() {
  analogSetPinAttenuation(PIN_PRESSAO, ADC_11db);   // 0 a ~3,1 V no pino
}

// Tensão na saída do sensor (V)
float lerTensaoSensorPressao(int amostras = 16) {
  uint32_t soma = 0;
  for (int k = 0; k < amostras; k++) soma += analogReadMilliVolts(PIN_PRESSAO);
  return (soma / (float)amostras) / 1000.0f * P_DIVISOR;
}

// ---- Amostragem contínua
// A pulsação do escapamento já é amortecida mecanicamente antes do sensor.
// Aqui a média só reduz o ruído elétrico do ADC (PWM da ponte H, ruído do
// próprio ESP32): uma amostra a cada passo da malha interna (500 Hz) e a
// malha externa usa a média do bloco de 20 ms (~10 amostras).
static uint32_t pSomaMv = 0;
static uint16_t pN = 0;

void amostrarPressao() {
  pSomaMv += analogReadMilliVolts(PIN_PRESSAO);
  pN++;
}

// Média desde a última chamada (V na saída do sensor) e zera o acumulador
float tensaoMediaPressao() {
  if (pN == 0) return lerTensaoSensorPressao(4);
  float v = (pSomaMv / (float)pN) / 1000.0f * P_DIVISOR;
  pSomaMv = 0;
  pN = 0;
  return v;
}

bool tensaoPressaoValida(float v) {
  return v > V_SENSOR_MIN && v < V_SENSOR_MAX;
}

// Correção do zero com a temperatura do sensor (coeficiente medido depois)
float compensarTemperatura(float pressaoBar, float temperaturaC) {
  return pressaoBar - COEF_ZERO_BAR_POR_C * (temperaturaC - tZero);
}

float tensaoParaBar(float v, float temperaturaC) {
  return compensarTemperatura((v - pV0) / P_S, temperaturaC);
}

// Mede o zero (~0,5 s). Recusa se estiver longe do zero da calibração,
// o que indica motor ligado ou sensor com problema.
bool zerarSensorPressao(float temperaturaC, float *vMedido = nullptr) {
  float soma = 0;
  for (int k = 0; k < 25; k++) { soma += lerTensaoSensorPressao(16); delay(20); }
  float v = soma / 25.0f;
  if (vMedido) *vMedido = v;
  if (fabsf(v - P_V0_CAL) > ZERO_TOL_V) return false;
  pV0 = v;
  tZero = temperaturaC;
  return true;
}
