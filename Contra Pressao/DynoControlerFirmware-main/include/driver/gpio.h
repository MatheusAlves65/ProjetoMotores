#ifndef ANALOG_H
#define ANALOG_H
#include <Arduino.h>
#include <Wire.h>   
#include "config.h"
#include "system.h"

int     d_gpio_get_internal_voltage(void);
void    d_gpio_read_raw(const uint8_t channel, uint16_t *raw);    
void    d_gpio_read_analog_input(uint16_t *raw);
void    d_gpio_init(void);
void    d_gpio_read_enc(encoder_t *enc);
void    d_gpio_write_enc(const encoder_t *enc);
void    d_gpio_read_pedal_app1(uint16_t *raw);
void    d_gpio_read_pedal_app2(uint16_t *raw);
void    d_gpio_send_DAC_voltage(int direccionBus, uint16_t valueMCP4725);
void    d_gpio_set_valve(int dir, int pwmVal);
void    d_gpio_read_press(uint16_t *pressure_raw);

#define D_GPIO_ANALOG_CH   0
#define D_GPIO_DIGITAL_CH  1

#endif