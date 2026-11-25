

#include "hal/hal_screen.h"

/* Providers */
static app_cfg_provider_t       cfg_provider;
static app_lpedal_provider_t    load_pedal_provider;
static h_screen_cmd_cb_t        cmd_cb;

static void h_screen_send_data_to_screen(const uint8_t page);

void h_screen_process(void)
{
    d_screen_loop();      
}
void h_screen_register_cfg_provider(app_cfg_provider_t cb)
{
    cfg_provider = cb;
}
void h_screen_register_lpedal_provider(app_lpedal_provider_t cb)
{
    load_pedal_provider = cb;
}
void h_screen_register_cmd_callback(h_screen_cmd_cb_t cb)
{
    cmd_cb = cb;
}

//Event handler - called by driver
static void h_screen_event_handler(screen_event_t evt, uint8_t err)
{
    //screen request system config
    switch(evt)
    {
        case SCR_EV_T00_REQ_SYS_CFG:
        {            
            system_config_t cfg;
            load_pedal_t    lpedal;
            
            //Getting system configuration data
            cfg_provider(&cfg);
            //Getting pedal data
            load_pedal_provider (&lpedal);            
            d_screen_send_number(FAST_INIT_GVAR,  cfg.fast_init);                
            d_screen_send_number(SEL_PEDAL_GVAR,  cfg.select_pedal);
            d_screen_send_number(DATA_INPUT_GVAR, cfg.data_input);
            d_screen_send_string(PEDAL_NAME_GVAR,lpedal.name);                
        }
        break;          
        case SCR_EV_T01_REQ_PED_DAT:        
        {
            uint8_t             s_pedal;    
            app_scr_cmd_t       cmd;                
            s_pedal = (uint8_t)d_screen_get_number(SEL_PEDAL_GVAR);
            switch (s_pedal)
            {
                case P1_ID:
                {
                    //Commmand to set selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P1_ID;
                    cmd_cb(&cmd);                                       
                    d_screen_send_number(CONF_SEL_PEDAL1_TAG,1);
                    h_screen_send_data_to_screen(PG_CONF_PD_STEP1);
                }
                break;
                case P2_ID:
                {
                    //Command to set and load selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P2_ID;
                    cmd_cb (&cmd);                                                           
                    d_screen_send_number(CONF_SEL_PEDAL2_TAG,1);
                    h_screen_send_data_to_screen(PG_CONF_PD_STEP1);
                }
                break;
                case P3_ID:
                {
                    //Command to set and load selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P3_ID;
                    cmd_cb (&cmd);                      
                    d_screen_send_number(CONF_SEL_PEDAL3_TAG,1);
                    h_screen_send_data_to_screen(PG_CONF_PD_STEP1);
                }
                break;
                case P4_ID:
                {
                    //Command to set and load selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P4_ID;
                    cmd_cb (&cmd);                      
                    d_screen_send_number(CONF_SEL_PEDAL4_TAG,1);
                    h_screen_send_data_to_screen(PG_CONF_PD_STEP1);
                }
                break;
                case P5_ID:
                {
                    //Command to set and load selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P5_ID;
                    cmd_cb (&cmd);                                         
                    d_screen_send_number(CONF_SEL_PEDAL5_TAG,1);
                    h_screen_send_data_to_screen(PG_CONF_PD_STEP1);
                }
                break;
                case P6_ID:
                {
                    //Command to set and load selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P6_ID;
                    cmd_cb (&cmd);                                            
                    d_screen_send_number(CONF_SEL_PEDAL6_TAG,1);
                    h_screen_send_data_to_screen(PG_CONF_PD_STEP1);
                }
                break;
            }        
        }
        break;
        case SCR_EV_T02_REQ_SAV_DAT:        
        {
            app_scr_cmd_t cmd;
            float         data;           
            //Assign system configuration
            cmd.val.cfg.fast_init      = (uint8_t)d_screen_get_number(FAST_INIT_GVAR);
            cmd.val.cfg.data_input     = (uint8_t)d_screen_get_number(DATA_INPUT_GVAR);
            cmd.val.cfg.select_pedal   = (uint8_t)d_screen_get_number(SEL_PEDAL_GVAR);                          
            //Command to save pedal data
            cmd.id = APP_CMD_SAV_CFG_DAT;
            cmd_cb (&cmd);                                  
            //Assign pedal data
            cmd.val.pedal.pedal_id       = cmd.val.cfg.select_pedal;
            data= d_screen_get_number(CONF_MIN_APP1_TAG,0,500);
            cmd.val.pedal.min_app1  = (data/100);
            data= d_screen_get_number(CONF_MIN_APP2_TAG,0,500);
            cmd.val.pedal.min_app2  = (data/100);
            data= d_screen_get_number(CONF_MAX_APP1_TAG,0,500);
            cmd.val.pedal.max_app1  = (data/100);      
            data= d_screen_get_number(CONF_MAX_APP2_TAG,0,500);
            cmd.val.pedal.max_app2  = (data/100); 
            data= d_screen_get_number(CONF_VREF_CAL_TAG,400,550);
            cmd.val.pedal.vref_cal  = (data/100); 
            data= d_screen_get_number(CONF_VREF_SIG_TAG,400,550);
            cmd.val.pedal.vref_sig  = (data/100); 
            String p_name= d_screen_get_string(CONF_PED_MAME_TAG);
            strcpy(cmd.val.pedal.name,p_name.c_str());
            //Command to save pedal data
            cmd.id = APP_CMD_SAV_PED_DAT;
            cmd_cb (&cmd);            
        }
        break;
        case SCR_EV_T03_REQ_STA_ACQ:        
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_STA_DAT_ACQ;
            cmd_cb(&cmd);                                
        }
        break;
        case SCR_EV_T04_REQ_STO_ACQ:        
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_STO_DAT_ACQ;
            cmd_cb(&cmd);                                
        }
        break;
        case SCR_EV_T05_REQ_VRF_DAT:
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_REQ_VRF_DAT;
            cmd_cb(&cmd);                                
        }
        break;
        case SCR_EV_T06_REQ_UPD_VRF:
        {
            app_scr_cmd_t cmd; 
            float data;
            data = d_screen_get_number(CAL1_VREF_CAL_TAG,400,550);
            cmd.val.pedal.vref_cal = (data/100);
            cmd.id = APP_CMD_REQ_UPD_VRF;
            cmd_cb(&cmd);                                
        }
        break;       
        case SCR_EV_T07_REQ_STA_CAL:
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_REQ_STA_CAL;
            cmd_cb(&cmd);                                
        }
        break;
        case SCR_EV_T08_REQ_STO_CAL:
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_REQ_STO_CAL;
            cmd_cb(&cmd);                                
        }
        break;  
        case SCR_EV_T09_REQ_PED_DAT:        
        {
            uint8_t             s_pedal;    
            app_scr_cmd_t       cmd;                
            s_pedal = (uint8_t)d_screen_get_number(SEL_PEDAL_GVAR);
            switch (s_pedal)
            {
                case P1_ID:
                {
                    //Commmand to set selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P1_ID;
                    cmd_cb(&cmd);                                       
                    d_screen_send_number(CAL3_SEL_PEDAL1_TAG,1);
                    h_screen_send_data_to_screen(PG_CALIB_PEDAL_STEP3);
                }
                break;
                case P2_ID:
                {
                    //Command to set and load selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P2_ID;
                    cmd_cb (&cmd);                                                           
                    d_screen_send_number(CAL3_SEL_PEDAL2_TAG,1);
                    h_screen_send_data_to_screen(PG_CALIB_PEDAL_STEP3);
                }
                break;
                case P3_ID:
                {
                    //Command to set and load selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P3_ID;
                    cmd_cb (&cmd);                      
                    d_screen_send_number(CAL3_SEL_PEDAL3_TAG,1);
                    h_screen_send_data_to_screen(PG_CALIB_PEDAL_STEP3);
                }
                break;
                case P4_ID:
                {
                    //Command to set and load selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P4_ID;
                    cmd_cb (&cmd);                      
                    d_screen_send_number(CAL3_SEL_PEDAL4_TAG,1);
                    h_screen_send_data_to_screen(PG_CALIB_PEDAL_STEP3);
                }
                break;
                case P5_ID:
                {
                    //Command to set and load selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P5_ID;
                    cmd_cb (&cmd);                                         
                    d_screen_send_number(CAL3_SEL_PEDAL5_TAG,1);
                    h_screen_send_data_to_screen(PG_CALIB_PEDAL_STEP3);
                }
                break;
                case P6_ID:
                {
                    //Command to set and load selected pedal
                    cmd.id = APP_CMD_SET_SEL_PEDAL;
                    cmd.val.cfg.select_pedal = P6_ID;
                    cmd_cb (&cmd);                                            
                    d_screen_send_number(CAL3_SEL_PEDAL6_TAG,1);
                    h_screen_send_data_to_screen(PG_CALIB_PEDAL_STEP3);
                }
                break;
            }        
        }
        break;
        case SCR_EV_T10_REQ_CAL_DAT:
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_REQ_CAL_DAT;
            cmd_cb(&cmd);                                
        }
        break; 
        case SCR_EV_T11_REQ_SAV_CAL:
        {
            app_scr_cmd_t cmd; 
            cmd.val.cfg.select_pedal   = (uint8_t)d_screen_get_number(SEL_PEDAL_GVAR);
            String p_name= d_screen_get_string(CAL4_PED_MAME_TAG);
            strcpy(cmd.val.pedal.name,p_name.c_str());                  
            //Assign pedal data
            cmd.val.pedal.pedal_id       = cmd.val.cfg.select_pedal;            
            cmd.id = APP_CMD_SAV_CAL_DAT;            
            cmd_cb(&cmd);                                
        }
        break; 
        case SCR_EV_T12_REQ_SOF_RES:
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_REQ_SOF_RES;
            cmd_cb(&cmd);                                
        }
        break; 
        case SCR_EV_T13_REQ_HAR_RES:
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_REQ_HAR_RES;
            cmd_cb(&cmd);                                
        }
        break; 
        case SCR_EV_T14_REQ_VAL_SET:
        {
            app_scr_cmd_t cmd; 
            cmd.val.valve.setpoint = d_screen_get_number(COM_VALVE_SET_TAG,0,1001);
            cmd.val.valve.setpoint = cmd.val.valve.setpoint/100;
            cmd.id = APP_CMD_REQ_VAL_SET;
            cmd_cb(&cmd);                                
        }
        break; 
        case SCR_EV_T15_REQ_VAL_INI:
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_REQ_INI_CON;
            cmd_cb(&cmd);                                
        }
        break; 
        case SCR_EV_T16_REQ_VAL_LOC:
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_REQ_LOC_VAL;
            cmd_cb(&cmd);                                
        }
        break; 
        case SCR_EV_T17_REQ_VAL_COM:
        {
            app_scr_cmd_t cmd; 
            cmd.id = APP_CMD_REQ_VAL_COM;
            cmd_cb(&cmd);                                
        }
        break; 
    }    

}

