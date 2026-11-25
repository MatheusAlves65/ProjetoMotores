#include "app.h"


static system_config_t  cfg;
static load_pedal_t     lpedal;
static calib_pedal_t    cpedal;
app_state_t             app;

void h_system_print_system_cfg(void);
void h_system_print_pedal(void);
void fillDataEPPROM(void);


static void app_provide_cfg(system_config_t *out)
{
    out->data_input     = cfg.data_input;   
    out->fast_init      = cfg.fast_init;
    out->select_pedal   = cfg.select_pedal;
}
static void app_provide_load_pedal(load_pedal_t *out)
{
    out->min_app1 = lpedal.min_app1;
    out->min_app2 = lpedal.min_app2;
    out->max_app1 = lpedal.max_app1;
    out->max_app2 = lpedal.max_app2;
    out->vref_cal = lpedal.vref_cal;    
    out->vref_sig = lpedal.vref_sig;
    strcpy (out->name,lpedal.name);
}
static void app_screen_command(const app_scr_cmd_t *cmd)
{
    switch (cmd->id)
    {
        //Update selected pedal (Screen -> Application)
        case APP_CMD_SET_SEL_PEDAL:
        {
            cfg.select_pedal = (uint8_t) cmd->val.cfg.select_pedal;
            lpedal.pedal_id        = (uint8_t) cmd->val.cfg.select_pedal;            
            h_storage_pedal_load(&lpedal); //Load pedal data
        }        
        break;
        //Save system configuration (Screen-> Application)
        case APP_CMD_SAV_CFG_DAT:
        {
            cfg.data_input   = cmd->val.cfg.data_input;
            cfg.fast_init    = cmd->val.cfg.fast_init;
            cfg.select_pedal = cmd->val.cfg.select_pedal;  
            h_storage_cfg_save(&cfg);
            h_system_print_system_cfg();
        }        
        break;
        //Save system configuration (Screen-> Application)
        case APP_CMD_SAV_PED_DAT:
        {
            lpedal.min_app1 = cmd->val.pedal.min_app1;
            lpedal.min_app2 = cmd->val.pedal.min_app2;
            lpedal.max_app1 = cmd->val.pedal.max_app1;
            lpedal.max_app2 = cmd->val.pedal.max_app2;
            lpedal.vref_cal = cmd->val.pedal.vref_cal;
            lpedal.vref_sig = cmd->val.pedal.vref_sig;
            lpedal.pedal_id      = cmd->val.pedal.pedal_id;
            strcpy(lpedal.name,cmd->val.pedal.name);              
            h_storage_pedal_save(&lpedal);             
            h_system_print_pedal();                          
        }        
        break;
        //Start data acquisition
        case APP_CMD_STA_DAT_ACQ:
        {
            if (cfg.data_input == CLTPAD_CANBUS_INPUT)
            {
                //Start canbus thread
                h_system_can_start();
            }                     
        }        
        break;
        //Stop data acquisition
        case APP_CMD_STO_DAT_ACQ:
        {
            if (cfg.data_input == CLTPAD_CANBUS_INPUT)
            {
                //Stop canbus thread
                h_system_can_stop();
            }          
        }        
        break;
        //Reuest load Vref, App1 and App2 data
        case APP_CMD_REQ_VRF_DAT:
        {
            int ivref,fvref; 
            float app1, app2;                  
            ivref = h_internal_get_voltage_reference();
            app1  = h_gpio_get_app1_voltage(ivref);
            app2  = h_gpio_get_app2_voltage(ivref);
            fvref = (ivref/100);
            cpedal.vref_cal=fvref;                                    
            cpedal.cur_app1=app1;
            cpedal.cur_app2=app2;            
            h_screen_send_voltage_reference(ivref);            
            h_screen_send_app_voltage(cpedal.cur_app1,cpedal.cur_app2);              
        }                 
        break;
        //Request update vref
        case APP_CMD_REQ_UPD_VRF:
        {
            if (cpedal.vref_cal!=cmd->val.pedal.vref_cal)
            {
                cpedal.vref_cal = cmd->val.pedal.vref_cal;    
                float app1, app2;            
                app1= h_gpio_get_app1_voltage(cpedal.vref_cal);
                app2= h_gpio_get_app2_voltage(cpedal.vref_cal);
                cpedal.cur_app1=app1;
                cpedal.cur_app2=app2;
                h_screen_send_app_voltage(cpedal.cur_app1,cpedal.cur_app2);   
            }                                     
        }        
        break;
        //Request start calibration
        case APP_CMD_REQ_STA_CAL:
        {
            app.state = APP_STATE_CAL;            
        }                 
        break;
        //Request stop calibration
        case APP_CMD_REQ_STO_CAL:
        {
            app.state = APP_STATE_INI;            
        }        
        break;   
        //Request calibration data
        case APP_CMD_REQ_CAL_DAT:
        {
            //h_screensend_pedal_position
            h_screen_send_calibration_data(&cpedal);                        
        }        
        break;
        //Save calibration pedal (Screen-> Application)
        case APP_CMD_SAV_CAL_DAT:
        {
            lpedal.pedal_id = cmd->val.pedal.pedal_id;
            lpedal.min_app1 = cpedal.min_app1;
            lpedal.min_app2 = cpedal.min_app2;
            lpedal.max_app1 = cpedal.max_app1;
            lpedal.max_app2 = cpedal.max_app2;
            lpedal.vref_cal = cpedal.vref_cal;
            lpedal.vref_sig = cpedal.vref_cal;            
            strcpy(lpedal.name,cmd->val.pedal.name);              
            h_storage_pedal_save(&lpedal);                         
        }  
        //Request stop calibration
        case APP_CMD_REQ_SOF_RES:
        {      
            h_system_reboot();
        }        
        //Request stop calibration
        case APP_CMD_REQ_HAR_RES:
        {
            app_factory_reset();
            delay(100);
            h_system_reboot();
        }        
        break;  
        //New Valve Setpoint
        case APP_CMD_REQ_VAL_SET:
        {
           float setpoint = cmd->val.valve.setpoint;
           h_storage_setpoint_save(&setpoint);
           h_gpio_setpoint(cmd->val.valve.setpoint);
        }        
        break;  
        case APP_CMD_REQ_INI_CON:
        {
           h_gpio_init_contr();
        }        
        break; 
        case APP_CMD_REQ_LOC_VAL:
        {
           h_gpio_lock_contr();
        }        
        break; 
        case APP_CMD_REQ_VAL_COM:
        {
           h_gpio_change_inf_status();
        }        
        break; 
    }
   
}

