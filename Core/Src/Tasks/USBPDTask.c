#include "USBPDTask.h"
#include "usbpowerdelivery.h"


// Initialises the USBPDTask and related modules. Called by app_freertos.c at startup.
void USBPDTaskInit( void ) {
    USBPDInitModule();
}

// Main loop for the USBPDTask.
// Called by app_freertos.c in the main task loop.
void USBPDTaskLoop( void ) {
    // Poll the MAINS_PWR_N pin and manage roles
    USBPDProcess();
    osDelay( 10 );
}

