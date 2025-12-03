//═══════════════════════════════════════════════════════════════════════════
// CONFIGURAÇÕES DE DEBUG E TIMER
//═══════════════════════════════════════════════════════════════════════════

// Define o nível de debug para os timers (0-4)
// ⚠️ CUIDADO: Valores > 0 podem travar o sistema durante ISRs
#define TIMER_INTERRUPT_DEBUG         1
#define _TIMERINTERRUPT_LOGLEVEL_     1

//───────────────────────────────────────────────────────────────────────────
// INFORMAÇÃO IMPORTANTE SOBRE O BOOTLOADER
//───────────────────────────────────────────────────────────────────────────
// Os MCUs deste projeto tiveram o bootloader USB REMOVIDO
// Eles NÃO podem receber uploads via USB e devem ser atualizados via CAN
// Para atualizar o firmware, use o comando:
// mcp-can-boot-flash-app -f your_file.hex -p m2560 -m 0x0042
//───────────────────────────────────────────────────────────────────────────

//───────────────────────────────────────────────────────────────────────────
// CONFIGURAÇÃO DA FREQUÊNCIA DO TIMER
//───────────────────────────────────────────────────────────────────────────
// USING_16MHZ   = true  → Timer mais curto, mas MELHOR precisão
// USING_8MHZ    = true  → Timer mais curto, mas MELHOR precisão  
// USING_250KHZ  = true  → Timer mais longo, mas PIOR precisão
// Não selecionar nenhum = 250KHz (padrão)
//───────────────────────────────────────────────────────────────────────────
#define USING_16MHZ     true   // ✓ Usando 16MHz para máxima precisão
#define USING_8MHZ      false
#define USING_250KHZ    false

//───────────────────────────────────────────────────────────────────────────
// SELEÇÃO DE TIMERS DISPONÍVEIS
//───────────────────────────────────────────────────────────────────────────
// Timers 0,1,2,4 estão sendo usados por outros processos
// Apenas Timers 3 e 5 estão disponíveis para uso neste projeto
//───────────────────────────────────────────────────────────────────────────
#define USE_TIMER_0     false  // ✗ Usado pelo sistema
#define USE_TIMER_1     false  // ✗ Usado pelo sistema
#define USE_TIMER_2     false  // ✗ Usado pelo sistema
#define USE_TIMER_3     true   // ✓ Disponível (usado para temp2)
#define USE_TIMER_4     false  // ✗ Usado pelo sistema
#define USE_TIMER_5     true   // ✓ Disponível (usado para temp1)

//═══════════════════════════════════════════════════════════════════════════
// INCLUDES - BIBLIOTECAS NECESSÁRIAS
//═══════════════════════════════════════════════════════════════════════════
#include "TimerInterrupt_Generic.h"  // Biblioteca de timers por hardware
#include <avr/wdt.h>                 // Watchdog Timer (para reset remoto)
#include "ISR_Timer_Generic.h"       // Timers por ISR (não usado atualmente)
#include <Arduino.h>                 // Core do Arduino
#include <mcp_can.h>                 // Driver do MCP2515 (controlador CAN)
#include <SPI.h>                     // Comunicação SPI com MCP2515
#include <EEPROM.h>                  // Persistência de dados na memória
#include "config.h"                  // ⚠️ Funções auxiliares (parse de msgs CAN)

//═══════════════════════════════════════════════════════════════════════════
// DEFINIÇÕES DE HARDWARE - PINOS
//═══════════════════════════════════════════════════════════════════════════

#ifndef LED_BUILTIN
	#define LED_BUILTIN  13  // LED onboard (caso não esteja definido)
#endif

//───────────────────────────────────────────────────────────────────────────
// SAÍDAS DIGITAIS (8 Relés)
//───────────────────────────────────────────────────────────────────────────
// Estes pinos controlam 6 módulos relé de 2 canais cada:
// - D1/D2: Start Engine / Emergency
// - D3/D4: Saídas digitais genéricas (DO1/DO2)
// - D5/D6: Ventilador/Exaustor
// - D7/D8: Válvulas NA1/NF1 ou Bomba d'água
//───────────────────────────────────────────────────────────────────────────
#define D1  39  // Saída Digital 1
#define D2  41  // Saída Digital 2
#define D3  32  // Saída Digital 3
#define D4  34  // Saída Digital 4
#define D5  36  // Saída Digital 5
#define D6  38  // Saída Digital 6
#define D7  40  // Saída Digital 7
#define D8  42  // Saída Digital 8

// Array para facilitar iteração sobre todos os pinos
int ledpins[8] = {D1, D2, D3, D4, D5, D6, D7, D8};

//───────────────────────────────────────────────────────────────────────────
// IDs DAS MENSAGENS CAN
//───────────────────────────────────────────────────────────────────────────
// DataIDs: IDs das mensagens de dados (Data Frame)
// remoteIDs: Mesmo ID mas com flag de Remote Frame (bit 30 setado)
//            Remote Frame = solicita dados sem enviar payload
//───────────────────────────────────────────────────────────────────────────
unsigned long DataIDs[8] = {
    0x510, 0x520, 0x520,  // Temperaturas? (não documentado completamente)
    0x610, 0x611,          // Sensores diversos? (não documentado)
    0x620, 0x621, 0x622    // Sensores diversos? (não documentado)
};

