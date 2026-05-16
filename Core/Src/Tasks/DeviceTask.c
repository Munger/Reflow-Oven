#include "DeviceTask.h"
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

// Initialises the DeviceTask and related modules. Called by app_freertos.c at startup.
void DeviceTaskInit( void ) {
}

// Main loop for the DeviceTask.
// Called by app_freertos.c in the main task loop.
void DeviceTaskLoop( void ) {
    PMProcess();
    MCUProcess();
    BuzzerProcess();
    DCFanProcess();
    REProcess();
    TMProcess();
    TMI2CProcess();
    TCProcess();
    USBPDProcess();
}

