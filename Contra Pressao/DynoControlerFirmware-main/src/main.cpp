#include <Arduino.h>
#include "hal/hal_system.h"

void setup(void)
{    
   h_system_init();
}

void loop(void)
{
    h_system_loop();     
}

