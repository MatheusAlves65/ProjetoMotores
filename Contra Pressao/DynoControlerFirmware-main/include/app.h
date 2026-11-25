#ifndef APP_H
#define APP_H
#include <Arduino.h>
#include "system.h"
#include "hal/hal_storage.h"
#include "hal/hal_screen.h"
#include "hal/hal_can.h"
#include "hal/hal_gpio.h"


void app_init(void);
void app_process(void);
typedef void (*app_cfg_provider_t)    (system_config_t *out);
typedef void (*app_lpedal_provider_t) (load_pedal_t *out);
void app_factory_reset(void);
#endif
