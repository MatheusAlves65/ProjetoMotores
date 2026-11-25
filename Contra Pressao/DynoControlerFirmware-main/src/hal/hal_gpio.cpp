#include "hal/hal_gpio.h"

#define ADC_MAX     1023
#define VREF        5.0f

static h_gpio_cmd_cb_t   cmd_cb;
static boolean           enc_sw_last_state;
static boolean           enc_sw_reset;

float   h_gpio_read_voltage(uint16_t raw);
void    h_gpio_handler_enc_swt(const uint8_t sw_state);
boolean h_gpio_handler_enc_swt(const encoder_t *enc, app_gpio_cmd_t *out);
void    h_gpio_handler_enc_pos(encoder_t *enc, app_gpio_cmd_t *out);
void    h_gpio_handler_ana_inp(uint16_t raw,app_gpio_cmd_t *out);
void    h_gpio_handler_app_data(uint16_t raw1, uint16_t raw2, app_gpio_cmd_t *out);
void    h_gpio_valve_controller(uint16_t raw_press);
unsigned long control_timer = 0;

#define ADC_MAX     1023.0f
#define VREF        5.0f

unsigned long time_int = 0;
float erro,pressure,erro_compound,PWM_Output = 0;
uint8_t controlSignal = 0;
volatile float setpoint = 1.05;
bool init_contr,send_press = 0;
bool lock_contr = 1;
static float pressureprev,pressurefilt = 0;

void h_gpio_init(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.print("H_GPIO_INIT ");
    #endif  
    //1 - Initialize gpio driver
    d_gpio_init();
    //2 - Initialize variables
    enc_sw_last_state = false;
    enc_sw_reset      = false;
}
void h_gpio_process()
{ 
    encoder_t       enc;
    app_gpio_cmd_t  cmd;
    uint16_t        raw_ap1,raw_ap2,raw_ain,raw_press;
            
    //1 - Read acceleration pedal position 1 and 2
    d_gpio_read_pedal_app1(&raw_ap1);
    d_gpio_read_pedal_app2(&raw_ap2);
    h_gpio_handler_app_data(raw_ap1,raw_ap2,&cmd);        
    cmd_cb(&cmd);

    //3 - Read analog input command
    d_gpio_read_analog_input(&raw_ain);        
    h_gpio_handler_ana_inp(raw_ain,&cmd);
    cmd_cb(&cmd);
    
    //4 - Read manual input encoder
    d_gpio_read_enc(&enc);

    //5 - Check encoderswitch button
    if(h_gpio_handler_enc_swt(&enc,&cmd))    
    {
        cmd_cb(&cmd);
    }
    //6 - Check manual input
    h_gpio_handler_enc_pos(&enc,&cmd);       
    cmd_cb(&cmd);
    //7 - Read Pressure
    d_gpio_read_press(&raw_press);
    //8 - Valve Controller
    h_gpio_valve_controller(raw_press);
}

void h_gpio_setpoint(float input){
    setpoint = input;
}

void h_gpio_init_contr(){
    init_contr = 1;
}

void h_gpio_lock_contr(){
    lock_contr = 1;
}

void h_gpio_change_inf_status(){
    send_press = !send_press;
}

void h_gpio_valve_controller(uint16_t raw_press){

    if (init_contr == 1){
        d_gpio_set_valve(1, 170);
        delay (800);
        init_contr = 0;
        lock_contr = 0;
    }
    if(((millis() - control_timer) >= 100)){
        pressure = map2(((raw_press/ADC_MAX)*VREF),1,5,0,10)+1;
        //pressure = (raw_press/ADC_MAX)*VREF;
        pressurefilt = 0.854 * pressurefilt + 0.0728 * pressure + 0.0728 * pressureprev;
        pressureprev = pressure;
        erro = setpoint - pressurefilt;
        //Serial.print("Pressure: ");
        //Serial.print(pressurefilt*10);
        //Serial.print("\t");
        //Serial.print("Erro: ");
        //Serial.print(erro);
        //Serial.print("\t");
        //Serial.print("Volt: ");
        //Serial.print(((raw_press/ADC_MAX)*VREF)*10);
        if(send_press == 1){
            h_screen_valve_data(setpoint, pressurefilt);
        }
        if(lock_contr == 0){
            if (erro < 0){
                controlSignal = 1;
            } else {
                controlSignal = 2;
            }
            if (abs(erro) <0.025){
                controlSignal = 0;
                d_gpio_set_valve(controlSignal, 5);
                
            } else {
                d_gpio_set_valve(controlSignal, 5);
            }
        }
        control_timer = millis();
        //Serial.print("\t");
        //Serial.print("PWM: ");
        //Serial.print(PWM_Output);
        //Serial.print("\t");
        //Serial.print("Control: ");
        //Serial.println(controlSignal);
    }
    d_gpio_set_valve(controlSignal, 5);
}

