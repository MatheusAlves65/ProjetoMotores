# SensoriamentoSegauto
Timers 1/2/4 estão sendo utilizados para controle pwm das ponte h.
Timer 5 usado pelo PWM nos pinos 44/46
Utilizando da biblioteca Timer interrupt se cria 2 ISR interrupt timer utilizando-se apenas do timer5, Erro encontrado foi de +-1ms.
Configurado igual agora é capaz de fazer intervalos de 10ms à 10s com exatidão de 1ms.

# 📊 **RESUMO VISUAL DO FLUXO**
```
┌──────────────────────────────────────────────────────────────────────┐
│                           INICIALIZAÇÃO                               │
├──────────────────────────────────────────────────────────────────────┤
│ setup()                                                               │
│  ├─ Configura pinos (8 relés, PWM, motores)                          │
│  ├─ Inicializa Serial (115200 baud)                                  │
│  ├─ Inicializa Timers 3 e 5                                          │
│  ├─ Inicializa CAN (500kbps, cristal 8MHz)                           │
│  └─ Carrega configs da EEPROM                                        │
└──────────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────────┐
│                         LOOP PRINCIPAL                                │
└──────────────────────────────────────────────────────────────────────┘
                              ↓
         ┌────────────────────┴────────────────────┐
         ↓                                          ↓
┌─────────────────────┐                  ┌──────────────────────┐
│ TIMEOUT TEMPERATURA │                  │ PROCESSAR MENSAGEM   │
│ (500ms sem mensagem)│                  │ CAN RECEBIDA         │
│                     │                  │                      │
│ • Força filtro com  │                  │ • 0x243 → Reset      │
│   último valor      │                  │ • 0x401 → Heartbeat  │
│ • Segurança contra  │                  │ • 0x402 → Digitais   │
│   falha de sensor   │                  │ • 0x403 → Config Temp│
└─────────────────────┘                  │ • 0x404 → Config Aq. │
                                         │ • 0x405 → Start/Stop │
                                         │ • 0x510 → Temp Data  │
                                         └──────────────────────┘
                              ↓
         ┌────────────────────┴────────────────────┐
         ↓                                          ↓
┌─────────────────────┐                  ┌──────────────────────┐
│ AQUISIÇÃO PERIÓDICA │                  │ MONITORA TEMPERATURA │
│                     │                  │                      │
│ Se tempo >= timer:  │                  │ Sensor 1:            │
│ • Envia 8 remotes   │                  │ • temp1f >= max?     │
│ • Lê respostas      │                  │   → Inicia Timer5    │
│ • Envia 0x426       │                  │ • temp1f < max?      │
│   (pressão+válvula) │                  │   → Cancela Timer5   │
└─────────────────────┘                  │                      │
                                         │ Sensor 2:            │
                                         │ • temp2f >= max?     │
                                         │   → Inicia Timer3    │
                                         │ • temp2f < max?      │
                                         │   → Cancela Timer3   │
                                         └──────────────────────┘
                              ↓
         ┌────────────────────┴────────────────────┐
         ↓                                          ↓
┌─────────────────────┐                  ┌──────────────────────┐
│ CONTROLA MOTOR      │                  │ LIMPA rxId           │
│                     │                  │                      │
│ setMotor(dir,pwm)   │                  │ rxId = 0             │
│ ⚠️ Sempre parado!   │                  │                      │
└─────────────────────┘                  └──────────────────────┘
                              ↓
                    ┌─────────────────┐
                    │ VOLTA AO INÍCIO │
                    │ DO LOOP         │
                    └─────────────────┘



Reles ordem de posicao da direita pra esquerda

Pino D1, ascende led 04 e aciona rele 10
Pino D2, ascende led 03 e aciona rele 09
Pino D3, ascende led 02 e aciona rele 02, mas o led dp pwr do primeiro modulo rele ascende
Pino D4, ascende led 01 e aciona rele 01, mesma coisa do d4
Pino D5 , ascende led08 e aciona rele 03,04,05,06
Pnio D6 ascende led 07 e aciona 03,04,05,06, 08
Pion D7 ascende led 06, aciona rele 05 duvida (aciona todos?)
Pino d8 ascende led 05, aciona rele 08, duvida...




