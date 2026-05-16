#include <stdbool.h>
#include "main.h"
#include "platform.h"
#include "interrupts.h"
#include "powermanager.h"
#include "thermocouple.h"
#include "usbpowerdelivery.h"
#include "triac.h"

static volatile bool driverInterruptsEnabled = false;

typedef void (*EdgeInterruptHandler)( uint16_t GPIO_Pin );

typedef enum {
    RisingEdge,
    FallingEdge
} EdgeType;

static struct {
    uint16_t             pin;
    EdgeType             type;
    EdgeInterruptHandler handler;
} interruptHandlers[] = {
    { ZCD_Pin, RisingEdge, ZCDHandler },
    { ZCD_Pin, RisingEdge, PMHandleZCDInterrupt },
    { ESTOP_Pin, RisingEdge, PMHandleEStopInterrupt },
    { THERM1_DRDY_Pin, RisingEdge, TCHandleDRDYInterrupt },
    { THERM2_DRDY_Pin, RisingEdge, TCHandleDRDYInterrupt },
    { THERM1_FAULT_Pin, RisingEdge, TCHandleFaultInterrupt },
    { THERM2_FAULT_Pin, RisingEdge, TCHandleFaultInterrupt },
    { FLGN_Pin, FallingEdge, USBPDHandleFLGNInterrupt },
    { PD_SRC_INT_Pin, FallingEdge, USBPDHandleSourceInterrupt }
};

void EnableDriverInterrupts( void ) {
    driverInterruptsEnabled = true;
}

void DisableDriverInterrupts( void ) {
    driverInterruptsEnabled = false;
}

bool DriverInterruptsEnabled( void ) {
    return driverInterruptsEnabled;
}

void HAL_GPIO_EXTI_Rising_Callback( uint16_t GPIO_Pin ) {

    if ( driverInterruptsEnabled ) {
        for ( uint8_t i = 0; i < sizeof( interruptHandlers ) / sizeof( interruptHandlers[ 0 ] ); i++ ) {
            if ( GPIO_Pin == interruptHandlers[ i ].pin && interruptHandlers[ i ].type == RisingEdge ) {
                interruptHandlers[ i ].handler( GPIO_Pin );
            }
        }
    }
}

void HAL_GPIO_EXTI_Falling_Callback( uint16_t GPIO_Pin ) {

    if ( driverInterruptsEnabled ) {
        for ( uint8_t i = 0; i < sizeof( interruptHandlers ) / sizeof( interruptHandlers[ 0 ] ); i++ ) {
            if ( GPIO_Pin == interruptHandlers[ i ].pin && interruptHandlers[ i ].type == FallingEdge ) {
                interruptHandlers[ i ].handler( GPIO_Pin );
            }
        }
    }
}
