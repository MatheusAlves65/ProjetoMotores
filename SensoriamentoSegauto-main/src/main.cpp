// These define's must be placed at the beginning before #include "megaAVR_TimerInterrupt.h"
// _TIMERINTERRUPT_LOGLEVEL_ from 0 to 4
// Don't define _TIMERINTERRUPT_LOGLEVEL_ > 0. Only for special ISR debugging only. Can hang the system.
#define TIMER_INTERRUPT_DEBUG         1
#define _TIMERINTERRUPT_LOGLEVEL_     1

//The MCU's utilized for this project have had their normal bootloader's removed
//They are not capable of receiving usb uploads and must be updated via CAN
//For that purpose utilize mcp-can-boot-flash-app with the command below
//mcp-can-boot-flash-app -f your_file.hex -p m2560 -m 0x0042

// Select USING_16MHZ     == true for  16MHz to Timer TCBx => shorter timer, but better accuracy
// Select USING_8MHZ      == true for   8MHz to Timer TCBx => shorter timer, but better accuracy
// Select USING_250KHZ    == true for 250KHz to Timer TCBx => longer timer,  but worse accuracy
// Not select for default 250KHz to Timer TCBx => longer timer,  but worse accuracy
#define USING_16MHZ     true
#define USING_8MHZ      false
#define USING_250KHZ    false


//All other timers outside of 3 and 5 have been identified as beng utilized by other processess
#define USE_TIMER_0     false
#define USE_TIMER_1     false
#define USE_TIMER_2     false
#define USE_TIMER_3     true
#define USE_TIMER_4     false
#define USE_TIMER_5     true

#include "TimerInterrupt_Generic.h"
#include <avr/wdt.h>
#include "ISR_Timer_Generic.h"
#include <Arduino.h>
#include <mcp_can.h>
#include <SPI.h>
#include <EEPROM.h>
#include "config.h"

//ISR_Timer ISR_Timer5;

#ifndef LED_BUILTIN
	#define LED_BUILTIN       13
#endif

#define D1       39
#define D2       41
#define D3       32
#define D4       34
#define D5       36
#define D6       38
#define D7       40
#define D8       42
int ledpins[8] = {D1,D2,D3,D4,D5,D6,D7,D8};


//Declaring the Ids utilized to poll system information
unsigned long DataIDs[8] = {0x510, 0x520, 0x530, 0x610, 0x611, 0x620, 0x621, 0x622};
unsigned long remoteIDs[8] = {0x510|0x40000000, 0x520|0x40000000, 0x530|0x40000000, 0x610|0x40000000, 0x611|0x40000000, 0x620|0x40000000, 0x621|0x40000000, 0x622|0x40000000};
//uint8_t remoteIDsPointer = 0;


#define CAN0_INT  3             // Pino INT para MCP2515
MCP_CAN CAN0(33);  				// Pino CS CAN 33
long unsigned int rxId,rxIdData[8];
unsigned char len;
unsigned char rxBuf[8] = " ";
byte txBufDebug[8] = {0x55, 0x55, 0x55, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
byte txBuf[8] = " ";
char serialString[128]; 


//This code only accomodates up to two different temperature inputs
safetyConfigStructure temp1c, temp2c, tconfigbuf;

tempReadStructure temp1s, temp2s, temp3s;

aquisitionConfigStructure aquisc, aconfigbuf;

//Curently not intregated
uint8_t downpipePress,valvPos = 0;


#define F_PWM_1 6  
#define R_PWM_1 8   

#define F_PWM_2 16  
#define R_PWM_2 37 

#define PWM1 44
#define PWM2 46

#define MOTOR1_EN 16
#define MOTOR2_EN 17

#define LED_TOGGLE_INTERVAL_MS        1000L

//Enc currently not intregated
int dir = 0;
int pwmVal,PWM1_val,PWM2_val,Enc,Temp = 0;

// You have to use longer time here if having problem because Arduino AVR clock is low, 16MHz => lower accuracy.
// Tested OK with 1ms when not much load => higher accuracy.

volatile uint32_t previousMillistimer,previousMillistimer2 = 0;

uint32_t timetempmess,timeaquisition = 0;

bool starttimer1,starttimer2 = 0;

volatile int16_t temp1 = 120;
volatile int16_t temp2 = 120;
volatile float temp1f = 0;
volatile float temp2f = 0;
volatile float temp3f = 0;
volatile float prevtempf1,prevtempf2 = 0;

//Functions to load and save to the EEprom
///////////////////////////////////////////////////////////////////////////////////
int addrtemp1max = 0;
int addrtemp2max = sizeof(float);
int addrtimer1 = sizeof(float) + sizeof(float);
int addrtimer2 = sizeof(float) + sizeof(float) + sizeof(uint16_t);
int monitenable1 = sizeof(float) + sizeof(float) + sizeof(uint16_t) + sizeof(uint16_t);
int monitenable2 = sizeof(float) + sizeof(float) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint8_t);