// Adiciona flag 0x40000000 para criar Remote Frame
// Bit 30 do ID indica que é remote frame no padrão CAN
unsigned long remoteIDs[8] = {
    0x510|0x40000000, 0x520|0x40000000, 0x530|0x40000000,
    0x610|0x40000000, 0x611|0x40000000,
    0x620|0x40000000, 0x621|0x40000000, 0x622|0x40000000
};

//───────────────────────────────────────────────────────────────────────────
// CONFIGURAÇÃO DO MÓDULO CAN (MCP2515)
//───────────────────────────────────────────────────────────────────────────
#define CAN0_INT  3        // Pino de interrupção do MCP2515 (INT)
MCP_CAN CAN0(33);          // Pino CS (Chip Select) do MCP2515 = 33

// Variáveis para recepção de mensagens CAN
long unsigned int rxId;           // ID da mensagem recebida
long unsigned int rxIdData[8];    // Buffer de IDs (não usado atualmente)
unsigned char len;                // Tamanho da mensagem (DLC)
unsigned char rxBuf[8] = " ";  
unsigned long fullId = rxId & 0x1FFFFFFF;   // Buffer de dados recebidos (max 8 bytes)

// Buffers para transmissão
byte txBufDebug[8] = {0x55, 0x55, 0x55, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
byte txBuf[8] = " ";              // Buffer de dados a transmitir
char serialString[128];           // Buffer para strings serial (não usado)

//═══════════════════════════════════════════════════════════════════════════
// ESTRUTURAS DE DADOS - CONFIGURAÇÕES DO SISTEMA
//═══════════════════════════════════════════════════════════════════════════

//───────────────────────────────────────────────────────────────────────────
// MONITORAMENTO DE TEMPERATURA E SEGURANÇA
//───────────────────────────────────────────────────────────────────────────
// Estrutura definida em config.h (não vemos aqui, mas inferida pelo uso)
// Provavelmente contém:
// struct safetyConfigStructure {
//     float maxtemp;          // Temperatura máxima permitida (°C)
//     uint16_t timer;         // Tempo que temp deve ficar acima (ms)
//     uint8_t Monit_Enable;   // Habilitado? 0=não, 1=sim, 2=erro
//     uint8_t saveeeprom;     // Flag para salvar na EEPROM
// };
//───────────────────────────────────────────────────────────────────────────
safetyConfigStructure temp1c;     // Configuração do sensor de temp 1
safetyConfigStructure temp2c;     // Configuração do sensor de temp 2
safetyConfigStructure tconfigbuf; // Buffer temporário para receber config

//───────────────────────────────────────────────────────────────────────────
// LEITURA DE TEMPERATURA
//───────────────────────────────────────────────────────────────────────────
// Estrutura para armazenar dados dos sensores de temperatura
// Provavelmente contém:
// struct tempReadStructure {
//     int16_t BLtemp;   // Temperatura do termopar (Bare Lead)
//     int16_t CJtemp;   // Temperatura de junção fria (Cold Junction)
//     uint8_t BLstatus; // Status do sensor (1=OK, 0=erro)
// };
//───────────────────────────────────────────────────────────────────────────
tempReadStructure temp1s;  // Dados do sensor 1 (implementado)
tempReadStructure temp2s;  // Dados do sensor 2 (não implementado)
tempReadStructure temp3s;  // Dados do sensor 3 (não implementado)

//───────────────────────────────────────────────────────────────────────────
// CONFIGURAÇÃO DE AQUISIÇÃO DE DADOS
//───────────────────────────────────────────────────────────────────────────
// Estrutura definida em config.h, provavelmente:
// struct aquisitionConfigStructure {
//     uint16_t timer;                    // Taxa de aquisição (10-3000ms)
//     uint8_t Aquics_Enable;             // Habilitado temporariamente
//     uint8_t Aquics_Enable_Continuous;  // Modo contínuo
// };
//───────────────────────────────────────────────────────────────────────────
aquisitionConfigStructure aquisc;      // Configuração atual
aquisitionConfigStructure aconfigbuf;  // Buffer para receber nova config

//───────────────────────────────────────────────────────────────────────────
// SENSORES ANALÓGICOS (Não implementados ainda)
//───────────────────────────────────────────────────────────────────────────
// TODO: Ler valores reais do ADC
uint8_t downpipePress = 0;  // Pressão no downpipe (0-20 bar) - sempre 0
uint8_t valvPos = 0;        // Posição válvula borboleta (0-100%) - sempre 0

//═══════════════════════════════════════════════════════════════════════════
// CONTROLE DE MOTORES - PONTE H
//═══════════════════════════════════════════════════════════════════════════
// Pinos para controlar 2 motores DC via ponte H (L298N ou similar)
// F_PWM = Forward (frente), R_PWM = Reverse (ré)
//───────────────────────────────────────────────────────────────────────────
#define F_PWM_1  6   // Forward PWM - Motor 1
#define R_PWM_1  8   // Reverse PWM - Motor 1

#define F_PWM_2  16  // Forward PWM - Motor 2  
#define R_PWM_2  37  // Reverse PWM - Motor 2

// Saídas PWM adicionais (propósito não claro na documentação)
#define PWM1  44
#define PWM2  46

// Pinos de enable dos motores
#define MOTOR1_EN  16
#define MOTOR2_EN  17

//───────────────────────────────────────────────────────────────────────────
// VARIÁVEIS DE CONTROLE DO MOTOR
//───────────────────────────────────────────────────────────────────────────
// ⚠️ PROBLEMA: Estas variáveis nunca mudam de valor!
//    dir e pwmVal sempre ficam em 0, então o motor nunca se move
//───────────────────────────────────────────────────────────────────────────
int dir = 0;       // Direção: 0=parado, 1=horário, 2=anti-horário
int pwmVal = 0;    // Velocidade PWM (0-255)
int PWM1_val = 0;  // Valor PWM1 recebido via CAN
int PWM2_val = 0;  // Valor PWM2 recebido via CAN
int Enc = 0;       // Encoder (não implementado corretamente)
int Temp = 0;      // Variável temporária para leituras

//═══════════════════════════════════════════════════════════════════════════
// CONTROLE DE TEMPO E TIMERS
//═══════════════════════════════════════════════════════════════════════════

#define LED_TOGGLE_INTERVAL_MS  1000L  // Intervalo para toggle LED (não usado)

// Timestamps para controle de tempo
volatile uint32_t previousMillistimer = 0;   // Timer1 (temperatura 1)
volatile uint32_t previousMillistimer2 = 0;  // Timer2 (temperatura 2)
uint32_t timetempmess = 0;                   // Último recebimento de temp
uint32_t timeaquisition = 0;                 // Último ciclo de aquisição

// Flags de controle dos timers de segurança
bool starttimer1 = 0;  // Timer1 está ativo?
bool starttimer2 = 0;  // Timer2 está ativo?

//───────────────────────────────────────────────────────────────────────────
// VARIÁVEIS DE TEMPERATURA
//───────────────────────────────────────────────────────────────────────────
// volatile = pode mudar durante ISR (Interrupt Service Routine)
//───────────────────────────────────────────────────────────────────────────
volatile int16_t temp1 = 120;   // Temp bruta sensor 1 (não usado)
volatile int16_t temp2 = 120;   // Temp bruta sensor 2 (não usado)
volatile float temp1f = 0;      // Temp filtrada sensor 1 (°C)
volatile float temp2f = 0;      // Temp filtrada sensor 2 (°C)
volatile float temp3f = 0;      // Temp filtrada sensor 3 (°C)
volatile float prevtempf1 = 0;  // Temp anterior para filtro 1
volatile float prevtempf2 = 0;  // Temp anterior para filtro 2

//═══════════════════════════════════════════════════════════════════════════
// SISTEMA DE PERSISTÊNCIA - EEPROM
//═══════════════════════════════════════════════════════════════════════════
// Endereços na EEPROM onde cada variável é salva
// EEPROM tem ~4KB no ATmega2560 (endereços 0-4095)
//───────────────────────────────────────────────────────────────────────────
int addrtemp1max = 0;                                      // float (4 bytes)
int addrtemp2max = sizeof(float);                          // float (4 bytes)
int addrtimer1 = sizeof(float) + sizeof(float);            // uint16_t (2 bytes)
int addrtimer2 = sizeof(float) + sizeof(float) + sizeof(uint16_t);  // uint16_t
int monitenable1 = sizeof(float) + sizeof(float) + sizeof(uint16_t) + sizeof(uint16_t);  // uint8_t
int monitenable2 = sizeof(float) + sizeof(float) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint8_t);  // uint8_t