void h_screen_init()
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.print("H_SCREEN_INIT->");
    #endif       
    d_screen_register_event_cb(h_screen_event_handler);
    d_screen_init();
}

static void h_screen_send_data_to_screen(const uint8_t page)
{ 
    switch (page)
    {
        case PG_CONF_PD_STEP1:        
        {
            load_pedal_t lpedal;                      
            load_pedal_provider(&lpedal);            
            d_screen_send_number(CONF_MIN_APP1_TAG,(lpedal.min_app1*100));
            d_screen_send_number(CONF_MIN_APP2_TAG,(lpedal.min_app2*100));
            d_screen_send_number(CONF_MAX_APP1_TAG,(lpedal.max_app1*100));
            d_screen_send_number(CONF_MAX_APP2_TAG,(lpedal.max_app2*100));
            d_screen_send_number(CONF_VREF_CAL_TAG,(lpedal.vref_cal*100)); 
            d_screen_send_number(CONF_VREF_SIG_TAG,(lpedal.vref_sig*100)); 
            d_screen_send_string(CONF_PED_MAME_TAG,(lpedal.name));    
        }
        break;
        case PG_CALIB_PEDAL_STEP3:        
        {
            load_pedal_t lpedal;                      
            load_pedal_provider(&lpedal);  
            d_screen_send_number(CAL3_MIN_APP1_TAG,(lpedal.min_app1*100));
            d_screen_send_number(CAL3_MIN_APP2_TAG,(lpedal.min_app2*100));
            d_screen_send_number(CAL3_MAX_APP1_TAG,(lpedal.max_app1*100));
            d_screen_send_number(CAL3_MAX_APP2_TAG,(lpedal.max_app2*100));
            d_screen_send_number(CAL3_VREF_CAL_TAG,(lpedal.vref_cal*100)); 
            d_screen_send_number(CAL3_VREF_SIG_TAG,(lpedal.vref_sig*100)); 
            d_screen_send_string(CAL3_PED_MAME_TAG,(lpedal.name));    
        }
        break;
    }
}

