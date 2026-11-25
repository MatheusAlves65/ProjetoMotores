//═══════════════════════════════════════════════════════════════════════════
// INCLUDES E DEFINIÇÕES
//═══════════════════════════════════════════════════════════════════════════
#include <avr/wdt.h>
#include <Arduino.h>
#include <mcp_can.h>
#include <SPI.h>
#include <EEPROM.h>
#include "config.h"

#ifndef LED_BUILTIN
    #define LED_BUILTIN  13
#endif

// SAÍDAS DIGITAIS (8 Relés)
#define D1  39  
#define D2  41  
#define D3  32  // NA2/NF2
#define D4  34  // NA1/NF1
#define D5  36  // Bomba
#define D6  38  
#define D7  40  
#define D8  42  

int ledpins[8] = {D1, D2, D3, D4, D5, D6, D7, D8};

#define CAN0_INT  3
MCP_CAN CAN0(33);

long unsigned int rxId;
unsigned char len;
unsigned char rxBuf[8] = " ";  
byte txBuf[8] = " ";

//═══════════════════════════════════════════════════════════════════════════
// ESTRUTURAS DE DADOS
//═══════════════════════════════════════════════════════════════════════════
safetyConfigStructure temp1c, temp2c, temp3c, temp4c;
aquisitionConfigStructure aquisc; 
tempReadStructure Egt_4;  

uint32_t timetempmess = 0;                   
volatile float temp1f = 0, temp2f = 0, temp3f = 0, temp4f = 0;
volatile float prevtempf1 = 0, prevtempf2 = 0, prevtempf3 = 0, prevtempf4 = 0;  

// Rastreador de Histerese
struct AlarmTracker {
    uint8_t state = 0;          
    uint32_t timerStart = 0;    
};
AlarmTracker trackerT1, trackerT2, trackerT3, trackerT4;

//═══════════════════════════════════════════════════════════════════════════
// ENDEREÇOS DA EEPROM (Agora usa 13 bytes por não ter mais relayMask)
//═══════════════════════════════════════════════════════════════════════════
const int sizeConfig = 13;
const int addrT1 = 0;
const int addrT2 = addrT1 + sizeConfig;
const int addrT3 = addrT2 + sizeConfig;
const int addrT4 = addrT3 + sizeConfig;

void updateEEPROMFloat(int address, float value) {
    byte *p = (byte*)(void*)&value;  
    for (size_t i = 0; i < sizeof(float); i++) EEPROM.update(address + i, p[i]);
}
void updateEEPROMUInt16(int address, uint16_t value) {
    byte *p = (byte*)(void*)&value;
    for (size_t i = 0; i < sizeof(uint16_t); i++) EEPROM.update(address + i, p[i]);
}
void updateEEPROMUInt8(int address, uint8_t value) {
    EEPROM.update(address, value);
}
float readEEPROMFloat(int address) {
    float value; byte *p = (byte*)(void*)&value;
    for (size_t i = 0; i < sizeof(float); i++) p[i] = EEPROM.read(address + i);
    return value;
}
uint16_t readEEPROMUInt16(int address) {
    uint16_t value; byte *p = (byte*)(void*)&value;
    for (size_t i = 0; i < sizeof(uint16_t); i++) p[i] = EEPROM.read(address + i);
    return value;
}
uint8_t readEEPROMUInt8(int address) {
    return EEPROM.read(address);
}

void saveConfig(int baseAddr, const safetyConfigStructure &cfg) {
    updateEEPROMUInt8(baseAddr, cfg.Monit_Enable);
    updateEEPROMFloat(baseAddr + 1, cfg.maxtemp);
    updateEEPROMUInt16(baseAddr + 5, cfg.timer);
    updateEEPROMFloat(baseAddr + 7, cfg.lowerLimit);
    updateEEPROMUInt16(baseAddr + 11, cfg.lowerTimer);
}
void loadConfig(int baseAddr, safetyConfigStructure &cfg) {
    cfg.Monit_Enable = readEEPROMUInt8(baseAddr);
    cfg.maxtemp = readEEPROMFloat(baseAddr + 1);
    cfg.timer = readEEPROMUInt16(baseAddr + 5);
    cfg.lowerLimit = readEEPROMFloat(baseAddr + 7);
    cfg.lowerTimer = readEEPROMUInt16(baseAddr + 11);
}
void loadAllConfigs() {
    loadConfig(addrT1, temp1c); loadConfig(addrT2, temp2c);
    loadConfig(addrT3, temp3c); loadConfig(addrT4, temp4c);
}

