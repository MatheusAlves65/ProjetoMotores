#ifndef SYSTEM_H
#define SYSTEM_H

#include<Arduino.h>

typedef struct system_config_t
{
    bool        fast_init;    //Fast initiatization (true or false)
    uint8_t     select_pedal; //Select pedal (id= 0-5)
    uint8_t     data_input;   //Data input for pedal (Manual, Analog, CANBus)
}system_config_t;

typedef struct load_pedal_t
{
    float       min_app1,max_app1,cur_app1;
    float       min_app2,max_app2,cur_app2;
    float       vref_sig, vref_cal;    
    short       pedal_id;
    uint16_t    pedal_pos;
    char        name[20]; 
}load_pedal_t;

typedef struct valve_sett
{
    float setpoint;
    float pressure;
}valve_sett;


typedef struct calib_pedal_t
{
    float   min_app1,max_app1,cs_min_app1,cs_max_app1;
    float   min_app2,max_app2,cs_min_app2,cs_max_app2;
    float   cur_app1,cur_app2;    
    float   vref_cal;
    uint8_t cumsum;    
}calib_pedal_t;


typedef struct 
{
    unsigned long   id;        // 11 or 29-bit ID
    uint8_t         dlc;       // data length
    uint8_t         data[8];   // payload
} can_frame_t;


typedef enum 
{
    SCR_EV_T00_REQ_SYS_CFG, //Trigger 0 - request system configuration data
    SCR_EV_T01_REQ_PED_DAT, //Trigger 1 - request configuration pedal data
    SCR_EV_T02_REQ_SAV_DAT, //Trigger 2 - request save system config and pedal data
    SCR_EV_T03_REQ_STA_ACQ, //Trigger 3 - request start data acquisition
    SCR_EV_T04_REQ_STO_ACQ, //Trigger 4 - request stop data acquisition   
    SCR_EV_T05_REQ_VRF_DAT, //Trigger 5 - request udpate vref, app1 and app2 values
    SCR_EV_T06_REQ_UPD_VRF, //Trigger 6 - request udpate vref value
    SCR_EV_T07_REQ_STA_CAL, //Trigger 7 - request start pedal calibration   
    SCR_EV_T08_REQ_STO_CAL, //Trigger 8 - request stop pedal acquisition
    SCR_EV_T09_REQ_PED_DAT, //Trigger 9 - request calibration pedal data
    SCR_EV_T10_REQ_CAL_DAT, //Trigger 10 - request soft reset
    SCR_EV_T11_REQ_SAV_CAL, //Trigger 11 - request save calibration
    SCR_EV_T12_REQ_SOF_RES, //Trigger 12 - request soft reset
    SCR_EV_T13_REQ_HAR_RES, //Trigger 13 - request factory reset
    SCR_EV_T14_REQ_VAL_SET, //Trigger 14 - request save Pressure Setpoint
    SCR_EV_T15_REQ_VAL_INI, //Trigger 15 - request Controler Function
    SCR_EV_T16_REQ_VAL_LOC,  //Trigger 16 - Lock Valve Position
    SCR_EV_T17_REQ_VAL_COM  //Trigger 16 - Lock Valve Position
} screen_event_t;


typedef enum {
    P1_ID, P2_ID, P3_ID, P4_ID,P5_ID,P6_ID
}pedal_id;

typedef enum {
    CAN_TX_DATA, CAN_RX_DATA
}can_state_machine_t;

typedef enum {
    MANUAL_DT_IN, ANALOG_DT_IN,CAN_DT_IN
}data_input_t;

typedef enum {
    LOAD_PEDAL, CALIB_PEDAL
}pedal_type_t;

typedef enum{
    ANALOG_INPUT, 
    APP1_INPUT, 
    APP2_INPUT
} gpio_channel_t;

typedef enum {
    APP_CALIB_STATE_S0, 
    APP_CALIB_STATE_S1, 
    APP_CALIB_STATE_S2, 
    APP_CALIB_STATE_S3, 
    APP_CALIB_STATE_S4, 
    APP_CALIB_STATE_S5    
} calib_state_id_t;

typedef enum {
    APP_STATE_INI, 
    APP_STATE_CAL, 
    APP_STATE_RUN    
} app_state_id_t;

