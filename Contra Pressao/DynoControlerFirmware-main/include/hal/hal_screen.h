#ifndef HAL_SCREEN_H
#define HAL_SCREEN_H
#include "config.h"
#include "system.h"
#include "driver/screen.h"

void h_screen_init(void);
void h_screen_process(void);
typedef void (*app_cfg_provider_t)  (system_config_t *out);
typedef void (*app_lpedal_provider_t)(load_pedal_t *out);
typedef void (*h_screen_cmd_cb_t) (const app_scr_cmd_t *cmd);
void h_screen_register_cfg_provider(app_cfg_provider_t cb);
void h_screen_register_lpedal_provider(app_lpedal_provider_t cb);
void h_screen_register_cmd_callback(h_screen_cmd_cb_t cb);
void h_screen_send_pedal_position(load_pedal_t *pedal);
void h_screen_send_voltage_reference(const int vref);
void h_screen_send_app_voltage(float app1,float app2);
void h_screen_send_calibration_data (calib_pedal_t *pedal);
void h_screen_send_calibration_state(const uint8_t state);
void h_screen_send_calibration_scale(const float scale);
void h_screen_valve_data(float setpoint, float pressure);



#endif