// Layout da EEPROM:
// ┌─────────────┬──────────┬────────┐
// │ Endereço    │ Variável │ Bytes  │
// ├─────────────┼──────────┼────────┤
// │ 0-3         │ temp1max │ 4      │
// │ 4-7         │ temp2max │ 4      │
// │ 8-9         │ timer1   │ 2      │
// │ 10-11       │ timer2   │ 2      │
// │ 12          │ enable1  │ 1      │
// │ 13          │ enable2  │ 1      │
// └─────────────┴──────────┴────────┘

//═══════════════════════════════════════════════════════════════════════════
// FUNÇÕES AUXILIARES - EEPROM
//═══════════════════════════════════════════════════════════════════════════

//───────────────────────────────────────────────────────────────────────────
// SALVAR FLOAT NA EEPROM
//───────────────────────────────────────────────────────────────────────────
// EEPROM.update() só escreve se o valor mudou (economiza ciclos de escrita)
// EEPROM tem vida útil limitada (~100.000 escritas por byte)
//───────────────────────────────────────────────────────────────────────────
void updateEEPROMFloat(int address, float value) {
    byte *p = (byte*)(void*)&value;  // Converte float para array de bytes
    for (int i = 0; i < sizeof(float); i++) {
        EEPROM.update(address + i, p[i]);  // Salva byte por byte
    }
}

//───────────────────────────────────────────────────────────────────────────
// SALVAR UINT16 NA EEPROM (mesmo princípio do float)
//───────────────────────────────────────────────────────────────────────────
void updateEEPROMUInt16(int address, uint16_t value) {
    byte *p = (byte*)(void*)&value;
    for (int i = 0; i < sizeof(uint16_t); i++) {
        EEPROM.update(address + i, p[i]);
    }
}

