#ifndef HAL_SYSTEM_H
#define HAL_SYSTEM_H

#include<Arduino.h>
#include <ThreadController.h>
#include "hal/hal_can.h"
#include "hal/hal_storage.h"
#include "hal/hal_screen.h"
#include "hal/hal_gpio.h"
#include "app.h"

const   system_config_t *h_system_cfg_get(void);
const   load_pedal_t    *h_system_pedal_get(void);

void        h_system_init(void);
void        h_system_loop(void);
uint32_t    h_system_time_ms(void);
void        h_system_can_start(void);
void        h_system_can_stop(void);
void        h_system_reboot(void);
#endif