typedef struct app_timers_t
{
    uint32_t now_ms;                // Snapshot of current time    
    uint32_t ped_press_evt_ms;      // Time of pedal press event
    uint32_t ped_hold_evt_ms;       // Time of pedal hold event
    uint32_t ped_press_elapsed_ms;  // Pedal press event watchdog
    uint32_t ped_hold_elapsed_ms;   // State hold 
} app_timers_t;

typedef struct
{   //Calibration flags
    bool pedal_hold;
    bool pedal_press;
} app_flags_t;

typedef struct app_state_t{
    app_timers_t        timers;
    app_flags_t         flags;
    app_state_id_t      state; 
    calib_state_id_t    c_state;
} app_state_t;

typedef enum {
    APP_CMD_SET_SEL_PEDAL,  //Command to start data input acquisition      
    APP_CMD_LOAD_SEL_PEDAL,
    APP_CMD_SET_SYS_CONF, 
    APP_CMD_SAV_CFG_DAT,
    APP_CMD_SAV_PED_DAT,
    APP_CMD_SAV_CAL_DAT,
    APP_CMD_REQ_STA_CAL,
    APP_CMD_REQ_STO_CAL,
    APP_CMD_REQ_UPD_VRF,
    APP_CMD_REQ_VRF_DAT,
    APP_CMD_STA_DAT_ACQ,  //Command to start data input acquisition      
    APP_CMD_STO_DAT_ACQ,  //Command to stop data input acquisition     
    APP_CMD_REQ_CAL_DAT,  //Command to stop data input acquisition    
    APP_CMD_REQ_SOF_RES,  //Command to stop data input acquisition   
    APP_CMD_REQ_HAR_RES,  //Command to stop data input acquisition   
    APP_CMD_REQ_VAL_SET,   //Command to stop data input acquisition   
    APP_CMD_REQ_INI_CON,   //Command to stop data input acquisition 
    APP_CMD_REQ_LOC_VAL,   //Command to stop data input acquisition 
    APP_CMD_REQ_VAL_COM   //Command to stop data input acquisition 
} app_scr_cmd_id_t;

typedef union {
    system_config_t cfg;
    load_pedal_t    pedal;
    valve_sett      valve;         
} app_scr_cmd_da_t;

typedef struct {
    app_scr_cmd_id_t id;
    app_scr_cmd_da_t val;
} app_scr_cmd_t;

typedef enum {
    APP_CAN_CMD_SET_PEDAL_POS, //Command to set pedal pedal position
    APP_CAN_CMD_SET_RPM        //Command to set rpm    
} app_can_cmd_id_t;

typedef struct {
    app_can_cmd_id_t id;  // Command id
    uint16_t         val; // Pedal position or rpm
} app_can_cmd_t;

typedef enum {
    APP_GPIO_CMD_SET_PEDAL_POS, //Command to set pedal pedal position
    APP_GPIO_CMD_SET_PEDAL_REL, //Command to set pedal pedal position
    APP_GPIO_CMD_SET_MAN_INPUT, //Command to set pedal pedal position    
    APP_GPIO_CMD_SET_AN_INPUT,  //Command to set analog input
    APP_GPIO_CMD_SET_APP_DAT  //Command to set analog input     

} app_gpio_cmd_id_t;


typedef struct app_gpio_cmd_t{
    app_gpio_cmd_id_t   id;     // Command id
    uint16_t            epos;  // Integer value    
    uint16_t            anin;  // Integer value    
    float               app1;  // Integer value    
    float               app2;  // Integer value    
} app_gpio_cmd_t;

typedef struct encoder_t{
    int16_t     e_pos;  //Encoder position
    boolean     e_swt;  //Switch pressed
} encoder_t;

#define CAN_RX_QUEUE_SIZE 02

typedef struct {
    can_frame_t buf[CAN_RX_QUEUE_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} can_rx_queue_t;

//#define DEBUG_SERIAL_ENABLE
#ifdef DEBUG_SERIAL_ENABLE
    #if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
        //#define dbSerial Serial2
        #define dbSerial Serial
    #elif defined (__AVR_ATmega328P__) 
        #define dbSerial Serial
    #endif
   
   //#define DEBUG_COMMUNICATION
   //#define DEBUG_TIMEOUT
   //#define DEBUG_DIGITAL_INPUT
   //#define DEBUG_ANALOG_INPUT   
   //#define DEBUG_ANALOG_OUTPUT
   //#define DEBUG_CAN_NETWORK
   //#define DEBUG_LIN_NETWORK


#endif

#endif