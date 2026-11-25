#ifndef HAL_CAN_H
#define HAL_CAN_H

#include <Arduino.h>
#include <Thread.h>
#include <mcp_can.h>
#include "driver/can.h"
#include "system.h"
#include "config.h"

void        h_can_init(void);
void        h_can_start(void);
void        h_can_stop(void);

void        h_can_process(void);
typedef void (*h_can_cmd_cb_t)(const app_can_cmd_t *cmd);
void h_can_register_cmd_callback(h_can_cmd_cb_t cb);


#endif