//───────────────────────────────────────────────────────────────────────────
// SALVAR UINT8 NA EEPROM (1 byte, simples)
//───────────────────────────────────────────────────────────────────────────
void updateEEPROMUInt8(int address, uint8_t value) {
    EEPROM.update(address, value);
}

//───────────────────────────────────────────────────────────────────────────
// LER FLOAT DA EEPROM
//───────────────────────────────────────────────────────────────────────────
float readEEPROMFloat(int address) {
    float value;
    byte *p = (byte*)(void*)&value;  // Ponteiro para os bytes do float
    for (int i = 0; i < sizeof(float); i++) {
        p[i] = EEPROM.read(address + i);  // Lê byte por byte
    }
    return value;
}

//───────────────────────────────────────────────────────────────────────────
// LER UINT16 DA EEPROM
//───────────────────────────────────────────────────────────────────────────
uint16_t readEEPROMUInt16(int address) {
    uint16_t value;
    byte *p = (byte*)(void*)&value;
    for (int i = 0; i < sizeof(uint16_t); i++) {
        p[i] = EEPROM.read(address + i);
    }
    return value;
}

//───────────────────────────────────────────────────────────────────────────
// LER UINT8 DA EEPROM
//───────────────────────────────────────────────────────────────────────────
uint8_t readEEPROMUInt8(int address) {
    return EEPROM.read(address);
}

//═══════════════════════════════════════════════════════════════════════════
// FUNÇÕES DE ALTO NÍVEL - SALVAR/CARREGAR CONFIGURAÇÕES
//═══════════════════════════════════════════════════════════════════════════

//───────────────────────────────────────────────────────────────────────────
// SALVAR CONFIGURAÇÃO DE SEGURANÇA 1
//───────────────────────────────────────────────────────────────────────────
void updateSafetyConfig1(const safetyConfigStructure &temp1c) {
    updateEEPROMFloat(addrtemp1max, temp1c.maxtemp);      // Temperatura max
    updateEEPROMUInt16(addrtimer1, temp1c.timer);         // Timer (ms)
    updateEEPROMUInt8(monitenable1, temp1c.Monit_Enable); // Habilitado?
}

//───────────────────────────────────────────────────────────────────────────
// SALVAR CONFIGURAÇÃO DE SEGURANÇA 2
//───────────────────────────────────────────────────────────────────────────
void updateSafetyConfig2(const safetyConfigStructure &temp2c) {
    updateEEPROMFloat(addrtemp2max, temp2c.maxtemp);
    updateEEPROMUInt16(addrtimer2, temp2c.timer);
    updateEEPROMUInt8(monitenable2, temp2c.Monit_Enable);
}

//───────────────────────────────────────────────────────────────────────────
// CARREGAR AMBAS CONFIGURAÇÕES DA EEPROM
//───────────────────────────────────────────────────────────────────────────
// Chamado no setup() para restaurar configurações após reboot
//───────────────────────────────────────────────────────────────────────────
void loadSafetyConfigs(safetyConfigStructure &temp1c, 
                       safetyConfigStructure &temp2c) {
    // Carrega config 1
    temp1c.maxtemp = readEEPROMFloat(addrtemp1max);
    temp1c.timer = readEEPROMUInt16(addrtimer1);
    temp1c.Monit_Enable = readEEPROMUInt8(monitenable1);

    // Carrega config 2
    temp2c.maxtemp = readEEPROMFloat(addrtemp2max);
    temp2c.timer = readEEPROMUInt16(addrtimer2);
    temp2c.Monit_Enable = readEEPROMUInt8(monitenable2);
}

//═══════════════════════════════════════════════════════════════════════════
// ISR (INTERRUPT SERVICE ROUTINES) - HANDLERS DE TIMER
//═══════════════════════════════════════════════════════════════════════════

//───────────────────────────────────────────────────────────────────────────
// TIMER 1 - SEGURANÇA TEMPERATURA 1
//───────────────────────────────────────────────────────────────────────────
// ⚠️ ATENÇÃO: Esta função é chamada quando a temperatura 1 fica MUITO TEMPO
//             acima do limite. Aqui você DEVE implementar ações de segurança!
//
// EXEMPLO DE IMPLEMENTAÇÃO NECESSÁRIA:
//   - Desligar motor de combustão
//   - Desligar bomba
//   - Acionar alarme
//   - Enviar mensagem CAN de emergência
//───────────────────────────────────────────────────────────────────────────
void TimerHandler1() {
	Serial.print("ITimer1 called, millis() = ");
	Serial.println(millis() - previousMillistimer);
	previousMillistimer = millis();
	
	// TODO: IMPLEMENTAR AÇÕES DE SEGURANÇA AQUI!
	// Exemplo:
	// setMotor(0, 0);  // Para motor
	// digitalWrite(D1, LOW);  // Desliga start
	// CAN0.sendMsgBuf(0x700, 1, 0xFF);  // Mensagem de emergência
}