//═══════════════════════════════════════════════════════════════════════════
// FILTROS DIGITAIS - EMA 
//═══════════════════════════════════════════════════════════════════════════
float filterSensorValue1(float newValue) { prevtempf1 = 0.1f * newValue + (1 - 0.1f) * prevtempf1; return prevtempf1; }
float filterSensorValue2(float newValue) { prevtempf2 = 0.1f * newValue + (1 - 0.1f) * prevtempf2; return prevtempf2; }
float filterSensorValue3(float newValue) { prevtempf3 = 0.1f * newValue + (1 - 0.1f) * prevtempf3; return prevtempf3; }
float filterSensorValue4(float newValue) { prevtempf4 = 0.1f * newValue + (1 - 0.1f) * prevtempf4; return prevtempf4; }


//═══════════════════════════════════════════════════════════════════════════
// FUNÇÃO DE HISTERESE COM PINOS ESPECÍFICOS E MODO FAIL-SAFE (Erro 1050°C)
//═══════════════════════════════════════════════════════════════════════════
void processTemperatureAlarm(float currentTemp, const safetyConfigStructure &cfg, AlarmTracker &tracker, const char* sensorName, int p1, int p2 = -1, int p3 = -1, int p4 = -1) {
    
    // Se o monitoramento desligou, desliga todos os relés vinculados a este sensor
    if (cfg.Monit_Enable != 1) {
        if (p1 != -1) digitalWrite(p1, HIGH);
        if (p2 != -1) digitalWrite(p2, HIGH);
        if (p3 != -1) digitalWrite(p3, HIGH);
        if (p4 != -1) digitalWrite(p4, HIGH);
        tracker.state = 0;
        return;
    }

    uint32_t currentMillis = millis();

    // ====================================================================
    // FAIL-SAFE: ERRO DE LEITURA (1050°C = Termopar Desconectado/Falha)
    // ====================================================================
    if (currentTemp >= 1050.0) {
        if (tracker.state != 4) {
            Serial.print("!!! ERRO CRITICO: Sensor "); Serial.print(sensorName); Serial.println(" Desconectado! Protecao Ativada !!!");
            
            // Atraca os relés IMEDIATAMENTE (LOW) para proteger o motor
            if (p1 != -1) digitalWrite(p1, HIGH);
            if (p2 != -1) digitalWrite(p2, HIGH);
            if (p3 != -1) digitalWrite(p3, HIGH);
            if (p4 != -1) digitalWrite(p4, HIGH);
            
            tracker.state = 4; // Entra em modo de Falha
        }
        return; // Aborta o resto da lógica e mantém os relés ligados para segurança
    }

    // Se o sensor estava em falha (1050) e voltou a ler uma temperatura normal
    if (tracker.state == 4) {
        Serial.print("INFO: Sensor "); Serial.print(sensorName); Serial.println(" Reconectado. Analisando resfriamento...");
        tracker.state = 2; // Joga para o estado de "Alarme Ativo" para fazer a histerese de descida de forma segura
        tracker.timerStart = currentMillis;
    }
    // ====================================================================

    switch (tracker.state) {
        case 0: // NORMAL
            if (currentTemp >= cfg.maxtemp) {
                tracker.timerStart = currentMillis; 
                tracker.state = 1;                  
            }
            break;

        case 1: // VALIDANDO ALTA 
            if (currentTemp < cfg.maxtemp) {
                tracker.state = 0; 
            } 
            else if ((currentMillis - tracker.timerStart) >= cfg.timer) { 
                Serial.print("!!! ALERTA: Limite atingido no sensor "); Serial.println(sensorName);
                tracker.state = 2; // Passa para o alarme
            }
            break;

        case 2: // ALARME ATIVO 
            // BLINDAGEM: Força o relé atracado contra o comando manual 0x402 
            if (p1 != -1) digitalWrite(p1, LOW);
            if (p2 != -1) digitalWrite(p2, LOW);
            if (p3 != -1) digitalWrite(p3, LOW);
            if (p4 != -1) digitalWrite(p4, LOW);

            if (currentTemp <= cfg.lowerLimit) {
                tracker.timerStart = currentMillis; 
                tracker.state = 3;                  
            }
            break;

        case 3: // VALIDANDO BAIXA
            // Mantém atracado enquanto conta o tempo
            if (p1 != -1) digitalWrite(p1, LOW);
            if (p2 != -1) digitalWrite(p2, LOW);
            if (p3 != -1) digitalWrite(p3, LOW);
            if (p4 != -1) digitalWrite(p4, LOW);

            if (currentTemp > cfg.lowerLimit) {
                tracker.state = 2; 
            } 
            else if ((currentMillis - tracker.timerStart) >= cfg.lowerTimer) { 
                Serial.print("INFO: Sistema normalizado no sensor "); Serial.println(sensorName);
                
                // DESLIGA OS RELÉS (HIGH = Solta o relé)
                if (p1 != -1) digitalWrite(p1, HIGH);
                if (p2 != -1) digitalWrite(p2, HIGH);
                if (p3 != -1) digitalWrite(p3, HIGH);
                if (p4 != -1) digitalWrite(p4, HIGH);
                tracker.state = 0;
            }
            break;
    }
}
//═══════════════════════════════════════════════════════════════════════════
// SETUP
//═══════════════════════════════════════════════════════════════════════════
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    
    for(int i = 0; i < 8; i++) {
        pinMode(ledpins[i], OUTPUT);
        digitalWrite(ledpins[i], HIGH);
    }
    
    pinMode(CAN0_INT, INPUT);
    Serial.begin(115200);
    wdt_disable();

    if(CAN0.begin(MCP_ANY, CAN_1000KBPS, MCP_8MHZ) == CAN_OK){
        Serial.println("MCP2515 Initialized Successfully!");
    } else {
        Serial.println("Error Initializing MCP2515...");
    }
    CAN0.setMode(MCP_NORMAL);
    
    byte tempLen; byte tempBuf[8]; unsigned long tempId;
    for(int i = 0; i < 100; i++) {
        if(CAN0.checkReceive() == CAN_MSGAVAIL) {
            CAN0.readMsgBuf(&tempId, &tempLen, tempBuf);
        } else break;
    }
    
    loadAllConfigs();

    // Se estiver vazio, aplica os limites padrão (agora sem máscara)
    if (isnan(temp1c.maxtemp) || temp1c.maxtemp <= 0) {
        Serial.println("EEPROM Virgem Detectada! Aplicando padroes...");
        
        temp1c.maxtemp = 100.0; temp1c.timer = 1000; temp1c.lowerLimit = 90.0; temp1c.lowerTimer = 1000; temp1c.Monit_Enable = 1;
        temp2c.maxtemp = 100.0; temp2c.timer = 1000; temp2c.lowerLimit = 90.0; temp2c.lowerTimer = 1000; temp2c.Monit_Enable = 1;
        temp3c.maxtemp = 100.0; temp3c.timer = 1000; temp3c.lowerLimit = 90.0; temp3c.lowerTimer = 1000; temp3c.Monit_Enable = 1;
        temp4c.maxtemp = 100.0; temp4c.timer = 1000; temp4c.lowerLimit = 90.0; temp4c.lowerTimer = 1000; temp4c.Monit_Enable = 1;

        saveConfig(addrT1, temp1c);
        saveConfig(addrT2, temp2c);
        saveConfig(addrT3, temp3c);
        saveConfig(addrT4, temp4c);
    }
    Serial.println("SISTEMA DE TEMPERATURAS INICIADO");
}