static void app_can_command(const app_can_cmd_t *cmd)
{
    switch (cmd->id)
    {
        case APP_CAN_CMD_SET_PEDAL_POS:
        {
            if (cfg.data_input==CAN_DT_IN)
            {
                lpedal.pedal_pos = cmd->val;
                h_screen_send_pedal_position(&lpedal);
                //h_gpio_handler_send_voltage(&lpedal);
            }
        }
        break;
        case APP_CAN_CMD_SET_RPM:
        {
            if (cmd->val==CTLPAD_RPM_REF)
            {
                //Serial.println("CAn alive");                
            }           
        }
        break;
    }
}

static void app_gpio_command(const app_gpio_cmd_t *cmd)
{
    switch (cmd->id)
    {
        case APP_GPIO_CMD_SET_MAN_INPUT:
        {
            if (cfg.data_input==MANUAL_DT_IN)
            {
                if (lpedal.pedal_pos!=cmd->epos)
                {
                    lpedal.pedal_pos = cmd->epos;
                    //lpedal.cur_app1 = cmd->app1;
                    //lpedal.cur_app2 = cmd->app2;
                    h_screen_send_pedal_position(&lpedal);
                    //h_gpio_handler_send_voltage(&lpedal);
                    //h_screen_send_app_voltage(lpedal.cur_app1,lpedal.cur_app2);                                                   
                }               
            }  
        }
        break;
        case APP_GPIO_CMD_SET_AN_INPUT:
        {
            if (cfg.data_input==ANALOG_DT_IN)
            {
                if (lpedal.pedal_pos!=cmd->anin)
                {
                    lpedal.pedal_pos = cmd->anin;
                    h_screen_send_pedal_position(&lpedal);      
                    //h_gpio_handler_send_voltage(&lpedal);                          
                }
            }                 
        }
        break;
        case APP_GPIO_CMD_SET_PEDAL_REL:
        {
            h_gpio_reset_encoder();            
        }
        break;       
        case APP_GPIO_CMD_SET_APP_DAT:
        {
            float diff1,diff2;
            if (app.state==APP_STATE_CAL) 
            {
                diff1 = abs(cpedal.cur_app1-cmd->app1);                
                diff2 = abs(cpedal.cur_app2-cmd->app2);                
                if (diff1>0.01f || diff2>0.01f)
                {
                    cpedal.cur_app1 = cmd->app1;                    
                    cpedal.cur_app2 = cmd->app2;                    
                    h_screen_send_app_voltage(cpedal.cur_app1,cpedal.cur_app2);                                 
                }               
            }
            else
            {
                //float min_p1 = lpedal.min_app1*(4096/lpedal.vref_cal);
                //float max_p1 = lpedal.max_app1*(4096/lpedal.vref_cal);
                //float min_p2 = lpedal.min_app2*(4096/lpedal.vref_cal);
                //float max_p2 = lpedal.max_app2*(4096/lpedal.vref_cal);                
                //float app1 = map(lpedal.pedal_pos,0,100,min_p1,max_p1);
                //float app2 = map(lpedal.pedal_pos,0,100,min_p2,max_p2);
                float app1=map2(lpedal.pedal_pos, MIN_ENCODER_POSITION,MAX_ENCODER_POSITION,lpedal.min_app1,lpedal.max_app1);
                float app2=map2(lpedal.pedal_pos, MIN_ENCODER_POSITION,MAX_ENCODER_POSITION,lpedal.min_app2,lpedal.max_app2);
                /*Serial.print("app1: ");
                Serial.print(app1);
                Serial.print(",app2: ");
                Serial.println(app2);
                static boolean init = 0;
                if (init == 0){
                    lpedal.cur_app1 = 0;
                    lpedal.cur_app2 = 0;
                    init = 1;
                }*/
                if (isnan(lpedal.cur_app1)) lpedal.cur_app1 = app1;
                if (isnan(lpedal.cur_app2)) lpedal.cur_app2 = app2;

                diff1 = abs(lpedal.cur_app1-app1);                
                diff2 = abs(lpedal.cur_app2-app2);     
                /*Serial.print("diff1: ");
                Serial.print(diff1);
                Serial.print(",diff2: ");
                Serial.println(diff2);*/
                           
                if (diff1>0.01f || diff2>0.01f)
                {
                    lpedal.cur_app1 = app1;                    
                    lpedal.cur_app2 = app2;                    
                    h_screen_send_app_voltage(lpedal.cur_app1,lpedal.cur_app2);   
                    h_gpio_handler_send_voltage(&lpedal);                             
                }         
            }
        }
        break;                     
    }
}

