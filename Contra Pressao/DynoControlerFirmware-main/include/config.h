#ifndef CONFIG_H
#define CONFIG_H

#define LED_PIN             13


#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    #define CAN0_CS_PIN       	33
    #define CAN0_INT_PIN       	3
#elif defined (__AVR_ATmega328P__) 
    #define CAN0_CS_PIN       	10
    #define CAN0_INT_PIN       	2    
#endif



#define CAN0_DLC      	    8

#define FAST_INIT_GVAR      "fastInit"
#define SEL_PEDAL_GVAR      "selPedal"
#define DATA_INPUT_GVAR     "dataInput"
#define PEDAL_APP1_GVAR     "app1"
#define PEDAL_APP2_GVAR     "app2"
#define DATA_INPUT_GVAR     "dataInput"
#define PEDAL_POS_GVAR      "pedalPos"

#define PEDAL_NAME_GVAR     "M_Menu.pedalName.txt"
#define CTLPAD_FRAME_ID     0x41A
#define CTLPAD_MUX_TPS      0x00
#define CTLPAD_MUX_RPM      0x01
#define CTLPAD_RPM_REF      1800
#define P_NAME_LEN           20
#define ERROR_INV_NUMBER        777777
#define ERROR_INV_STRING        "ERROR"

#define CONF_FAST_INIT_TAG  "CF_PedalStep1.c0.val"
#define CONF_MANUAL_DI_TAG  "CF_PedalStep1.r6.val"
#define CONF_ANALOG_DI_TAG  "CF_PedalStep1.r7.val"
#define CONF_CANBUS_DI_TAG  "CF_PedalStep1.r8.val"

#define CONF_SEL_PEDAL1_TAG "CF_PedalStep1.r0.val"
#define CONF_SEL_PEDAL2_TAG "CF_PedalStep1.r1.val"
#define CONF_SEL_PEDAL3_TAG "CF_PedalStep1.r2.val"
#define CONF_SEL_PEDAL4_TAG "CF_PedalStep1.r3.val"
#define CONF_SEL_PEDAL5_TAG "CF_PedalStep1.r4.val"
#define CONF_SEL_PEDAL6_TAG "CF_PedalStep1.r5.val"

#define CONF_MIN_APP1_TAG   "CF_PedalStep1.x0.val"
#define CONF_MIN_APP2_TAG   "CF_PedalStep1.x1.val"
#define CONF_MAX_APP1_TAG   "CF_PedalStep1.x2.val"
#define CONF_MAX_APP2_TAG   "CF_PedalStep1.x3.val"
#define CONF_VREF_CAL_TAG   "CF_PedalStep1.x4.val"
#define CONF_VREF_SIG_TAG   "CF_PedalStep1.x5.val"
#define CONF_PED_MAME_TAG   "CF_PedalStep1.t10.txt"
#define CONF_INV_MESG_TAG   "CF_PedalStep2.t0.txt"

#define COM_VALVE_DAT_PRE   "CM_Throttle.x0.val"
#define COM_VALVE_SET_TAG   "CM_Throttle.x1.val"
#define COM_VALVE_DAT_SET   "CM_Throttle.x2.val"

#define COM_PEDAL_DAT_TAG   "CM_Pedal.x2.val"
#define COM_PEDAL_PER_TAG   "CM_Pedal.j0.val"
#define COM_PEDAL_IND_TAG   "CM_Pedal.rD.val"

#define CAL1_APP1_VAL_TAG   "CA_PedalStep1.x0.val"
#define CAL1_APP2_VAL_TAG   "CA_PedalStep1.x1.val"
#define CAL1_VREF_CAL_TAG   "CA_PedalStep1.x2.val"

#define CAL2_MIN_APP1_TAG   "CA_PedalStep2.x0.val"
#define CAL2_MIN_APP2_TAG   "CA_PedalStep2.x1.val"
#define CAL2_MAX_APP1_TAG   "CA_PedalStep2.x2.val"
#define CAL2_MAX_APP2_TAG   "CA_PedalStep2.x3.val"
#define CAL2_CUR_APP1_TAG   "CA_PedalStep2.x4.val"
#define CAL2_CUR_APP2_TAG   "CA_PedalStep2.x5.val"
#define CAL2_CAL_STAT_TAG   "CA_PedalStep2.cstate.val"
#define CAL2_CAL_SCAL_TAG   "CA_PedalStep2.scl.val"

#define CAL3_SEL_PEDAL1_TAG "CA_PedalStep3.r0.val"
#define CAL3_SEL_PEDAL2_TAG "CA_PedalStep3.r1.val"
#define CAL3_SEL_PEDAL3_TAG "CA_PedalStep3.r2.val"
#define CAL3_SEL_PEDAL4_TAG "CA_PedalStep3.r3.val"
#define CAL3_SEL_PEDAL5_TAG "CA_PedalStep3.r4.val"
#define CAL3_SEL_PEDAL6_TAG "CA_PedalStep3.r5.val"