//───────────────────────────────────────────────────────────────────────────
// TIMER 2 - SEGURANÇA TEMPERATURA 2
//───────────────────────────────────────────────────────────────────────────
// Mesmo conceito do Timer1, mas para o segundo sensor de temperatura
//───────────────────────────────────────────────────────────────────────────
void TimerHandler2() {
	// Código comentado - provavelmente para reduzir output no serial
	//Serial.print("ITimer2 called, millis() = ");
	//Serial.println(millis() - previousMillistimer2);
	previousMillistimer2 = millis();
	
	// TODO: IMPLEMENTAR AÇÕES DE SEGURANÇA AQUI!
}

//═══════════════════════════════════════════════════════════════════════════
// CONTROLE DE MOTOR DC - PONTE H
//═══════════════════════════════════════════════════════════════════════════

//───────────────────────────────────────────────────────────────────────────
// FUNÇÃO: setMotor
// Controla direção e velocidade de 2 motores DC via ponte H
//───────────────────────────────────────────────────────────────────────────
// PARÂMETROS:
//   dir    - Direção: 0=parado, 1=sentido horário, 2=sentido anti-horário
//   pwmVal - Velocidade: 0-255 (duty cycle do PWM)
//
// FUNCIONAMENTO DA PONTE H:
//   - Para girar num sentido: F_PWM=0, R_PWM=velocidade
//   - Para girar no outro: F_PWM=velocidade, R_PWM=0
//   - Para parar: F_PWM=0, R_PWM=0
//───────────────────────────────────────────────────────────────────────────
void setMotor(int dir, int pwmVal) {
    if (dir == 1) {
        // Sentido 1 (ex: válvula abrindo)
        analogWrite(R_PWM_2, pwmVal);  // Motor 2 - Reverse ativo
        analogWrite(F_PWM_2, 0);       // Motor 2 - Forward desligado
        analogWrite(R_PWM_1, pwmVal);  // Motor 1 - Reverse ativo
        analogWrite(F_PWM_1, 0);       // Motor 1 - Forward desligado
    } 
    else if (dir == 2) {
        // Sentido 2 (ex: válvula fechando)
        analogWrite(F_PWM_2, pwmVal);  // Motor 2 - Forward ativo
        analogWrite(R_PWM_2, 0);       // Motor 2 - Reverse desligado
        analogWrite(F_PWM_1, pwmVal);  // Motor 1 - Forward ativo
        analogWrite(R_PWM_1, 0);       // Motor 1 - Reverse desligado
    } 
    else if (dir == 0) {
        // Parado (freio elétrico)
        analogWrite(R_PWM_2, 0);
        analogWrite(F_PWM_2, 0);
        analogWrite(R_PWM_1, 0);
        analogWrite(F_PWM_1, 0);
    }
}

//═══════════════════════════════════════════════════════════════════════════
// FILTROS DIGITAIS - SUAVIZAÇÃO DE LEITURA DE TEMPERATURA
//═══════════════════════════════════════════════════════════════════════════

//───────────────────────────────────────────────────────────────────────────
// FILTRO EXPONENCIAL (EMA - Exponential Moving Average)
// Sensor 1
//───────────────────────────────────────────────────────────────────────────
// FÓRMULA: filtrado[n] = α * novo + (1-α) * filtrado[n-1]
//
// α = 0.1 significa:
//   - Novo valor contribui com 10%
//   - Histórico contribui com 90%
//   - Resultado: resposta LENTA mas SUAVE (remove ruído)
//
// α maior = mais responsivo mas mais ruidoso
// α menor = mais suave mas mais lento
//───────────────────────────────────────────────────────────────────────────
float filterSensorValue1(int16_t newValue) {
    static float filteredValue1 = 25;  // Valor inicial (temperatura ambiente)
    const float alpha1 = 0.1f;         // Fator de suavização (0.0-1.0)
    
    // Aplica filtro
    filteredValue1 = alpha1 * newValue + (1 - alpha1) * prevtempf1;
    prevtempf1 = filteredValue1;  // Salva para próxima iteração
    
    return filteredValue1;
}

//───────────────────────────────────────────────────────────────────────────
// FILTRO EXPONENCIAL - Sensor 2 (idêntico ao sensor 1)
//───────────────────────────────────────────────────────────────────────────
float filterSensorValue2(int16_t newValue) {
    static float filteredValue2 = 25;
    const float alpha2 = 0.1f;
    filteredValue2 = alpha2 * newValue + (1 - alpha2) * prevtempf2;
    prevtempf2 = filteredValue2;
    return filteredValue2;
}

//═══════════════════════════════════════════════════════════════════════════
// SETUP() - INICIALIZAÇÃO DO SISTEMA
//═══════════════════════════════════════════════════════════════════════════

