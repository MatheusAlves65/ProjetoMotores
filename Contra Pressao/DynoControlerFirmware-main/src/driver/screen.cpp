#include "driver/screen.h"

EasyNex    nextScreem(Serial);

static d_screen_event_cb_t  event_cb;

//Initialize screen
void d_screen_init(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("D_SCREEN_INIT");
    #endif
    nextScreem.begin(9600);         
}
//Request a number from NextScreen. No verification
uint32_t d_screen_get_number(const char *tag)
{
  uint32_t data;  
  data=nextScreem.readNumber(tag);
  while (data==ERROR_INV_NUMBER)
  {
    data=nextScreem.readNumber(tag);
    delay(50);
  }
  return data;
}
//Request a number from NextScreen. With range verification
uint32_t d_screen_get_number(const char *tag, const int16_t minVal, const int16_t maxVal)
{
  int32_t data;
  data=nextScreem.readNumber(tag);
  while((data==ERROR_INV_NUMBER) || (data<=minVal) || (data>=maxVal))
  {
    data=nextScreem.readNumber(tag);
    delay(50);
  }
  return data;
}
//Request a screen. No verification
String d_screen_get_string(const char *tag)
{
  String data=nextScreem.readStr(tag);
  while((data==ERROR_INV_STRING))
  {
    data=nextScreem.readStr(tag);
    delay(50);
  }
  return data;
}
//Send number
void d_screen_send_number(const char *tag, const uint32_t number)
{
  nextScreem.writeNum(tag,number);
}
//Send string
void d_screen_send_string(const char *tag,const char *cmd)
{
  nextScreem.writeStr(tag,cmd);
}
//Set default reference
int d_screen_get_last_page_id (void)
{
  return nextScreem.lastCurrentPageId;
}
//////////////////////////////////////////////////////////////
// Event Functions
/////////////////////////////////////////////////////////////

//Event callback registry
void d_screen_register_event_cb(d_screen_event_cb_t cb)
{
    event_cb=cb;
}
//Trigger0 - Getting system configuration data
void trigger0(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T0 - Request System Configuration");
    #endif
    if (event_cb)
    {
        event_cb(SCR_EV_T00_REQ_SYS_CFG,0);
    }        
}
//Trigger1 - Request pedal data to configuration screen
void trigger1(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T1 - Request Pedal Data");
    #endif
    if (event_cb)
    {
        event_cb(SCR_EV_T01_REQ_PED_DAT,0);
    }        
}
//Trigger2 - Save System and Pedal Configuration
void trigger2(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T2 - Save pedal and system configuration");
    #endif
    if (event_cb)
    {
        event_cb(SCR_EV_T02_REQ_SAV_DAT,0);
    }        
}
//Trigger3 - Start CAN data acquisition
void trigger3(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T3 - Start Data Acquisition");
    #endif
    if (event_cb)
    {
        event_cb(SCR_EV_T03_REQ_STA_ACQ,0);
    }        
}
//Trigger4 - Stop CAN data acquisition
void trigger4(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T4 - Stop CAN Acquisition");
    #endif
    if (event_cb)
    {
        event_cb(SCR_EV_T04_REQ_STO_ACQ,0);
    }        
}
//Trigger5 - Load data calibration(Vref, APP1 and APP2)
void trigger5(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T5 - Load VREF, APP1 and APP2");
    #endif    
    if (event_cb)
    {
        event_cb(SCR_EV_T05_REQ_VRF_DAT,0);
    }        
}
//Trigger6 - Update Voltage Reference (Vref)
void trigger6(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T6 - Update VREF");
    #endif    
    if (event_cb)
    {
        event_cb(SCR_EV_T06_REQ_UPD_VRF,0);
    }        
}

//Trigger7 - Request start pedal calibration
void trigger7(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T7 - start pedal calibration");
    #endif    
    if (event_cb)
    {
       event_cb(SCR_EV_T07_REQ_STA_CAL,0);
    }        
}
//Trigger8 - Request stop pedal calibration
void trigger8(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T8 - stop pedal calibration");
    #endif    
    if (event_cb)
    {
       event_cb(SCR_EV_T08_REQ_STO_CAL,0);
    }        
}
//Trigger9 - Request pedal data to calibration screen 
void trigger9(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T9 - Request Pedal Data");
    #endif
    if (event_cb)
    {
        event_cb(SCR_EV_T09_REQ_PED_DAT,0);
    }        
}
//Trigger10 - Request calibration data 
void trigger10(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T10 - Request calib data");
    #endif
    //Serial.println("T10 - request calibration data");
    if (event_cb)
    {
        event_cb(SCR_EV_T10_REQ_CAL_DAT,0);
    }        
}
//Trigger11 - Save calibration data 
void trigger11(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T11 - Save calibration data");
    #endif
    Serial.println("T11 - Save calibration data");
    if (event_cb)
    {
        event_cb(SCR_EV_T11_REQ_SAV_CAL,0);
    }        
}
//Trigger12 - Factory reset 
void trigger12(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T12 - Factory reset");
    #endif
    //Serial.println("T12 - Soft reset");
    if (event_cb)
    {
        event_cb(SCR_EV_T12_REQ_SOF_RES,0);
    }        
}
//Trigger13 - Factory reset 
void trigger13(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T13 - Hardware reset");
    #endif
    //Serial.println("T13 - Hardware reset");
    if (event_cb)
    {
        event_cb(SCR_EV_T13_REQ_HAR_RES,0);
    }        
}
//Trigger14 - Save Counter Pressure Setpoint
void trigger14(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T14 - Valve Setpoint");
    #endif
    //Serial.println("T14 - Valve Setpoint");
    if (event_cb)
    {
        event_cb(SCR_EV_T14_REQ_VAL_SET,0);
    }        
}
//Trigger15 - Initiate Counter Pressure Control
void trigger15(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T15 - Valve Setpoint");
    #endif
    //Serial.println("T15 - Valve Setpoint");
    if (event_cb)
    {
        event_cb(SCR_EV_T15_REQ_VAL_INI,0);
    }        
}
//Trigger16 - Lock Valve Position
void trigger16(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T16 - Valve Setpoint");
    #endif
    //Serial.println("T16 - Valve Setpoint");
    if (event_cb)
    {
        event_cb(SCR_EV_T16_REQ_VAL_LOC,0);
    }        
}
//Trigger17 - Enter Command valve
void trigger17(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.println("T17 - Valve Setpoint");
    #endif
    //Serial.println("T17 - Valve Setpoint");
    if (event_cb)
    {
        Serial.println("funct");
        event_cb(SCR_EV_T17_REQ_VAL_COM,0);
    }        
}

void d_screen_loop()
{
    nextScreem.NextionListen();     
}