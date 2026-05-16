#include "ManagerTask.h"
#include "interrupts.h"
#include "buzzer.h"
#include "dcfan.h"
#include "mcu.h"
#include "powermanager.h"
#include "rotaryencoder.h"
#include "thermistor.h"
#include "thermistori2c.h"
#include "thermocouple.h"
#include "usbpowerdelivery.h"

// Initialises the SystemInitTask and related modules. Called by app_freertos.c at startup.
void ManagerTaskInit( void ) {
    PMInitModule();
    MCUInitModule();
    BuzzerInitModule();
    DCFanInitModule();
    REInitModule();
    TMInitModule();
    TMI2CInitModule();
    TCInitModule();
    USBPDInitModule();

    EnableDriverInterrupts();
}

// Main loop for the SystemInitTask.
// Called by app_freertos.c in the main task loop.
void ManagerTaskLoop( void ) {
}