void setup()
{
	//───────────────────────────────────────────────────────────────────────
	// CONFIGURAÇÃO DOS PINOS DE SAÍDA
	//───────────────────────────────────────────────────────────────────────
	pinMode(LED_BUILTIN, OUTPUT);  // LED onboard (pino 13)
	
	// Configura os 8 pinos de controle dos relés como saída
	pinMode(D1, OUTPUT);
	pinMode(D2, OUTPUT);
	pinMode(D3, OUTPUT);
	pinMode(D4, OUTPUT);
	pinMode(D5, OUTPUT);
	pinMode(D6, OUTPUT);
	pinMode(D7, OUTPUT);
	pinMode(D8, OUTPUT);
	
	// Configura pino de interrupção do CAN como entrada
	pinMode(CAN0_INT, INPUT);

	//───────────────────────────────────────────────────────────────────────
	// CONFIGURAÇÃO DOS PINOS PWM E PONTE H
	//───────────────────────────────────────────────────────────────────────
	pinMode(PWM1, OUTPUT);     // PWM auxiliar 1 (pino 44)
	pinMode(PWM2, OUTPUT);     // PWM auxiliar 2 (pino 46)
	
	// Pinos da ponte H para controle de motores
	pinMode(R_PWM_1, OUTPUT);  // Reverse Motor 1 (pino 8)
    pinMode(F_PWM_1, OUTPUT);  // Forward Motor 1 (pino 6)
    pinMode(R_PWM_2, OUTPUT);  // Reverse Motor 2 (pino 37)
    pinMode(F_PWM_2, OUTPUT);  // Forward Motor 2 (pino 16)
	
	// Pinos de enable dos motores
	pinMode(MOTOR1_EN, OUTPUT);
    pinMode(MOTOR2_EN, OUTPUT);
    
    // Habilita os motores (ponte H ativada)
    digitalWrite(MOTOR1_EN, HIGH);
    digitalWrite(MOTOR2_EN, HIGH);

	//───────────────────────────────────────────────────────────────────────
	// INICIALIZAÇÃO DA COMUNICAÇÃO SERIAL
	//───────────────────────────────────────────────────────────────────────
	Serial.begin(115200);  // 115200 baud para debug

	//───────────────────────────────────────────────────────────────────────
	// INICIALIZAÇÃO DOS TIMERS DE HARDWARE
	//───────────────────────────────────────────────────────────────────────
	// ITimer3 e ITimer5 são usados para monitoramento de temperatura
	// São inicializados aqui, mas attachInterrupt() só é chamado quando
	// a temperatura ultrapassa o limite
	ITimer3.init();  // Timer 3 para sensor de temperatura 2
	ITimer5.init();  // Timer 5 para sensor de temperatura 1

	//───────────────────────────────────────────────────────────────────────
	// INICIALIZAÇÃO DO MÓDULO CAN (MCP2515)
	//───────────────────────────────────────────────────────────────────────
	// MCP_ANY: aceita mensagens standard e extended
	// CAN_500KBPS: velocidade do barramento CAN
	// MCP_8MHZ: frequência do cristal do MCP2515
	if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK){
		Serial.println("MCP2515 Initialized Successfully!");
		Serial.println("TEste se esta funcionando");
	} else {
		Serial.println("Error Initializing MCP2515...");
		// ⚠️ Nota: O código continua mesmo com erro. Melhor seria:
		// while(1);  // Trava aqui se CAN não inicializar
	}

		// Muda para modo normal (permite transmitir e receber)
	// Outros modos disponíveis: MCP_LOOPBACK (teste), MCP_LISTENONLY (só lê)
	CAN0.setMode(MCP_NORMAL);
	
	byte tempLen;
    byte tempBuf[8];
    unsigned long tempId;
    for(int i = 0; i < 100; i++) {
        if(CAN0.checkReceive() == CAN_MSGAVAIL) {
            CAN0.readMsgBuf(&tempId, &tempLen, tempBuf);
        } else {
            break;
        }
    }
    Serial.println("Buffer CAN limpo!");
    




	
	//───────────────────────────────────────────────────────────────────────
	// CARREGA CONFIGURAÇÕES SALVAS DA EEPROM
	//───────────────────────────────────────────────────────────────────────
	// Restaura as configurações de temperatura que foram salvas anteriormente
	// Se for a primeira vez que o código roda, valores serão aleatórios!
	// Melhor seria inicializar EEPROM com valores padrão na primeira execução
	loadSafetyConfigs(temp1c, temp2c);

	// Configura padrão para aquisição contínua automática
    aquisc.Aquics_Enable_Continuous = 1; // 1 = Habilitado, 0 = Desabilitado
    aquisc.timer = 100;                  // Solicita temperatura a cada 100ms
    timeaquisition = millis();           // Inicializa o contador de tempo
    
    Serial.println("MODO CONTINUO INICIADO AUTOMATICAMENTE");
}

//═══════════════════════════════════════════════════════════════════════════
// LOOP() - CICLO PRINCIPAL DO PROGRAMA
//═══════════════════════════════════════════════════════════════════════════
// Este loop executa continuamente após setup()
// Processa mensagens CAN, monitora temperatura, controla aquisição, etc.
//═══════════════════════════════════════════════════════════════════════════

// ...existing code...

