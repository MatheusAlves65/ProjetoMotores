#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H


#include "config.h"
#include "system.h"
#include "hal/hal_system.h"
#include "driver/storage.h"

void    h_storage_cfg_load(system_config_t* cfg);
void    h_storage_cfg_save(system_config_t* cfg);
uint8_t h_storage_pedal_load(load_pedal_t* lpedal);
uint8_t h_storage_pedal_load(uint8_t pid, load_pedal_t* lpedal);
uint8_t h_storage_pedal_save(load_pedal_t* lpedal);
uint8_t h_storage_setpoint_load(float* setpoint);
uint8_t h_storage_setpoint_save(float* setpoint);
int     h_internal_get_voltage_reference(void);
#endif