// Internal helpers 
void updateEEPROMFloat(int address, float value) {
    byte *p = (byte*)(void*)&value;
    for (int i = 0; i < sizeof(float); i++) {
        EEPROM.update(address + i, p[i]);
    }
}

void updateEEPROMUInt16(int address, uint16_t value) {
    byte *p = (byte*)(void*)&value;
    for (int i = 0; i < sizeof(uint16_t); i++) {
        EEPROM.update(address + i, p[i]);
    }
}

void updateEEPROMUInt8(int address, uint8_t value) {
    EEPROM.update(address, value);
}

float readEEPROMFloat(int address) {
    float value;
    byte *p = (byte*)(void*)&value;
    for (int i = 0; i < sizeof(float); i++) {
        p[i] = EEPROM.read(address + i);
    }
    return value;
}

uint16_t readEEPROMUInt16(int address) {
    uint16_t value;
    byte *p = (byte*)(void*)&value;
    for (int i = 0; i < sizeof(uint16_t); i++) {
        p[i] = EEPROM.read(address + i);
    }
    return value;
}

uint8_t readEEPROMUInt8(int address) {
    return EEPROM.read(address);
}

// --- Save functions ---
void updateSafetyConfig1(const safetyConfigStructure &temp1c) {
    updateEEPROMFloat(addrtemp1max, temp1c.maxtemp);
    updateEEPROMUInt16(addrtimer1, temp1c.timer);
    updateEEPROMUInt8(monitenable1, temp1c.Monit_Enable);
}

void updateSafetyConfig2(const safetyConfigStructure &temp2c) {
    updateEEPROMFloat(addrtemp2max, temp2c.maxtemp);
    updateEEPROMUInt16(addrtimer2, temp2c.timer);
    updateEEPROMUInt8(monitenable2, temp2c.Monit_Enable);
}

// --- Load function ---
void loadSafetyConfigs(safetyConfigStructure &temp1c, safetyConfigStructure &temp2c) {
    temp1c.maxtemp       = readEEPROMFloat(addrtemp1max);
    temp1c.timer         = readEEPROMUInt16(addrtimer1);
    temp1c.Monit_Enable  = readEEPROMUInt8(monitenable1);

    temp2c.maxtemp       = readEEPROMFloat(addrtemp2max);
    temp2c.timer         = readEEPROMUInt16(addrtimer2);
    temp2c.Monit_Enable  = readEEPROMUInt8(monitenable2);
}
////////////////////////////////////////////////////////////////////////////


//To implement, once thee funtions are called they must deactivate the motor
void TimerHandler1()
{
	Serial.print("ITimer1 called, millis() = ");
	Serial.println(millis()-previousMillistimer);
	previousMillistimer = millis();
}

void TimerHandler2()
{
	//Serial.print("ITimer2 called, millis() = ");
	//Serial.println(millis()-previousMillistimer2);
	previousMillistimer2 = millis();
}

/////////////////////////////////////////////////


void setMotor(int dir, int pwmVal) {
        if (dir == 1) {
            // Gira num sentido
            analogWrite(R_PWM_2, pwmVal);
            analogWrite(F_PWM_2, 0);
            analogWrite(R_PWM_1, pwmVal);
            analogWrite(F_PWM_1, 0);
        } else if (dir == 2) {
            // Gira em outro sentido
            analogWrite(F_PWM_2, pwmVal);
            analogWrite(R_PWM_2, 0);
            analogWrite(F_PWM_1, pwmVal);
            analogWrite(R_PWM_1, 0);
        } else if (dir == 0) {
            // Desliga os motores
            analogWrite(R_PWM_2, 0);
            analogWrite(F_PWM_2, 0);
            analogWrite(R_PWM_1, 0);
            analogWrite(F_PWM_1, 0);
        }
}

float filterSensorValue1(int16_t newValue) {
    static float filteredValue1 = 25;       // Initialize the filter value
    const float alpha1 = 0.1f;             // Smoothing factor [0.0 - 1.0]
    filteredValue1 = alpha1 * newValue + (1 - alpha1) * prevtempf1;
	prevtempf1 = filteredValue1;
    return filteredValue1;
}

float filterSensorValue2(int16_t newValue) {
    static float filteredValue2 = 25;       // Initialize filtered value
    const float alpha2 = 0.1f;             // Smoothing factor [0.0 - 1.0]
    filteredValue2 = alpha2 * newValue + (1 - alpha2) * prevtempf2;
	prevtempf2 = filteredValue2;
    return filteredValue2;
}

////////////////////////////////////////////////