void h_screen_send_pedal_position(load_pedal_t *lpedal)
{
    d_screen_send_number(COM_PEDAL_DAT_TAG,lpedal->pedal_pos*100);
    d_screen_send_number(COM_PEDAL_PER_TAG,(lpedal->pedal_pos));
    d_screen_send_number(COM_PEDAL_IND_TAG,true);             
}

void h_screen_send_app_voltage(float app1,float app2)
{
    d_screen_send_number(PEDAL_APP1_GVAR,app1*100);    
    d_screen_send_number(PEDAL_APP2_GVAR,app2*100); 
}
void h_screen_send_voltage_reference(const int vref)
{
    d_screen_send_number(CAL1_VREF_CAL_TAG,vref*10);  
}
void h_screen_send_calibration_state(const uint8_t state)
{
    d_screen_send_number(CAL2_CAL_STAT_TAG,state);     
}
void h_screen_valve_data(float setpoint, float pressure)
{
    d_screen_send_number(COM_VALVE_DAT_SET,setpoint*100);     
    d_screen_send_number(COM_VALVE_DAT_PRE,pressure*100);   
}

/*void h_screen_send_calibration_data(calib_pedal_t *cpedal)
{
    //Sending APP1 and APP2 Min and Max values
    d_screen_send_number (CAL2_MIN_APP1_TAG,cpedal->min_app1*100);
    d_screen_send_number (CAL2_MIN_APP2_TAG,cpedal->min_app2*100);
    d_screen_send_number (CAL2_MAX_APP1_TAG,cpedal->max_app1*100);
    d_screen_send_number (CAL2_MAX_APP2_TAG,cpedal->max_app2*100);
    d_screen_send_number (CAL2_CUR_APP1_TAG,cpedal->cur_app1*100);   
    d_screen_send_number (CAL2_CUR_APP2_TAG,cpedal->cur_app2*100);  
    d_screen_send_number (PEDAL_APP1_GVAR,cpedal->cur_app1*100);    
    d_screen_send_number (PEDAL_APP2_GVAR,cpedal->cur_app2*100);  
}*/

void h_screen_send_calibration_data(calib_pedal_t *cpedal)
{
    //Sending APP1 and APP2 Min and Max values
    d_screen_send_number (CAL4_MIN_APP1_TAG,cpedal->min_app1*100);
    d_screen_send_number (CAL4_MIN_APP2_TAG,cpedal->min_app2*100);
    d_screen_send_number (CAL4_MAX_APP1_TAG,cpedal->max_app1*100);
    d_screen_send_number (CAL4_MAX_APP2_TAG,cpedal->max_app2*100);
    d_screen_send_number (CAL4_VREF_CAL_TAG,cpedal->vref_cal*100);   
    d_screen_send_number (CAL4_VREF_SIG_TAG,cpedal->vref_cal*100);       
}

