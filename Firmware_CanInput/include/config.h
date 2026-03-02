//////////////////////////////////////////////////////////////////////////////////
// Universidade de Brasilia (UnB)
// Faculdade do Gama
// Projeto: SegurAuto
// Biblioteca para comunicar com o radar ARS404 V1
// DUFOUR Amelie
//////////////////////////////////////////////////////////////////////////////////

// /!\tem a explicacao das funcao na documentacao sobre a biblioteca "AR404"
#ifndef config_H
#define config_H

#include <SPI.h>
#include "mcp_can.h"

struct safetyConfigStructure {
	uint8_t saveeeprom = 0;
	uint8_t Monit_Enable = 0;
	float	maxtemp = 0;
	uint16_t timer = 1000; // Milliseconds
};

struct aquisitionConfigStructure {
	uint8_t Aquics_Enable_Continuous, Aquics_Enable = 0;
	boolean	analog = 0;
	uint16_t timer = 1000; // Milliseconds
};

struct tempReadStructure {
	int8_t CJtemp = 25;
	int8_t TLstatus, TRstatus, BLstatus, BRstatus = 0;
	int16_t TLtemp, TRtemp, BLtemp, BRtemp = 0;
};

#define TempFrameId 0x123
#define saveEepromId 0x120

// para usar o tipo "byte" do arduino
typedef unsigned char byte;

void readDigital(byte *buf, int digitalCommand[8]);

int readPWMEnc (byte *buf, int num);

void sendDigital(int digitalCommand[8], int PWM1, int PWM2, int Enc, byte *txBuf);

safetyConfigStructure safetyConfig (byte *buf);

void sendsafetyConfig (safetyConfigStructure tempc, byte *txBuf);

aquisitionConfigStructure aquisitionConfig(byte *buf);

void sendaquisitionConfig(aquisitionConfigStructure aquisc, byte *txBuf);

tempReadStructure tempRead(byte *buf);

#endif