void setup()
{
	pinMode(LED_BUILTIN, OUTPUT);
	pinMode(D1, OUTPUT);
	pinMode(D2, OUTPUT);
	pinMode(D3, OUTPUT);
	pinMode(D4, OUTPUT);
	pinMode(D5, OUTPUT);
	pinMode(D6, OUTPUT);
	pinMode(D7, OUTPUT);
	pinMode(D8, OUTPUT);
	pinMode(CAN0_INT, INPUT);

	pinMode(PWM1, OUTPUT);
	pinMode(PWM2, OUTPUT);
	pinMode(R_PWM_1, OUTPUT);
    pinMode(F_PWM_1, OUTPUT);
    pinMode(R_PWM_2, OUTPUT);
    pinMode(F_PWM_2, OUTPUT);
	pinMode(MOTOR1_EN, OUTPUT);
    pinMode(MOTOR2_EN, OUTPUT);
    digitalWrite(MOTOR1_EN, HIGH);
    digitalWrite(MOTOR2_EN, HIGH);


	Serial.begin(115200);

	//Timer configs
	ITimer3.init();
	ITimer5.init();

	// Initialize MCP2515 running at 16MHz with a baudrate of 500kb/s and the masks and filters disabled.
	if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK){
		Serial.println("MCP2515 Initialized Successfully!");
	} else {
		Serial.println("Error Initializing MCP2515...");
	}
	CAN0.setMode(MCP_NORMAL);  // Change to normal mode to allow messages to be transmitted
	// Read back from EEPROM
	loadSafetyConfigs(temp1c, temp2c);
}


