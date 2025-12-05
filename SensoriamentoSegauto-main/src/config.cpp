#include "config.h"

//All of the functions present here have been validated for their use cases
void readDigital(byte *buf, int digitalCommand[8]){
    digitalCommand[3] = buf[0] & 0x03;
    digitalCommand[2] = (buf[0] >> 2) & 0x03;
    digitalCommand[1] = (buf[0] >> 4) & 0x03;
    digitalCommand[0] = buf[0] >> 6;
    digitalCommand[7] = buf[1] & 0x03;
    digitalCommand[6] = (buf[1] >> 2) & 0x03;
    digitalCommand[5] = (buf[1] >> 4) & 0x03;
    digitalCommand[4] = buf[1] >> 6;
}

int readPWMEnc (byte *buf, int num){
    int value = buf[num];
    if (value <= 250){
       value = (value * 255) / 250;
    } else if(value == 255){
        return -2;
    } else if (value >= 251){
        return -1;
    } else {
        value = 255;
    }
    return value;
} 

void sendDigital(int digitalCommand[8], int PWM1, int PWM2, int Enc, byte *txBuf){
    txBuf[0] = (digitalCommand[0] << 6) | (digitalCommand[1] << 4) | (digitalCommand[2] << 2) | (digitalCommand[3]);
    txBuf[1] = (digitalCommand[4] << 6) | (digitalCommand[5] << 4) | (digitalCommand[6] << 2) | (digitalCommand[7]); 
    txBuf[2] = PWM1;
    txBuf[3] = PWM2;
    txBuf[4] = Enc; 
    txBuf[5] = 0; 
    txBuf[6] = 0; 
    txBuf[7] = 0; 
}

safetyConfigStructure safetyConfig (byte *buf){
    safetyConfigStructure tempc;
    tempc.saveeeprom = 0;
    uint16_t checkId = ((uint16_t)buf[0] << 4 | (buf[1] >> 4));
    if(checkId != TempFrameId){
        if(checkId == saveEepromId){
            tempc.saveeeprom = 1;
        } else {
            tempc.Monit_Enable = 2;
            return tempc;
        }
    } 
    tempc.Monit_Enable = (buf[1] & 0x03);
    tempc.maxtemp = (buf[2]*120)/255;
    tempc.timer = (uint16_t)(((float)buf[3] * 10000.0f) / 255.0f);
    return tempc;
}

void sendsafetyConfig (safetyConfigStructure tempc, byte *txBuf){
    txBuf[0] = 0x12;
    txBuf[1] = 0x30 | tempc.Monit_Enable;
    txBuf[2] = (tempc.maxtemp*255)/120;
    txBuf[3] = (uint8_t)((((float)tempc.timer) * 255.0f) / 10000.0f);
}

aquisitionConfigStructure aquisitionConfig (byte *buf){
    aquisitionConfigStructure aquisc;
    Serial.println("Parsing aquisitionConfigStructure from buffer:");
    for(int i=0; i<8; i++) {
        Serial.print(buf[i], HEX);
        Serial.print(" ");
    }
    aquisc.timer = (buf[0] << 8) | buf[1];
    aquisc.analog = (buf[2] & 0x01);
    aquisc.Aquics_Enable_Continuous = (buf[3] & 0x03);
    return aquisc;
}

void sendaquisitionConfig(aquisitionConfigStructure aquisc, byte *txBuf){
    txBuf[0] = (aquisc.timer >> 8) & 0xFF;  
    txBuf[1] = aquisc.timer & 0xFF;         
    txBuf[2] = aquisc.analog & 0x01;
    txBuf[3] = aquisc.Aquics_Enable_Continuous & 0x03;
    txBuf[4] = 0x00;
    txBuf[5] = 0x00;
    txBuf[6] = 0x00;
    txBuf[7] = 0x00;
}

tempReadStructure tempRead(byte *buf){
    tempReadStructure temp;
    temp.CJtemp = buf[0] - 128;
    temp.TLstatus = (buf[1] & 0x03);
    temp.TLtemp = (((buf[2] & 0x3f) << 6) | (buf[1] >> 2)) - 2048;
    temp.TRstatus = (buf[2] >> 6);
    temp.TRtemp = (((buf[4] & 0x0F) << 8) | buf[3]) - 2048;
    temp.BLstatus = ((buf[4] >> 4) & 0x03);
    temp.BLtemp = (((buf[6] & 0x03) << 10) | (buf[5] << 2) | (buf[4] >> 6)) - 2048;
    temp.BRstatus = ((buf[6] >> 2) & 0x03);
    temp.BRtemp = ((buf[7] << 4) | (buf[6] >> 4)) - 2048;
    return temp;
}