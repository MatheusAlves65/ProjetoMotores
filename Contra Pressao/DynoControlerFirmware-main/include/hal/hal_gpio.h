#ifndef HAL_ANALOG_H
#define HAL_ANALOG_H
#include <Arduino.h>
#include "driver/gpio.h"
#include "hal/hal_system.h"
#include "config.h"
#include "system.h"

int     h_input_get_internal_voltage (void);
void    h_gpio_init(void);
void    h_gpio_process(void);
typedef void (*h_gpio_read_cb_t)(float value);
typedef void (*h_gpio_cmd_cb_t)(const app_gpio_cmd_t *cmd);
void    h_gpio_register_cmd_callback(h_gpio_cmd_cb_t cb);
void    h_gpio_reset_encoder(void);
float   h_gpio_get_app1_voltage(const uint16_t vref);
float   h_gpio_get_app2_voltage(const uint16_t vref);
void    h_gpio_handler_send_voltage(load_pedal_t *lpedal);
void    h_gpio_setpoint(float input);
void    h_gpio_init_contr();
void    h_gpio_lock_contr();
void    h_gpio_change_inf_status();
float map2(float x, float in_min, float in_max, float out_min, float out_max);


#define MAX_ENCODER_POSITION 100
#define MIN_ENCODER_POSITION 0

#endif