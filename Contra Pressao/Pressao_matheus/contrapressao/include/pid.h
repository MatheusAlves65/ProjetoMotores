/*
  PID com:
   - derivada sobre a medição (sem "chute" quando o setpoint muda) e filtrada
   - anti-windup por integração condicional
   - partida sem tranco (reset() a partir da saída atual)
*/
#pragma once
#include <math.h>

struct Pid {
  float kp, ki, kd;
  float tf;               // constante de tempo do filtro da derivada (s)
  float saidaMin, saidaMax;

  float integral = 0.0f;
  float deriv = 0.0f;
  float medAnterior = NAN;
  float p = 0, i = 0, d = 0;   // parcelas da última execução (telemetria)

  Pid(float kp_, float ki_, float kd_, float tf_, float mn, float mx)
      : kp(kp_), ki(ki_), kd(kd_), tf(tf_), saidaMin(mn), saidaMax(mx) {}

  // Começa a partir da saída atual, sem degrau
  void reset(float saidaAtual, float setpoint, float medicao) {
    integral = saidaAtual - kp * (setpoint - medicao);
    deriv = 0.0f;
    medAnterior = medicao;
  }

  float passo(float setpoint, float medicao, float dt) {
    float erro = setpoint - medicao;

    // derivada da medição, filtrada
    if (isnan(medAnterior)) medAnterior = medicao;
    float dBruta = -(medicao - medAnterior) / dt;
    medAnterior = medicao;
    float a = dt / (tf + dt);
    deriv += a * (dBruta - deriv);

    p = kp * erro;
    d = kd * deriv;
    float u = p + integral + d;

    // integra só se não estiver saturado empurrando para o mesmo lado
    bool satAlto  = (u >= saidaMax) && (erro > 0);
    bool satBaixo = (u <= saidaMin) && (erro < 0);
    if (!satAlto && !satBaixo) {
      integral += ki * erro * dt;
      if (integral > saidaMax) integral = saidaMax;
      if (integral < saidaMin) integral = saidaMin;
    }
    i = integral;

    u = p + integral + d;
    if (u > saidaMax) u = saidaMax;
    if (u < saidaMin) u = saidaMin;
    return u;
  }
};
