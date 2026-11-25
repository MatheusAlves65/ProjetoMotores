#ifndef CAN_H
#define CAN_H

#include <Arduino.h>
#include <mcp_can.h>
#include "config.h"
#include "system.h"
#include "hal/hal_can.h"

typedef void (*d_can_rx_cb_t)(const can_frame_t *frame);
typedef void (*d_can_tx_cb_t)(void);

bool d_can_send(can_frame_t *frame);
//void d_can_register_tx_callback(d_can_tx_cb_t cb);
void d_can_register_rx_callback(d_can_rx_cb_t cb);
void d_can_init(void);
boolean d_can_receive(can_frame_t *frame);
void d_can_start(void);
void d_can_stop(void);
void d_can_set_loopback(void);
void d_can_set_normal(void);

#endif