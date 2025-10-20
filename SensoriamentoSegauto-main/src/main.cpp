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
    0x510, 0x520, 0x530,  // Temperaturas? (não documentado completamente)
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
unsigned char rxBuf[8] = " ";     // Buffer de dados recebidos (max 8 bytes)

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
}

//═══════════════════════════════════════════════════════════════════════════
// LOOP() - CICLO PRINCIPAL DO PROGRAMA
//═══════════════════════════════════════════════════════════════════════════
// Este loop executa continuamente após setup()
// Processa mensagens CAN, monitora temperatura, controla aquisição, etc.
//═══════════════════════════════════════════════════════════════════════════

void loop()
{

	// LED pisca = loop está rodando
    static unsigned long lastBlink = 0;
    if(millis() - lastBlink > 1000) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        Serial.println("Loop rodando...");
        lastBlink = millis();
    }
    
    // ═══════════════════════════════════════════════════════════
    // DEBUG: Verifica pino INT
    // ═══════════════════════════════════════════════════════════
    static bool lastIntState = HIGH;
    bool currentIntState = digitalRead(CAN0_INT);
    
    if(currentIntState != lastIntState) {
        Serial.print("INT mudou para: ");
        Serial.println(currentIntState ? "HIGH" : "LOW");
        lastIntState = currentIntState;
    }
    
    // ═══════════════════════════════════════════════════════════
    // Verifica se tem mensagem
    // ═══════════════════════════════════════════════════════════
    if(!digitalRead(CAN0_INT)) {
        Serial.println("INT = LOW, lendo mensagem...");
        
        // Lê mensagem
        if(CAN0.readMsgBuf(&rxId, &len, rxBuf) == CAN_OK) {
            Serial.print("RX: 0x");
            Serial.print(rxId, HEX);
            Serial.print(" [");
            Serial.print(len);
            Serial.println("]");
            
            // Responde ao heartbeat
            if(rxId == 0x401) {
                Serial.println("  -> HEARTBEAT! Respondendo...");
                
                byte result = CAN0.sendMsgBuf(0x401, 0, 0);
                
                if(result == CAN_OK) {
                    Serial.println("  -> Resposta enviada OK!");
                } else {
                    Serial.print("  -> ERRO ao enviar! Código: ");
                    Serial.println(result);
                }
                
                // Pisca 3x rápido quando responde
                for(int i=0; i<6; i++) {
                    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
                    delay(100);
                }
            }
            
            // Reset
            if(rxId == 0x243) {
                Serial.println("  -> RESET!");
                delay(100);
                asm volatile ("jmp 0");
            }
        } else {
            Serial.println("ERRO ao ler buffer CAN!");
        }
    }
        
	
	//═══════════════════════════════════════════════════════════════════════
	// MENSAGEM 0x402 - CONTROLE DE SAÍDAS DIGITAIS E PWM
	//═══════════════════════════════════════════════════════════════════════
	// FORMATO DA MENSAGEM (8 bytes):
	//   Byte 0: [D1 D1 D2 D2 D3 D3 D4 D4]  - 2 bits por saída digital
	//   Byte 1: [D5 D5 D6 D6 D7 D7 D8 D8]  - 2 bits por saída digital
	//   Byte 2: [PWM1]                      - 0-100 ou 251-255 para erro
	//   Byte 3: [PWM2]                      - 0-100 ou 251-255 para erro
	//   Byte 4: [Encoder]                   - 0-100 ou 251-255 para erro
	//
	// CODIFICAÇÃO DOS 2 BITS:
	//   00 = Desativada
	//   01 = Ativada
	//   10 = Erro
	//   11 = Don't Care (não fazer nada)
	//───────────────────────────────────────────────────────────────────────
	if(rxId == 0x402){
		static int result[8];  // Array para armazenar estado das 8 saídas
		
		// Decodifica os bytes recebidos em estados (0/1/2/3)
		// Função definida em config.h
		readDigital(rxBuf, result);
		
		// Atualiza cada saída digital baseado no comando recebido
		for (size_t i = 0; i < 8; i++){
			switch (result[i]){
			case 0:
				digitalWrite(ledpins[i], 0);  // Desliga relé
				break;
			case 1:
				digitalWrite(ledpins[i], 1);  // Liga relé
				break;
			case 2:
				Serial.println("Error LEDs");  // Indica erro
				break;	
			case 3:
				// Don't care - não faz nada
				break;
			default:
				break;
			}
		}
		
		//───────────────────────────────────────────────────────────────────
		// PROCESSA PWM1
		//───────────────────────────────────────────────────────────────────
		// Byte 2 contém valor de PWM1 (0-100% ou código de erro)
		Temp = readPWMEnc(rxBuf, 2);  // Extrai PWM1 do byte 2
		if(Temp < 0){
			// Valor negativo indica erro
			if (Temp == -1){
				Serial.print("Error PWM 1");
			}
		} else {
			// Valor válido (0-100), mapeia para 0-255
			PWM1_val = Temp;
		}
		
		//───────────────────────────────────────────────────────────────────
		// PROCESSA PWM2
		//───────────────────────────────────────────────────────────────────
		Temp = readPWMEnc(rxBuf, 3);  // Extrai PWM2 do byte 3
		if(Temp < 0){
			if (Temp == -1){
				Serial.print("Error PWM 2");
			}
		} else {
			PWM2_val = Temp;
		}
		
		//───────────────────────────────────────────────────────────────────
		// PROCESSA ENCODER
		//───────────────────────────────────────────────────────────────────
		// ⚠️ NOTA: Implementação incorreta do encoder
		//    Está apenas lendo um valor, não contando pulsos reais
		//    Para encoder real, usar interrupções e contador
		Temp = readPWMEnc(rxBuf, 4);  // Extrai Encoder do byte 4
		if(Temp < 0){
			if (Temp == -1){
				Serial.print("Error Encoder");
			}
		} else {
			Enc = Temp;  // Armazena valor (implementação simplificada)
		}
		
		//───────────────────────────────────────────────────────────────────
		// APLICA OS VALORES PWM NAS SAÍDAS
		//───────────────────────────────────────────────────────────────────
		// PWM1_val e PWM2_val já estão mapeados (0-255)
		analogWrite(PWM1, PWM1_val);  // Pino 44
		analogWrite(PWM2, PWM2_val);  // Pino 46

		//───────────────────────────────────────────────────────────────────
		// ENVIA RESPOSTA 0x422 (Confirmação do status)
		//───────────────────────────────────────────────────────────────────
		// Envia de volta o estado atual de tudo
		sendDigital(result, PWM1_val, PWM2_val, Enc, txBuf);
		CAN0.sendMsgBuf(0x422, sizeof(txBuf), txBuf);
	}

	//═══════════════════════════════════════════════════════════════════════
	// MENSAGEM 0x403 - CONFIGURAÇÃO DE SEGURANÇA (Limites de Temperatura)
	//═══════════════════════════════════════════════════════════════════════
	// FORMATO DA MENSAGEM (8 bytes):
	//   Bytes 0-3: Configuração do sensor 1
	//     b.01: [Frame ID parte 1]
	//     b.02: [Frame ID parte 2 + Enable bit]
	//     b.03: [Threshold 0-120°C]
	//     b.04: [Elapsed time 0-10s]
	//   Bytes 4-7: Configuração do sensor 2 (mesmo formato)
	//───────────────────────────────────────────────────────────────────────
	if(rxId == 0x403){
		//───────────────────────────────────────────────────────────────────
		// PROCESSA CONFIGURAÇÃO DO SENSOR 1 (primeiros 4 bytes)
		//───────────────────────────────────────────────────────────────────
		tconfigbuf = safetyConfig(rxBuf);  // Função em config.h
		
		// Valida o valor de Monit_Enable
		switch (tconfigbuf.Monit_Enable){
			case 0:
				temp1c = tconfigbuf;  // Desabilitado
				break;
			case 1:
				temp1c = tconfigbuf;  // Habilitado
				break;
			case 2:
				Serial.print("Error 0x403");  // Erro na configuração
				break;	
			default:
				break;
		}
		
		// Se flag saveeeprom está setada, salva na EEPROM
		if(temp1c.saveeeprom == 1){
			updateSafetyConfig1(temp1c);
		}
		
		//───────────────────────────────────────────────────────────────────
		// PROCESSA CONFIGURAÇÃO DO SENSOR 2 (últimos 4 bytes)
		//───────────────────────────────────────────────────────────────────
		tconfigbuf = safetyConfig(rxBuf+4);  // Pula 4 bytes (rxBuf+4)
		
		switch (tconfigbuf.Monit_Enable){
			case 0:
				temp2c = tconfigbuf;
				break;
			case 1:
				temp2c = tconfigbuf;
				break;
			case 2:
				Serial.print("Error 0x403 +4");
				break;	
			default:
				break;
		}
		
		if(temp2c.saveeeprom == 1){
			updateSafetyConfig2(temp2c);
		}
		
		//───────────────────────────────────────────────────────────────────
		// ENVIA RESPOSTA 0x423 (Confirmação das configurações)
		//───────────────────────────────────────────────────────────────────
		sendsafetyConfig(temp1c, txBuf);      // Primeiros 4 bytes
		sendsafetyConfig(temp2c, txBuf+4);    // Últimos 4 bytes
		
		CAN0.sendMsgBuf(0x423, sizeof(txBuf), txBuf);
	}

	//═══════════════════════════════════════════════════════════════════════
	// MENSAGEM 0x404 - CONFIGURAÇÃO DA TAXA DE AQUISIÇÃO
	//═══════════════════════════════════════════════════════════════════════
	// FORMATO DA MENSAGEM:
	//   b.01-02: Taxa de aquisição (10-3000ms) em uint16_t
	//   b.03: Habilita leitura analógica
	//   b.04: Aquisição contínua habilitada
	//───────────────────────────────────────────────────────────────────────
	if(rxId == 0x404){
		// Decodifica a mensagem recebida
		aconfigbuf = aquisitionConfig(rxBuf);  // Função em config.h
		
		// Valida o valor de Aquics_Enable
		switch (aconfigbuf.Aquics_Enable){
			case 0:
				aquisc = aconfigbuf;  // Desabilitado
				break;
			case 1:
				aquisc = aconfigbuf;  // Habilitado
				break;
			case 2:
				Serial.print("Error 0x404");  // Erro
				break;	
			default:
				break;
		}
		
		// Envia resposta 0x424 (Confirmação)
		sendaquisitionConfig(aquisc, txBuf);
		CAN0.sendMsgBuf(0x424, sizeof(txBuf), txBuf);
	}

	//═══════════════════════════════════════════════════════════════════════
	// MENSAGEM 0x405 - START/STOP AQUISIÇÃO
	//═══════════════════════════════════════════════════════════════════════
	// FORMATO: 1 byte
	//   b1.2 (bits 6-7): 00=stop, 01=start, 10=erro, 11=don't care
	//───────────────────────────────────────────────────────────────────────
	if(rxId == 0x405){
		static uint8_t EnableBuf = 0;
		EnableBuf = (rxBuf[0] >> 6);  // Extrai bits 6-7
		
		// Valida o comando
		switch (EnableBuf){
			case 0:
				aquisc.Aquics_Enable = EnableBuf;  // Para aquisição
				break;
			case 1:
				aquisc.Aquics_Enable = EnableBuf;  // Inicia aquisição
				break;
			case 2:
				Serial.print("Error 0x405");
				break;	
			default:
				break;
		}
		
		// Prepara resposta 0x425
		static byte EnableBufc[1] = {0x00};
		EnableBufc[0] = ((aquisc.Aquics_Enable << 6) & 0xC0);
		CAN0.sendMsgBuf(0x425, 8, EnableBufc);	
	}

	//═══════════════════════════════════════════════════════════════════════
	// MENSAGEM 0x510 - RECEPÇÃO DE DADOS DE TEMPERATURA
	//═══════════════════════════════════════════════════════════════════════
	// Esta mensagem vem de um módulo externo de termopar via CAN
	// ⚠️ NOTA: Apenas temp1s está implementado. temp2s e temp3s não são usados
	//───────────────────────────────────────────────────────────────────────
	if (rxId == 0x510){
		// Decodifica a mensagem de temperatura
		temp1s = tempRead(rxBuf);  // Função em config.h
		
		// Código comentado: verificação de status
		//if(temp1s.BLstatus == 1){
			
			// Aplica filtro para suavizar leitura
			// BLtemp = Bare Lead Temperature (temperatura do termopar)
			// CJtemp = Cold Junction Temperature (compensação de junção fria)
			// A subtração está comentada, mas seria: BLtemp - CJtemp
			temp1f = filterSensorValue1(temp1s.BLtemp /*- temp1s.CJtemp*/);
			
			// Debug: mostra temperatura filtrada
			Serial.print("Temperatura 1 Filtrada: ");
			Serial.println(temp1f);
			
			// Atualiza timestamp da última mensagem
			timetempmess = millis();
		//}
	}

	//═══════════════════════════════════════════════════════════════════════
	// BLOCO 3: SISTEMA DE AQUISIÇÃO PERIÓDICA
	//═══════════════════════════════════════════════════════════════════════
	// FUNCIONAMENTO:
	//   1. Verifica se passou o tempo configurado (aquisc.timer)
	//   2. Verifica se está habilitado (Enable ou Enable_Continuous)
	//   3. Envia 8 remote frames solicitando dados
	//   4. Lê as respostas
	//   5. Envia dados analógicos (pressão + posição válvula)
	//───────────────────────────────────────────────────────────────────────
	if(((millis() - timeaquisition) >= aquisc.timer) && 
	   (aquisc.Aquics_Enable || aquisc.Aquics_Enable_Continuous)){
		
		// Se não for modo contínuo, desabilita após uma aquisição
		aquisc.Aquics_Enable = 0;
		
		//───────────────────────────────────────────────────────────────────
		// SOLICITA DADOS DOS 8 MÓDULOS EXTERNOS
		//───────────────────────────────────────────────────────────────────
		for (size_t i = 0; i < 8; i++){
			// Envia Remote Frame (solicita dados sem enviar payload)
			CAN0.sendMsgBuf(remoteIDs[i], 0, NULL);
			
			// Aguarda resposta
			// 3µs = muito rápido para debug
			// Use 1000µs (1ms) se quiser ver no Raspberry Pi
			delayMicroseconds(3);
			
			// Lê resposta
			CAN0.readMsgBuf(&rxId, &len, rxBuf);
			
			//───────────────────────────────────────────────────────────────
			// CÓDIGO COMENTADO: Processamento específico por ID
			//───────────────────────────────────────────────────────────────
			// TODO: Descomentar e implementar quando necessário
			//switch (i){
			// case 0:  // 0x510 - Temperatura 1
			//	if (rxId == DataIDs[0]){
			//		temp1s = tempRead(rxBuf);
			//		if(temp1s.BLstatus == 1){
			//			temp1f = filterSensorValue(temp1s.BLtemp - temp1s.CJtemp);
			//			Serial.print("Temperatura 1 Filtrada: ");
			//			Serial.println(temp1f);
			//			timetempmess = millis();
			//		}
			//	}
			//	break;
			// case 1:  // 0x520 - Temperatura 2
			//	if (rxId == DataIDs[1]){
			//		temp2s = tempRead(rxBuf);
			//		if(temp2s.BLstatus == 1){
			//			temp2f = filterSensorValue2(temp2s.BLtemp - temp2s.CJtemp);
			//			Serial.print("Temperatura 2 Filtrada: ");
			//			Serial.println(temp2f);
			//			timetempmess = millis();
			//		}
			//	}
			//	break;
			// case 2:  // 0x530 - Temperatura 3
			//	if (rxId == DataIDs[2]){
			//		temp3s = tempRead(rxBuf);
			//		if(temp3s.BLstatus == 1){
			//			temp3f = filterSensorValue(temp3s.BLtemp - temp3s.CJtemp);
			//			Serial.print("Temperatura 3 Filtrada: ");
			//			Serial.println(temp3f);
			//			timetempmess = millis();
			//		}
			//	}
			//	break;
			// default:
			//	break;
			//}
		}
		
		//───────────────────────────────────────────────────────────────────
		// ENVIA DADOS ANALÓGICOS (0x426)
		//───────────────────────────────────────────────────────────────────
		// ⚠️ PROBLEMA: downpipePress e valvPos sempre são 0!
		//    É necessário ler os sensores via ADC
		//
		// FORMATO:
		//   Byte 0: Pressão (0-20 bar) mapeada para 0-255
		//   Byte 1: Posição válvula (0-100%) mapeada para 0-255
		//───────────────────────────────────────────────────────────────────
		static byte aquisData[2] = {0x00, 0x00};
		aquisData[0] = (downpipePress * 255) / 20;   // 0-20 bar → 0-255
		aquisData[1] = (valvPos * 255) / 100;        // 0-100% → 0-255
		CAN0.sendMsgBuf(0x426, 8, aquisData);
		
		// Atualiza timestamp da última aquisição
		timeaquisition = millis();
	} 

	//═══════════════════════════════════════════════════════════════════════
	// CÓDIGO COMENTADO: TESTE MANUAL DE TEMPERATURA
	//═══════════════════════════════════════════════════════════════════════
	// Usado para testar o sistema de segurança sem sensores reais
	// Envie mensagem 0x300 com 2 bytes: [temp1, temp2]
	//───────────────────────────────────────────────────────────────────────
	//if(rxId == 0x300){
	//	temp1 = rxBuf[0];
	//	temp2 = rxBuf[1];
	//	temp1f = filterSensorValue1(temp1);
	//	temp2f = filterSensorValue2(temp2);
	//	timetempmess = millis();
	//}

	//═══════════════════════════════════════════════════════════════════════
	// BLOCO 4: MONITORAMENTO DE SEGURANÇA POR TEMPERATURA
	//═══════════════════════════════════════════════════════════════════════
	// LÓGICA:
	//   1. Se monitoramento habilitado (Monit_Enable == 1)
	//   2. E temperatura >= temperatura máxima
	//   3. Inicia timer de segurança
	//   4. Após tempo configurado (temp1c.timer), chama TimerHandler1()
	//   5. Se temperatura voltar ao normal, cancela o timer
	//───────────────────────────────────────────────────────────────────────
	
	//───────────────────────────────────────────────────────────────────────
	// MONITOR DE TEMPERATURA 1
	//───────────────────────────────────────────────────────────────────────
	if(temp1c.Monit_Enable == 1){
		if(temp1f >= temp1c.maxtemp){
			// Temperatura está ACIMA do limite!
			if(starttimer1 == 0){
				// Timer ainda não foi iniciado, inicia agora
				ITimer5.attachInterruptInterval(temp1c.timer, TimerHandler1);
				previousMillistimer = millis();
				starttimer1 = 1;
				
				// ⚠️ ATENÇÃO: Após temp1c.timer milissegundos,
				//    TimerHandler1() será chamado!
			}
		} else {
			// Temperatura voltou ao normal
			ITimer5.detachInterrupt();  // Cancela o timer
			starttimer1 = 0;
		}
	} else {
		// Monitoramento desabilitado
		
		ITimer5.detachInterrupt();
		starttimer1 = 0;
	}

	//───────────────────────────────────────────────────────────────────────
	// MONITOR DE TEMPERATURA 2 (idêntico ao 1)
	//───────────────────────────────────────────────────────────────────────
	if(temp2c.Monit_Enable == 1){
		if(temp2f >= temp2c.maxtemp){
			if(starttimer2 == 0){
				ITimer3.attachInterruptInterval(temp2c.timer, TimerHandler2);
				starttimer2 = 1;
			}
		} else {
			ITimer3.detachInterrupt();
			starttimer2 = 0;
		}
	} else {
		ITimer3.detachInterrupt();
		starttimer2 = 0;
	}

	//═══════════════════════════════════════════════════════════════════════
	// BLOCO 5: ATUALIZAÇÃO DO CONTROLE DE MOTOR
	//═══════════════════════════════════════════════════════════════════════
	// ⚠️ PROBLEMA CRÍTICO: dir e pwmVal são sempre 0!
	//    O motor NUNCA se move porque estas variáveis não são alteradas
	//
	// TODO: Conectar ao protocolo CAN ou implementar lógica de controle
	//───────────────────────────────────────────────────────────────────────
	setMotor(dir, pwmVal);  // dir=0,
	
	//═══════════════════════════════════════════════════════════════════════
	// BLOCO 6: RESET DA VARIÁVEL rxId
	//═══════════════════════════════════════════════════════════════════════
	// Limpa o ID da mensagem para evitar processar a mesma mensagem duas vezes
	// Isso garante que na próxima iteração do loop, só processamos mensagens
	// novas que chegaram via CAN
	//───────────────────────────────────────────────────────────────────────
	rxId = 0;
}

//═══════════════════════════════════════════════════════════════════════════
// FIM DO CÓDIGO PRINCIPAL
//═══════════════════════════════════════════════════════════════════════════