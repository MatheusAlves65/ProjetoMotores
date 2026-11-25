
#include "hal/hal_storage.h"

void h_storage_cfg_load(system_config_t* cfg)
{
  d_storage_cfg_load(EEPROM_CFG_ADDR,cfg);
}
void h_storage_cfg_save(system_config_t* cfg)
{
  d_storage_cfg_save(EEPROM_CFG_ADDR,cfg);  
}
uint8_t h_storage_pedal_load(load_pedal_t* pedal)
{
   //Check pid
  if(pedal->pedal_id < MIN_PEDAL_ID || pedal->pedal_id > MAX_PEDAL_ID)
  {
    return 1;
  }  
  int addr = EEPROM_PEDAL_ADDR+(pedal->pedal_id*(sizeof(load_pedal_t)+1));
  d_storage_pedal_load(addr,pedal); 
  return 0; 
}
uint8_t h_storage_pedal_load(uint8_t pid, load_pedal_t* pedal)
{
   //Check pid
  if(pid < MIN_PEDAL_ID || pid> MAX_PEDAL_ID)
  {
    return 1;
  }  
  int addr = EEPROM_PEDAL_ADDR+(pid*(sizeof(load_pedal_t)+1));
  d_storage_pedal_load(addr,pedal);  
  return 0;
}
uint8_t h_storage_pedal_save(load_pedal_t* pedal)
{
  if (pedal->pedal_id< MIN_PEDAL_ID || pedal->pedal_id> MAX_PEDAL_ID)
  {
    return 1;
  }  
  int addr=EEPROM_PEDAL_ADDR+((pedal->pedal_id)*(sizeof(load_pedal_t)+1));
  d_storage_pedal_save(addr,pedal);
  return 0;  
}
uint8_t h_storage_setpoint_load(float* setpoint)
{
  int addr = EEPROM_SETPOINT_ADDR;
  d_storage_setpoint_load(addr,setpoint);  
  return 0;
}
uint8_t h_storage_setpoint_save(float* setpoint)
{
  int addr=EEPROM_SETPOINT_ADDR;
  d_storage_setpoint_save(addr,setpoint);
  return 0;  
}
int h_internal_get_voltage_reference(void)
{
  return d_internal_read_voltage_reference(); 
}