#define CAL3_MIN_APP1_TAG   "CA_PedalStep3.x0.val"
#define CAL3_MIN_APP2_TAG   "CA_PedalStep3.x1.val"
#define CAL3_MAX_APP1_TAG   "CA_PedalStep3.x2.val"
#define CAL3_MAX_APP2_TAG   "CA_PedalStep3.x3.val"
#define CAL3_VREF_CAL_TAG   "CA_PedalStep3.x4.val"
#define CAL3_VREF_SIG_TAG   "CA_PedalStep3.x5.val"
#define CAL3_PED_MAME_TAG   "CA_PedalStep3.t10.txt"

#define CAL4_MIN_APP1_TAG   "CA_PedalStep4.x0.val"
#define CAL4_MIN_APP2_TAG   "CA_PedalStep4.x1.val"
#define CAL4_MAX_APP1_TAG   "CA_PedalStep4.x2.val"
#define CAL4_MAX_APP2_TAG   "CA_PedalStep4.x3.val"
#define CAL4_VREF_CAL_TAG   "CA_PedalStep4.x4.val"
#define CAL4_VREF_SIG_TAG   "CA_PedalStep4.x5.val"
#define CAL4_PED_MAME_TAG   "CA_PedalStep4.t10.txt"

#define CONF_FAST_INIT_TAG  "CF_PedalStep1.c0.val"
#define CONF_MANUAL_DI_TAG  "CF_PedalStep1.r6.val"
#define CONF_ANALOG_DI_TAG  "CF_PedalStep1.r7.val"
#define CONF_CANBUS_DI_TAG  "CF_PedalStep1.r8.val"

#define PG_SPLASH_SCREEN        0
#define PG_MAIN_MENU            1
#define PG_SELECT_DEVICE        2
#define PG_CALIB_PEDAL_STEP1    3
#define PG_CALIB_PEDAL_STEP2    4
#define PG_CALIB_PEDAL_STEP3    5
#define PG_CALIB_PEDAL_STEP4    6
#define PG_CONF_PD_STEP1        7
#define PG_CONF_PD_STEP2        8
#define PG_RESET_DEVIC_STEP1    9
#define PG_RESET_DEVIC_STEP2    10
#define PG_RESET_DEVIC_STEP3    11
#define PG_COMMAND_PEDAL        12
#define PG_COMMAND_THROTTLE     13
#define PG_KEYBOARD_B           14
#define PG_KEYBOARD_A           15
#define PG_KEYBOARD_C           16

//EEPROM memory address
#define EEPROM_CFG_ADDR       0
#define EEPROM_PEDAL_ADDR        80
#define EEPROM_SETPOINT_ADDR     343

#define MIN_PEDAL_ID    0
#define MAX_PEDAL_ID    5

#define CALIB_PEDAL_APP1_CH A0
#define CALIB_PEDAL_APP2_CH A1

#define GPIO_CHANNELS  3

#define THREAD_INTERVAL_20MS    20
#define THREAD_INTERVAL_50MS    50
#define THREAD_INTERVAL_100MS   100
#define THREAD_INTERVAL_300MS   300
#define THREAD_INTERVAL_250MS   250


#define CLTPAD_MANUAL_INPUT 0
#define CLTPAD_ANALOG_INPUT 1
#define CLTPAD_CANBUS_INPUT 2

#define CTLPAD_FRAME_ID     0x41A
#define CTLPAD_MUX_TPS      0x00
#define CTLPAD_MUX_RPM      0x01
#define CTLPAD_RPM_REF      1800

#define CTLPAD_CAN_INIT     0
#define CTLPAD_CAN_READY    1

#define Num_pul_max_encoder_pedal 100


//Encoder pins
#define CLTPAD_ENC_CLK_PIN   18
#define CLTPAD_ENC_DT_PIN    2
#define CLTPAD_ENC_SW_PIN    4

//Control pins
#define CLTPAD_AN_INP_PIN    A12
#define CLTPAD_APP_INP_PIN   A2
#define CLTPAD_APP1_INP_PIN   A5
#define CLTPAD_APP2_INP_PIN   A7
#define CLTPAD_R_IS_INP_PIN   A8
#define CLTPAD_L_IS_INP_PIN   A6
#define CLTPAD_PRESS_INP_PIN  A10

//H-bridge
#define CLTPAD_F_PWM_PIN  6
#define CLTPAD_R_PWM_PIN  8
#define CLTPAD_F_EN_PIN   16
#define CLTPAD_R_EN_PIN   37


#define CLTPAD_MAX_CAL_ITR   4





#endif