void app_init(void)
{
    //1 - Load system configuration
    h_storage_cfg_load(&cfg);
    h_system_print_system_cfg();    
    //2 - Load pedal configuration
    h_storage_pedal_load(&lpedal);  
    //h_system_print_pedal();
    //3 - Load Valve configuration
    float setpoint;
    h_storage_setpoint_load(&setpoint);
    h_gpio_setpoint(setpoint);
    
    //Screen provider callbacks
    h_screen_register_cfg_provider(app_provide_cfg);
    h_screen_register_lpedal_provider(app_provide_load_pedal); 
    
    //Screen command callback
    h_screen_register_cmd_callback(app_screen_command);
    //CAN command callback
    h_can_register_cmd_callback(app_can_command);
    //GPIO command callback
    h_gpio_register_cmd_callback(app_gpio_command);

    //fillDataEPPROM();

    //Just for test
    //cfg.data_input=0;
    //cfg.fast_init =0;
} 

void app_update(void)
{
    uint32_t now            = h_system_time_ms();
    app.timers.now_ms       = now;

    if(app.flags.pedal_press)
    {
        app.timers.ped_press_elapsed_ms = now - app.timers.ped_press_evt_ms;                   
    }
    else
    {
        app.timers.ped_press_elapsed_ms=0;
    }     
    if(app.flags.pedal_hold)
    {
        app.timers.ped_hold_elapsed_ms = now - app.timers.ped_hold_evt_ms;
    }
    else
    {
        app.timers.ped_hold_elapsed_ms=0;
    }   
}
void app_run(void)
{   
    switch(app.state)
    {
        case APP_STATE_INI:
        {
            //1 - Check fast initialization                        
            if (cfg.fast_init==true)
            {
                app.state = APP_STATE_RUN;
            }
        }        
        break;
        case APP_STATE_RUN:
        {
            
        }
        break; 
        case APP_STATE_CAL:
        {
            switch (app.c_state)
            {
                //1 - Start calibration process. Waiting pedal press event
                case APP_CALIB_STATE_S0:
                {
                    //Start press the acceleration pedal
                    if(abs(cpedal.cur_app1-cpedal.min_app1)>0.5f)
                    {
                        app.c_state=APP_CALIB_STATE_S1; //Trigger to state 1      
                        h_screen_send_calibration_state(app.c_state);                 
                        app.flags.pedal_press=true;  
                        app.timers.ped_press_evt_ms = h_system_time_ms(); //Timer press started             
                    }
                }
                break;    
                //2 - Pedal press variation. Waiting until WOT.
                case APP_CALIB_STATE_S1:
                {  
                    //Ckeck each 1000ms
                    if (app.timers.ped_press_elapsed_ms>2500)
                    {
                        if(abs(cpedal.cur_app1-cpedal.max_app1)<0.5f)
                        {
                            app.c_state=APP_CALIB_STATE_S2; //Trigger to state 2
                            app.timers.ped_hold_evt_ms = h_system_time_ms();
                            app.flags.pedal_press=false;
                            app.flags.pedal_hold=true;    
                            h_screen_send_calibration_state(app.c_state);                                       
                        }
                        //Restart Watchdog timer
                        app.timers.ped_press_evt_ms=h_system_time_ms();
                    } 
                    else //Update max app1 and max app2
                    {                       
                        cpedal.max_app1=cpedal.cur_app1;
                        cpedal.max_app2=cpedal.cur_app2;                     
                    }                   
                }
                break;
                //3 - Waiting pedal hold time - WOT            
                case APP_CALIB_STATE_S2:
                {  
                    //Ckeck each 4000ms
                    if (app.timers.ped_hold_elapsed_ms > 4000)
                    { 
                        app.c_state=APP_CALIB_STATE_S3; //Trigger to state 3 
                        app.timers.ped_press_evt_ms=h_system_time_ms();
                        app.flags.pedal_hold    =false;
                        app.flags.pedal_press   =true;  
                        h_screen_send_calibration_state(app.c_state);                                                                              
                    }                    
                }
                break;
                //4 - Releasing pedal            
                case APP_CALIB_STATE_S3:
                {  
                    //Ckeck each 2000ms
                    if (app.timers.ped_press_elapsed_ms > 2500)
                    {
                        if(abs(cpedal.cur_app1-cpedal.min_app1)<0.5f)
                        {
                            app.c_state=APP_CALIB_STATE_S4; //Trigger to state 4
                            app.timers.ped_hold_evt_ms = h_system_time_ms();
                            app.flags.pedal_press=false;
                            app.flags.pedal_hold=true;    
                            cpedal.min_app1=cpedal.cur_app1;
                            cpedal.min_app2=cpedal.cur_app2;   
                            h_screen_send_calibration_state(app.c_state);                    
                        }
                        //Restart Watchdog timer
                        app.timers.ped_press_evt_ms=h_system_time_ms();
                    }                     
                }
                break;
                //5 - Waiting pedal hold time - CT            
                case APP_CALIB_STATE_S4:
                {  
                    //Ckeck each 4000ms
                    if (app.timers.ped_hold_elapsed_ms > 4000)
                    { 
                        app.c_state=APP_CALIB_STATE_S0; //Trigger to state 0 
                        app.flags.pedal_hold    =false;
                        app.flags.pedal_press   =false;   
                        
                        cpedal.cs_min_app1=cpedal.cs_min_app1+cpedal.min_app1;                             
                        cpedal.cs_min_app2=cpedal.cs_min_app2+cpedal.min_app2;   
                        cpedal.cs_max_app1=cpedal.cs_max_app1+cpedal.max_app1;                       
                        cpedal.cs_max_app2=cpedal.cs_max_app2+cpedal.max_app2;

                        cpedal.cumsum++;
                        if (cpedal.cumsum>=CLTPAD_MAX_CAL_ITR)                      
                        {
                            cpedal.min_app1 = (cpedal.cs_min_app1/cpedal.cumsum);
                            cpedal.min_app2 = (cpedal.cs_min_app2/cpedal.cumsum);
                            cpedal.max_app1 = (cpedal.cs_max_app1/cpedal.cumsum);
                            cpedal.max_app2 = (cpedal.cs_max_app2/cpedal.cumsum);
                            app.c_state=APP_CALIB_STATE_S5; 
                        }
                        else
                        {
                            app.c_state=APP_CALIB_STATE_S0;                             
                        }
                        h_screen_send_calibration_state(app.c_state);                           
                    }
                }
                break;
            }    
        }
        break;
    }

}
void app_factory_reset(void)
{
  //Data to populate EEPROM for the first time
  cfg.select_pedal=0;
  cfg.fast_init=true;
  cfg.data_input=2;
  h_storage_cfg_save(&cfg); 

  //Data for pedal
  lpedal.pedal_id=0;
  lpedal.min_app1=0.5;
  lpedal.min_app2=0.5;
  lpedal.max_app1=4.5;
  lpedal.max_app2=4.5;
  lpedal.vref_cal=5.0;
  lpedal.vref_sig=5.0;
  strcpy(lpedal.name, String("Pedal 1").c_str());
  h_storage_pedal_save(&lpedal); 
 
  lpedal.pedal_id=1;
  lpedal.min_app1=0.5;
  lpedal.min_app2=0.5;
  lpedal.max_app1=4.5;
  lpedal.max_app2=4.5;
  lpedal.vref_cal=5.0;
  lpedal.vref_sig=5.0;
  strcpy(lpedal.name, String("Pedal 2").c_str());
  h_storage_pedal_save(&lpedal); 

  lpedal.pedal_id=2;
  lpedal.min_app1=0.5;
  lpedal.min_app2=0.5;
  lpedal.max_app1=4.5;
  lpedal.max_app2=4.5;
  lpedal.vref_cal=5.0;
  lpedal.vref_sig=5.0;
  strcpy(lpedal.name, String("Pedal 3").c_str());
  h_storage_pedal_save(&lpedal); 

  lpedal.pedal_id=3;
  lpedal.min_app1=0.5;
  lpedal.min_app2=0.5;
  lpedal.max_app1=4.5;
  lpedal.max_app2=4.5;
  lpedal.vref_cal=5.0;
  lpedal.vref_sig=5.0;
  strcpy(lpedal.name, String("Pedal 4").c_str());
  h_storage_pedal_save(&lpedal);  

  lpedal.pedal_id=4;
  lpedal.min_app1=0.5;
  lpedal.min_app2=0.5;
  lpedal.max_app1=4.5;
  lpedal.max_app2=4.5;
  lpedal.vref_cal=5.0;
  lpedal.vref_sig=5.0;
  strcpy(lpedal.name, String("Pedal 5").c_str());
  h_storage_pedal_save(&lpedal); 

  lpedal.pedal_id=5;
  lpedal.min_app1=0.5;
  lpedal.min_app2=0.5;
  lpedal.max_app1=4.5;
  lpedal.max_app2=4.5;
  lpedal.vref_cal=5.0;
  lpedal.vref_sig=5.0;
  strcpy(lpedal.name, String("Pedal 6").c_str());
  h_storage_pedal_save(&lpedal);  
}

void app_process(void)
{
    app_update();
    app_run();
}