int h_input_get_internal_voltage(void)
{
    int vref;
    vref=d_gpio_get_internal_voltage();
    if (vref<400 || vref>550)
    {
        //Default value
        vref=500; //5 Volts
    }
    return vref;
}
void h_gpio_register_cmd_callback(h_gpio_cmd_cb_t cb)
{
    cmd_cb = cb;
}

float h_gpio_read_voltage(uint16_t raw)
{
    return (raw / ADC_MAX) * VREF;
}


boolean h_gpio_handler_enc_swt(const encoder_t *enc, app_gpio_cmd_t *out)
{
    boolean ret = false;
    if (enc->e_swt != enc_sw_last_state)
    {
        //Switch button pressed
        if (enc->e_swt==false)
        {
            //Switch button pressed
            out->id   = APP_GPIO_CMD_SET_PEDAL_REL;            
            ret = true;
        }        
    }
    enc_sw_last_state = enc->e_swt;
    return ret;
}
void h_gpio_handler_enc_pos(encoder_t *enc, app_gpio_cmd_t *out)
{    
    if (enc_sw_reset)
    {
        enc->e_pos--;        
        d_gpio_write_enc(enc);
        if (enc->e_pos<=MIN_ENCODER_POSITION)
        {
            enc_sw_reset=false;
        }
    }
    else 
    {
        if ((enc->e_pos)>MAX_ENCODER_POSITION)
        {
            enc->e_pos = MAX_ENCODER_POSITION;
            d_gpio_write_enc(enc);
        }
        else if ((enc->e_pos)<MIN_ENCODER_POSITION)
        {
            enc->e_pos = MIN_ENCODER_POSITION;
            d_gpio_write_enc(enc);
        }

    }   
    out->id       = APP_GPIO_CMD_SET_MAN_INPUT;
    out->epos      = enc->e_pos;    
}
void h_gpio_reset_encoder(void)
{
    enc_sw_reset=true;
}

void h_gpio_handler_ana_inp(uint16_t raw, app_gpio_cmd_t *out)
{
    uint16_t val;
    val = map(raw,0,1023,0,100);   
    out->id = APP_GPIO_CMD_SET_AN_INPUT;
    out->anin = val;
}

void h_gpio_handler_app_data(uint16_t raw_ap1,uint16_t raw_ap2, app_gpio_cmd_t *out)
{
    float volts;
    volts = (raw_ap1/ADC_MAX)*VREF;
    out->app1=volts;
    volts = (raw_ap2/ADC_MAX)*VREF;
    out->app2=volts;
    out->id = APP_GPIO_CMD_SET_APP_DAT;    
}

float h_gpio_get_app1_voltage(const uint16_t vref)
{
    uint16_t raw;
    float    volt;    
    d_gpio_read_pedal_app1(&raw);
    volt = (raw/ADC_MAX)*(vref/100)*1.0f;        
    return volt;
}

float h_gpio_get_app2_voltage(const uint16_t vref)
{
    uint16_t raw;
    float    volt;    
    d_gpio_read_pedal_app2(&raw);
    volt = (raw/ADC_MAX)*(vref/100)*1.0f;        
    return volt;
}
    
float map2(float x, float in_min, float in_max, float out_min, float out_max) {
  if (x < in_min) x = in_min;
  if (x > in_max) x = in_max;

  if (in_max == in_min)
    return out_min;

  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

uint16_t v1_pedal,v2_pedal;
void h_gpio_handler_send_voltage(load_pedal_t *lpedal)
{
  v1_pedal= (uint16_t) map2(lpedal->pedal_pos, MIN_ENCODER_POSITION,MAX_ENCODER_POSITION,lpedal->min_app1*(4096.0f/lpedal->vref_cal),lpedal->max_app1*(4096.0/lpedal->vref_cal));
  v2_pedal= (uint16_t) map2(lpedal->pedal_pos, MIN_ENCODER_POSITION,MAX_ENCODER_POSITION,lpedal->min_app2*(4096.0f/lpedal->vref_cal),lpedal->max_app2*(4096.0/lpedal->vref_cal));
  v1_pedal = constrain(v1_pedal,0,4095);
  v2_pedal = constrain(v2_pedal,0,4095);
  /*Serial.print("v1_pedal: ");
  Serial.print(v1_pedal);
  Serial.print(",v2_pedal: ");
  Serial.println(v2_pedal);*/
  d_gpio_send_DAC_voltage (0x60,v1_pedal);
  d_gpio_send_DAC_voltage (0x61,v2_pedal);
}
    