void loop()
{
	//txBufDebug = {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	//CAN0.sendMsgBuf(0x402, sizeof(txBufDebug), txBufDebug);	//For debugging
	
	//In case no new messages are received by the temperature sensor this conditional is met
	//For safety reasons this function forces the filtered value to be the last read temperature
	//If desirable make this deactivate the motor
	if((timetempmess + 500) < millis()){
		temp1f = filterSensorValue1(temp1s.BLtemp);
		temp2f = filterSensorValue2(temp2);
		timetempmess = millis();
	}

	CAN0.readMsgBuf(&rxId, &len, rxBuf);

	//This is the message to remotely reset the MCU so it enters the bootloader
	//Do NOT delete this function, as doing so will make the MCU incapable of remote uploading
	//In case that happens you are to manually reset the MCU and then send the update handshake
	if(rxId == 0x243){
		cli();                 // disable interrupts
		wdt_enable(WDTO_15MS); // watchdog timeout 15ms
		while(1);              // wait for watchdog to reset mcu
	}
	

	if(rxId == 0x401){
		CAN0.sendMsgBuf(0x401, 0, 0);
	}
	if(rxId == 0x402){
		static int result[8];
		readDigital(rxBuf,result);
		for (size_t i = 0; i < 8; i++){
			switch (result[i]){
			case 0:
				digitalWrite(ledpins[i],0);
				break;
			case 1:
				digitalWrite(ledpins[i],1);
				break;
			case 2:
				Serial.println("Error LEDs");
				break;	
			case 3:
				break;
			default:
				break;
			}
		}
		Temp = readPWMEnc(rxBuf,2);
		if(Temp < 0){
			if (Temp == -1){
				Serial.print("Error PWM 1");
			}
		} else {
			PWM1_val = Temp;
		}
		Temp = readPWMEnc(rxBuf,3);
		if(Temp < 0){
			if (Temp == -1){
				Serial.print("Error PWM 2");
			}
		} else {
			PWM2_val = Temp;
		}
		Temp = readPWMEnc(rxBuf,4);
		if(Temp < 0){
			if (Temp == -1){
				Serial.print("Error Encoder");
			}
		} else {
			Enc = Temp; //This is an incorect implementation of the encoder, Currently in use since it curently has no real function
		}
		analogWrite(PWM1, PWM1_val);
		analogWrite(PWM2, PWM2_val);

		sendDigital(result,PWM1_val,PWM2_val,Enc,txBuf);
		CAN0.sendMsgBuf(0x422, sizeof(txBuf), txBuf);
	}


	if(rxId == 0x403){
		tconfigbuf = safetyConfig(rxBuf);
		switch (tconfigbuf.Monit_Enable){
			case 0:
				temp1c = tconfigbuf;
				break;
			case 1:
				temp1c = tconfigbuf;
				break;
			case 2:
				Serial.print("Error 0x403");
				break;	
			default:
				break;
		}
		if(temp1c.saveeeprom == 1){
			updateSafetyConfig1(temp1c);
		}
		tconfigbuf = safetyConfig(rxBuf+4);
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
		sendsafetyConfig(temp1c, txBuf);
		sendsafetyConfig(temp2c, txBuf+4);
		
		CAN0.sendMsgBuf(0x423, sizeof(txBuf), txBuf);
	}

	if(rxId == 0x404){
		aconfigbuf = aquisitionConfig (rxBuf);
		switch (aconfigbuf.Aquics_Enable){
			case 0:
				aquisc = aconfigbuf;
				break;
			case 1:
				aquisc = aconfigbuf;
				break;
			case 2:
				Serial.print("Error 0x404");
				break;	
			default:
				break;
		}
		sendaquisitionConfig(aquisc,txBuf);
		CAN0.sendMsgBuf(0x424, sizeof(txBuf), txBuf);
	}

	if(rxId == 0x405){
		static uint8_t EnableBuf = 0;
		EnableBuf = (rxBuf[0] >> 6);
		switch (EnableBuf){
			case 0:
				aquisc.Aquics_Enable = EnableBuf;
				break;
			case 1:
				aquisc.Aquics_Enable = EnableBuf;
				break;
			case 2:
				Serial.print("Error 0x405");
				break;	
			default:
				break;
		}
		static byte EnableBufc[1] = {0x00};
		EnableBufc[0] = ((aquisc.Aquics_Enable << 6) & 0xC0);
		CAN0.sendMsgBuf(0x425, 8, EnableBufc);	
	}

	//Only temp1s is fully implemented and it is reading BLtem
	//To implement other sensors simply copy this function and modify the necessary values
	if (rxId == 0x510){
		temp1s = tempRead (rxBuf);
		//Serial.println(temp1s.BLtemp);
		//if(temp1s.BLstatus == 1){
			temp1f = filterSensorValue1(temp1s.BLtemp /*- temp1s.CJtemp*/);
			Serial.print("Temperatura 1 Filtrada: ");
			Serial.println(temp1f);
			timetempmess = millis();
		//}
	}

	if(((millis() - timeaquisition) >= aquisc.timer) & (aquisc.Aquics_Enable || aquisc.Aquics_Enable_Continuous)){
		aquisc.Aquics_Enable = 0;
		for (size_t i = 0; i < 8; i++){
			CAN0.sendMsgBuf(remoteIDs[i], 0, NULL);
			delayMicroseconds(3);//Place 1000 here if you want to check with the raspberry
			CAN0.readMsgBuf(&rxId, &len, rxBuf);
			//switch (i){
			// case 0:
			//	if (rxId == DataIDs[0]){
			//		temp1s = tempRead (rxBuf);
			//		if(temp1s.BLstatus == 1){
			//			//temp1f = filterSensorValue(temp1s.BLtemp - temp1s.CJtemp);
			//			Serial.print("Temperatura 1 Filtrada: ");
			//			Serial.println(temp1f);
			//			timetempmess = millis();
			//		}
			//	}
			//	break;
			// case 1:
			//	if (rxId == DataIDs[1]){
			//		temp2s = tempRead (rxBuf);
			//		if(temp2s.BLstatus == 1){
			//			temp2f = filterSensorValue2(temp2s.BLtemp - temp2s.CJtemp);
			//			Serial.print("Temperatura 2 Filtrada: ");
			//			Serial.println(temp2f);
			//			timetempmess = millis();
			//		}
			//	}
			//	break;
			// case 2:
			//	if (rxId == DataIDs[2]){
			//		temp3s = tempRead (rxBuf);
			//		if(temp3s.BLstatus == 1){
			//			//temp3f = filterSensorValue(temp3s.BLtemp - temp3s.CJtemp);
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
		static byte aquisData[2] = {0x00,0x00};
		aquisData[0] = (downpipePress*255)/20;
		aquisData[1] = (valvPos*255)/100;
		CAN0.sendMsgBuf(0x426, 8, aquisData);
		timeaquisition = millis();
	} 

	//Used for testing
	//if(rxId == 0x300){
	//	temp1 = rxBuf[0];
	//	temp2 = rxBuf[1];
	//	temp1f = filterSensorValue1(temp1);
	//	temp2f = filterSensorValue2(temp2);
	//	timetempmess = millis();
	//}

	//Section of code to start timers in case of unsafe temperatures.
	//////////////////////////////////////////////////////////////////////////
	if(temp1c.Monit_Enable == 1){
		if(temp1f >= temp1c.maxtemp){
			if(starttimer1 == 0){
				ITimer5.attachInterruptInterval(temp1c.timer, TimerHandler1);
				previousMillistimer = millis();
				starttimer1 = 1;
			}
		} else {
			ITimer5.detachInterrupt();
			starttimer1 = 0;
		}
	} else {
		ITimer5.detachInterrupt();
		starttimer1 = 0;
	}

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
	/////////////////////////////////////////////////////////////////////////////////

	setMotor(dir,pwmVal);
	rxId = 0;
}