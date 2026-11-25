#include "hal/hal_can.h"

static  h_can_cmd_cb_t cmd_cb;
//static  uint8_t     can_state;
boolean h_can_decode_cmd(const can_frame_t *frame, app_can_cmd_t *out);
//static void h_can_init_check(void);
//Init Hal CAN
void h_can_init(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.print("H_CAN_INIT->");
    #endif  
    //can_state=CTLPAD_CAN_INIT;        
    d_can_init();    
}
void h_can_process(void)
{  
  can_frame_t   rx;
  app_can_cmd_t cmd;

  while (d_can_receive(&rx))
  {
    if (h_can_decode_cmd(&rx,&cmd)==true)
    {
      //Serial.println("h_can_event");
      cmd_cb(&cmd);
    }
  } 
 /* if(can_state==CTLPAD_CAN_INIT)
  {
    can_frame_t frame;
    uint16_t rpm;
    rpm = CTLPAD_RPM_REF;   
    frame.id     = CTLPAD_FRAME_ID;
    frame.dlc    = 8;
    frame.data[0]= CTLPAD_MUX_RPM;
    frame.data[2]= rpm>>8;
    frame.data[3]= rpm& 0xFF;
    while (!(d_can_send(&frame)));
  }*/
}

/*void h_can_set_state(uint8_t state)
{
  can_state = state;
}*/
boolean h_can_decode_cmd(const can_frame_t *frame, app_can_cmd_t *out)
{  
  if(frame->id == CTLPAD_FRAME_ID && frame->data[0]==CTLPAD_MUX_TPS)
  {
    uint16_t   raw_data;       
    //Protocol decoding
    raw_data  = (uint16_t)(frame->data[2]<<8|frame->data[3]);  
    out->val  = (uint16_t)(raw_data*0.1f);
    out->id   = APP_CAN_CMD_SET_PEDAL_POS; 
    return true;    
  }
  else if(frame->id == CTLPAD_FRAME_ID && frame->data[0]==CTLPAD_MUX_RPM)
  {
    uint16_t      raw_data;       
    //Protocol decoding
    raw_data  = (uint16_t)(frame->data[2]<<8|frame->data[3]);  
    out->val  = (uint16_t)(raw_data);
    out->id   = APP_CAN_CMD_SET_RPM;
    return true;
  }
  return false;
}
void h_can_register_cmd_callback(h_can_cmd_cb_t cb)
{
    cmd_cb = cb;
}
/*void h_can_start(void)
{
  //d_can_set_normal();
  d_can_start();   
}
void h_can_stop(void)
{
  d_can_set_loopback();
  d_can_stop();  
}*/

/*static void h_can_init_check(void)
{
  #ifdef DEBUG_SERIAL_ENABLE
  dbSerial.print("h_can_init_check->");
  #endif  
  //Enable CAN send
   can_snd_enable=true;  
  //Enable CAN Loopback mode
  d_can_set_loopback();
  //Init the test
  
}*/

