#ifndef SCREEN_H
#define SCREEN_H
#include "hal/hal_screen.h"
#include "EasyNextionLibrary.h"
#include "driver/storage.h"
#include "config.h"
#include "system.h"

typedef void    (*d_screen_event_cb_t)(screen_event_t event, uint8_t error);
void            d_screen_init(void);
void            d_screen_register_event_cb(d_screen_event_cb_t cb);
uint32_t        d_screen_get_number(const char *tag);
uint32_t        d_screen_get_number(const char *tag, const int16_t minVal, const int16_t maxVal);
String          d_screen_get_string(const char *tag);
void            d_screen_send_number(const char *tag, const uint32_t number);
void            d_screen_send_string(const char *tag,const char *cmd);
void            d_screen_loop(void);
int             d_screen_get_last_page_id (void);
#endif