void loop()
{
    // LED pisca = loop está rodando (Heartbeat visual)
    static unsigned long lastBlink = 0;
    static unsigned long loopCounter = 0;
    loopCounter++;
    
    if(millis() - lastBlink > 1000) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        loopCounter = 0;
        lastBlink = millis();
    }
    
    //═══════════════════════════════════════════════════════════════════════
    // PROCESSAMENTO CAN (Prioridade Alta)
    //═══════════════════════════════════════════════════════════════════════
    
    // Variável local para armazenar o ID limpo desta iteração
    unsigned long currentFullId = 0; 

    if(!digitalRead(CAN0_INT)) {
        // Lê mensagem
        if(CAN0.readMsgBuf(&rxId, &len, rxBuf) == CAN_OK) {
            
            // [CORREÇÃO CRÍTICA] Calcula o ID limpo AQUI, toda vez que chega mensagem
            currentFullId = rxId & 0x1FFFFFFF; 
            
            // Debug se não for mensagem repetitiva (Temp ou Aquisicao)
            if(currentFullId != 0x510 && currentFullId != 0x426) { 
                Serial.print("RX ID: 0x"); Serial.print(rxId, HEX);
                Serial.print(" -> ID Limpo: 0x"); Serial.print(currentFullId, HEX);
                Serial.print(" | DLC: "); Serial.println(len);
            }

            // --- TRATAMENTO DE MENSAGENS ESPECÍFICAS (Usando currentFullId) ---

            // 0x401 - Debug
            if(currentFullId == 0x401){
                Serial.println("cmd: 0x401 (Debug)");
                CAN0.sendMsgBuf(0x401, sizeof(txBufDebug), txBufDebug);
            }

            // 0x402 - SAÍDAS DIGITAIS
            if(currentFullId == 0x402){
                Serial.println("cmd: 0x402 (Saidas)");
                static int result[8];
                readDigital(rxBuf, result);
                
                for (size_t i = 0; i < 8; i++){
                    if(result[i] == 0) digitalWrite(ledpins[i], 0);
                    else if(result[i] == 1) digitalWrite(ledpins[i], 1);
                }
                
                // PWM e Encoder
                int tPWM1 = readPWMEnc(rxBuf, 2);
                int tPWM2 = readPWMEnc(rxBuf, 3);
                int tEnc = readPWMEnc(rxBuf, 4);

                if(tPWM1 >= 0) { PWM1_val = tPWM1; analogWrite(PWM1, PWM1_val); }
                if(tPWM2 >= 0) { PWM2_val = tPWM2; analogWrite(PWM2, PWM2_val); }
                if(tEnc >= 0) Enc = tEnc;

                sendDigital(result, PWM1_val, PWM2_val, Enc, txBuf);
                CAN0.sendMsgBuf(0x422, sizeof(txBuf), txBuf);
            }

            // 0x403 - CONFIG SEGURANÇA (PARSE MANUAL + SALVAMENTO FORÇADO)
            // 0x403 - CONFIG SEGURANÇA (CORRIGIDO PARA T2)
            // 0x403 - CONFIG SEGURANÇA (CORRIGIDO E FORÇADO)
            if(currentFullId == 0x403){
                
                // Se tiver dados na mensagem (Len > 0), atualiza as variáveis
                if(len > 0) {
                    Serial.println("\n--- COMANDO 0x403 RECEBIDO ---");

                    // 1. Atualiza Temp 1 na memória
                    temp1c.Monit_Enable = (rxBuf[1] & 0x03); 
                    temp1c.maxtemp = (float)rxBuf[2]; 
                    temp1c.timer = (uint16_t)((float)rxBuf[3]);
                    temp1c.saveeeprom = 1;

                    // 2. Atualiza Temp 2 na memória
                    temp2c.Monit_Enable = (rxBuf[5] & 0x03);
                    temp2c.maxtemp = (float)rxBuf[6]; 
                    temp2c.timer = (uint16_t)((float)rxBuf[7]);
                    temp2c.saveeeprom = 1;

                    // 3. Salva na EEPROM se estiver habilitado
                    if(temp1c.Monit_Enable != 2) updateSafetyConfig1(temp1c);
                    if(temp2c.Monit_Enable != 2) updateSafetyConfig2(temp2c);
                    
                    Serial.println(" -> Configuracoes salvas na RAM e EEPROM");
                } 

                // 4. PREENCHIMENTO MANUAL DO BUFFER DE RESPOSTA (0x423)
                // Aqui garantimos que os dados serao escritos no txBuf
                
                // --- SENSOR 1 (Bytes 0-3) ---
                txBuf[0] = 0x12; // Cabeçalho Fixo
                txBuf[1] = 0x30 | (temp1c.Monit_Enable & 0x03);
                txBuf[2] = (byte)((float)temp1c.maxtemp); // Converte para escala CAN
                txBuf[3] = (byte)(temp1c.timer);

                // --- SENSOR 2 (Bytes 4-7) ---
                txBuf[4] = 0x12; // Cabeçalho Fixo (FORÇADO)
                txBuf[5] = 0x30 | (temp2c.Monit_Enable & 0x03);
                txBuf[6] = (byte)((float)temp2c.maxtemp); // Converte para escala CAN
                txBuf[7] = (byte)(temp2c.timer);

                // DEBUG: Mostra no terminal o que vai ser enviado
                Serial.print(" > TX (0x423) HEX: ");
                for(int i=0; i<8; i++) { 
                    Serial.print(txBuf[i], HEX); 
                    Serial.print(" "); 
                }
                Serial.println();

                // Envia a resposta (Força tamanho 8 bytes)
                CAN0.sendMsgBuf(0x423, 8, txBuf); 
            }

            // 0x404 - CONFIG AQUISIÇÃO
            if(currentFullId == 0x404){
                Serial.println("cmd: 0x404 (Config Aquisicao)");
                aconfigbuf = aquisitionConfig(rxBuf);
                if(aconfigbuf.Aquics_Enable != 2) aquisc = aconfigbuf;
                
                sendaquisitionConfig(aquisc, txBuf);
                CAN0.sendMsgBuf(0x424, sizeof(txBuf), txBuf);
            }

            // 0x405 - START/STOP
            if(currentFullId == 0x405){
                uint8_t EnableBuf = (rxBuf[0] >> 6);
                if(EnableBuf < 2) {
                    aquisc.Aquics_Enable = EnableBuf;
                    Serial.print("cmd: 0x405 -> "); 
                    Serial.println(EnableBuf ? "START" : "STOP");
                }
                byte EnableBufc[1];
                EnableBufc[0] = ((aquisc.Aquics_Enable << 6) & 0xC0);
                CAN0.sendMsgBuf(0x425, 8, EnableBufc);
            }

            // 0x510 - LEITURA DE TEMPERATURA
            if (currentFullId == 0x510){
                temp1s = tempRead(rxBuf);
                temp2s = tempRead(rxBuf); // Atenção: Confirme se Sensor 2 vem nos bytes 4-7
                temp1f = filterSensorValue1(temp1s.TLtemp);
                temp2f = filterSensorValue2(temp2s.TRtemp);
                timetempmess = millis();
            }
        } 
    }

    //═══════════════════════════════════════════════════════════════════════
    // SISTEMA DE AQUISIÇÃO PERIÓDICA
    //═══════════════════════════════════════════════════════════════════════
    if(((millis() - timeaquisition) >= aquisc.timer) && 
       (aquisc.Aquics_Enable || aquisc.Aquics_Enable_Continuous)){
        
        timeaquisition = millis();
        if (aquisc.Aquics_Enable == 1) aquisc.Aquics_Enable = 0;
        
        for (size_t i = 0; i < 3; i++){ 
            CAN0.sendMsgBuf(remoteIDs[i], 0, NULL);
            delayMicroseconds(100); 
        }
        
        static byte aquisData[2];
        aquisData[0] = (downpipePress * 255) / 20;
        aquisData[1] = (valvPos * 255) / 100;
        CAN0.sendMsgBuf(0x426, 8, aquisData);
    } 

    //═══════════════════════════════════════════════════════════════════════
    // MONITOR DE SEGURANÇA E TIMERS
    //═══════════════════════════════════════════════════════════════════════
    
    // --- MONITOR TEMP 1 ---
    if(temp1c.Monit_Enable == 1){
        if(temp1f >= temp1c.maxtemp){
            if(starttimer1 == 0){
                Serial.println("!!! ALERTA: T1 LIMITE ATINGIDO Acionando D1 !!!");
                digitalWrite(D1, HIGH); // LED ON em alerta
                ITimer5.attachInterruptInterval(temp1c.timer, TimerHandler1);
                previousMillistimer = millis();
                starttimer1 = 1;
            }
        } else {
            if(starttimer1 == 1) {
                Serial.println("INFO: T1 Normalizado D1 Desligado");
                digitalWrite(D1, LOW); // LED ON em alerta
                ITimer5.detachInterrupt();
                starttimer1 = 0;
            }
        }
    } else {
        if(starttimer1) ITimer5.detachInterrupt();
        starttimer1 = 0;
    }

    // --- MONITOR TEMP 2 ---
    if(temp2c.Monit_Enable == 1){
        if(temp2f >= temp2c.maxtemp){
            if(starttimer2 == 0){
                Serial.println("!!! ALERTA: T2 LIMITE ATINGIDO Acionando D2 !!!");
                digitalWrite(D2, HIGH); // LED ON em alerta
                ITimer3.attachInterruptInterval(temp2c.timer, TimerHandler2);
                starttimer2 = 1;
            }
        } else {
            if(starttimer2 == 1) {
                Serial.println("INFO: T2 Normalizado D2 desligado");
                digitalWrite(D2, LOW); // LED OFF
                ITimer3.detachInterrupt();
                starttimer2 = 0;
            }
        }
    } else {
        if(starttimer2) ITimer3.detachInterrupt();
        starttimer2 = 0;
    }

    //═══════════════════════════════════════════════════════════════════════
    // CONTROLE DE MOTOR
    //═══════════════════════════════════════════════════════════════════════
    setMotor(dir, pwmVal);
    
    //═══════════════════════════════════════════════════════════════════════
    // DASHBOARD SERIAL
    //═══════════════════════════════════════════════════════════════════════
    static unsigned long lastDebugPrint = 0;
    if(millis() - lastDebugPrint >= 500) {
        lastDebugPrint = millis();
        
        Serial.print("[STATUS] T1: ");
        Serial.print(temp1f, 1);
        Serial.print("C (Max:");
        Serial.print(temp1c.maxtemp, 0);
        
        Serial.print(") | T2: ");
        Serial.print(temp2f, 1);
        Serial.print("C (Max:");
        Serial.print(temp2c.maxtemp, 0);
        
        Serial.print(") | PWM: ");
        Serial.println(pwmVal);
    }

    // Limpa IDs para próximo ciclo
    rxId = 0;
}