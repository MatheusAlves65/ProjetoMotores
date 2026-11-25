#include "hal/hal_system.h"

static ThreadController controller;
static Thread screenThread,canThread,appThread,gpioThread;

void h_system_init(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.begin(9600);    
    dbSerial.print("H_SYSTEM_INIT->");
    #endif     
    //Serial.begin(9600);
    //Initialing modules
    h_can_init();
    h_screen_init();
    h_gpio_init();   
    app_init();

    gpioThread.enabled=true;
    gpioThread.onRun(h_gpio_process);
    gpioThread.setInterval(THREAD_INTERVAL_50MS);

    appThread.enabled=true;
    appThread.onRun(app_process);
    appThread.setInterval(THREAD_INTERVAL_50MS);

    screenThread.enabled=true;
    screenThread.onRun(h_screen_process);
    screenThread.setInterval(THREAD_INTERVAL_50MS);

    canThread.enabled=false;
    canThread.onRun(h_can_process);
    canThread.setInterval(THREAD_INTERVAL_250MS); 

    controller.add(&gpioThread);
    controller.add(&appThread);
    controller.add(&canThread);
    controller.add(&screenThread);
}
void h_system_loop()
{
    controller.run();
}

void h_system_can_start(void)
{
    canThread.enabled=true;
}
void h_system_can_stop(void)
{
    canThread.enabled=false;
}
uint32_t h_system_time_ms(void)
{
    return millis();   
}

void h_system_reboot(void)
{
    asm volatile (" jmp 0");
}

void h_system_print_system_cfg(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.print("Control Pad: ");
    dbSerial.print("Fast Init= ");
    dbSerial.print(cfg.fast_init);
    dbSerial.print("\tData input= ");
    dbSerial.print(cfg.data_input);
    dbSerial.print("\tSelect Pedal= ");
    dbSerial.println(cfg.select_pedal);
    #endif
}

void h_system_print_pedal(void)
{
    #ifdef DEBUG_SERIAL_ENABLE
    dbSerial.print("Pedal: ");
    dbSerial.print("id= ");
    dbSerial.print(lpedal.id);
    dbSerial.print("\tname= ");
    dbSerial.print(lpedal.name);
    dbSerial.print("\tMinAPP1= ");
    dbSerial.print(lpedal.minAPP1);
    dbSerial.print("\tMinAPP2= ");
    dbSerial.print(lpedal.minAPP2);
    dbSerial.print("\tMaxAPP1= ");
    dbSerial.print(lpedal.maxAPP1);
    dbSerial.print("\tMaxAPP2= ");
    dbSerial.print(lpedal.maxAPP2);
    dbSerial.print("\tVrefCal= ");
    dbSerial.print(lpedal.vRefCal);
    dbSerial.print("\tVrefSig= ");
    dbSerial.println(lpedal.vRefSig);   
    #endif 
}