//═══════════════════════════════════════════════════════════════════════════
// LOOP
//═══════════════════════════════════════════════════════════════════════════
void loop() {
    static unsigned long lastBlink = 0;
    
    if(millis() - lastBlink > 1000) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        lastBlink = millis();
    }
    
    unsigned long currentFullId = 0; 

    if(!digitalRead(CAN0_INT)) {
        if(CAN0.readMsgBuf(&rxId, &len, rxBuf) == CAN_OK) {
            currentFullId = rxId & 0x1FFFFFFF; 
            
            if(currentFullId == 0x0042){
                txBuf[0] = 0xAA; CAN0.sendMsgBuf(0x0042, 1, txBuf);
                delay(100); wdt_enable(WDTO_15MS); while(1) {} 
            }

            // 0x402 - SAÍDAS DIGITAIS MANUAL
            if(currentFullId == 0x402){
                static int result[8];
                readDigital(rxBuf, result);
                for (size_t i = 0; i < 8; i++){
                    if(result[i] == 0) digitalWrite(ledpins[i], LOW);
                    else if(result[i] == 1) digitalWrite(ledpins[i], HIGH);
                }
            }

            // --- 0x403: CONFIGURAÇÃO SEGURANÇA ENGINE (T2) ---
            if(currentFullId == 0x403){
                if(len > 0) {
                    temp2c.Monit_Enable = (rxBuf[1] & 0x03); 
                    temp2c.maxtemp = (float)rxBuf[3]; 
                    temp2c.timer = (uint16_t)rxBuf[4] * 100; 
                    temp2c.lowerLimit = (float)rxBuf[5];
                    temp2c.lowerTimer = (uint16_t)rxBuf[6] * 100;
                    if(temp2c.Monit_Enable != 2) saveConfig(addrT2, temp2c);
                } 
                txBuf[0] = 0x12; txBuf[1] = 0x30 | (temp2c.Monit_Enable & 0x03);
                txBuf[2] = 0; txBuf[3] = (byte)temp2c.maxtemp; txBuf[4] = (byte)(temp2c.timer / 100);
                txBuf[5] = (byte)temp2c.lowerLimit; txBuf[6] = (byte)(temp2c.lowerTimer / 100); txBuf[7] = 0;
                CAN0.sendMsgBuf(0x423, 8, txBuf); 
            }

            // --- 0x404: CONFIGURAÇÃO SEGURANÇA HEAT EXCHANGE (T4) ---
            if(currentFullId == 0x404){
                if(len > 0) {
                    temp4c.Monit_Enable = (rxBuf[1] & 0x03); 
                    temp4c.maxtemp = (float)rxBuf[3]; 
                    temp4c.timer = (uint16_t)rxBuf[4] * 100;
                    temp4c.lowerLimit = (float)rxBuf[5];
                    temp4c.lowerTimer = (uint16_t)rxBuf[6] * 100; 
                    if(temp4c.Monit_Enable != 2) saveConfig(addrT4, temp4c);
                } 
                txBuf[0] = 0x12; txBuf[1] = 0x30 | (temp4c.Monit_Enable & 0x03);
                txBuf[2] = 0; txBuf[3] = (byte)temp4c.maxtemp; txBuf[4] = (byte)(temp4c.timer / 100);
                txBuf[5] = (byte)temp4c.lowerLimit; txBuf[6] = (byte)(temp4c.lowerTimer / 100); txBuf[7] = 0;
                CAN0.sendMsgBuf(0x424, 8, txBuf); 
            }

            // --- 0x405: CONFIGURAÇÃO SEGURANÇA STARTER (T1) ---
            if(currentFullId == 0x405){
                if(len > 0) {
                    temp1c.Monit_Enable = (rxBuf[1] & 0x03); 
                    temp1c.maxtemp = (float)rxBuf[3]; 
                    temp1c.timer = (uint16_t)rxBuf[4] * 100;
                    temp1c.lowerLimit = (float)rxBuf[5];
                    temp1c.lowerTimer = (uint16_t)rxBuf[6] * 100; 
                    if(temp1c.Monit_Enable != 2) saveConfig(addrT1, temp1c);
                } 
                txBuf[0] = 0x12; txBuf[1] = 0x30 | (temp1c.Monit_Enable & 0x03);
                txBuf[2] = 0; txBuf[3] = (byte)temp1c.maxtemp; txBuf[4] = (byte)(temp1c.timer / 100);
                txBuf[5] = (byte)temp1c.lowerLimit; txBuf[6] = (byte)(temp1c.lowerTimer / 100); txBuf[7] = 0;
                CAN0.sendMsgBuf(0x425, 8, txBuf); 
            }

            // --- 0x406: CONFIGURAÇÃO SEGURANÇA INTERCOOLER (T3) ---
            if (currentFullId == 0x406){
                if(len > 0){
                    temp3c.Monit_Enable = (rxBuf[1] & 0x03);
                    temp3c.maxtemp = (float)rxBuf[3]; 
                    temp3c.timer = (uint16_t)rxBuf[4] * 100;
                    temp3c.lowerLimit = (float)rxBuf[5];
                    temp3c.lowerTimer = (uint16_t)rxBuf[6] * 100;
                    if(temp3c.Monit_Enable != 2) saveConfig(addrT3, temp3c);
                }
                txBuf[0] = 0x12; txBuf[1] = 0x30 | (temp3c.Monit_Enable & 0x03);
                txBuf[2] = 0; txBuf[3] = (byte)temp3c.maxtemp; txBuf[4] = (byte)(temp3c.timer / 100);
                txBuf[5] = (byte)temp3c.lowerLimit; txBuf[6] = (byte)(temp3c.lowerTimer / 100); txBuf[7] = 0;
                CAN0.sendMsgBuf(0x426, 8, txBuf);
            }

            // --- 0x02400000: LEITURA DE TEMPERATURA EGT-4 ---
            if(currentFullId == 0x02400000){
                Egt_4 = tempRead(rxBuf);
                temp1f = (Egt_4.Ch1status == 1) ? filterSensorValue1(Egt_4.Channel1) : 1050;    
                temp2f = (Egt_4.Ch2status == 1) ? filterSensorValue2(Egt_4.Channel2) : 1050;
                temp3f = (Egt_4.Ch3status == 1) ? filterSensorValue3(Egt_4.Channel3) : 1050;
                temp4f = (Egt_4.Ch4status == 1) ? filterSensorValue4(Egt_4.Channel4) : 1050;
                timetempmess = millis();
            }
        } 
    }

    //═══════════════════════════════════════════════════════════════════════
    // AQUI ESTÁ A LÓGICA DOS PINOS DIRETO (CONFORME VOCÊ PEDIU)
    //═══════════════════════════════════════════════════════════════════════
    
    // T1 aciona Bomba (D5), NA1 (D4) e NA2 (D3) ao mesmo tempo
    processTemperatureAlarm(temp1f, temp1c, trackerT1, "T1 (Starter)", D5, D4, D3); 
    
    // T2 aciona D2 (apenas um exemplo, mude conforme precisar)
    processTemperatureAlarm(temp2f, temp2c, trackerT2, "T2 (Engine)", D4); 
    
    // T3 aciona D6 (apenas um exemplo)
    processTemperatureAlarm(temp3f, temp3c, trackerT3, "T3 (Inter)", D3); 
    
    // T4 aciona D8 (apenas um exemplo)
    processTemperatureAlarm(temp4f, temp4c, trackerT4, "T4 (Heat)", D5);

    //═══════════════════════════════════════════════════════════════════════
    // DASHBOARD SERIAL
    //═══════════════════════════════════════════════════════════════════════
    static unsigned long lastDebugPrint = 0;
    if(millis() - lastDebugPrint >= 500) {
        lastDebugPrint = millis();
        Serial.print("[STATUS] T1: "); Serial.print(temp1f, 1); Serial.print("C | T2: "); Serial.print(temp2f, 1);
        Serial.print("C | T3: "); Serial.print(temp3f, 1); Serial.print("C | T4: "); Serial.print(temp4f, 1); Serial.println("C");
    }

    rxId = 0;
}