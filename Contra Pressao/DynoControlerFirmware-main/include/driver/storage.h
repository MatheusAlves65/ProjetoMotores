#ifndef STORAGE_H
#define STORAGE_H
#include "hal/hal_storage.h"
#include "hal/hal_system.h"
#include <EEPROM.h>
#include "config.h"
void d_storage_cfg_load(int addr, system_config_t* data);
void d_storage_cfg_save(int addr, const system_config_t* data);
void d_storage_pedal_load(int addr, load_pedal_t* data);
void d_storage_pedal_save(int addr, const load_pedal_t* data);
void d_storage_setpoint_load(int addr, float* data);
void d_storage_setpoint_save(int addr, const float* data);
int  d_internal_read_voltage_reference(void);
#endif
