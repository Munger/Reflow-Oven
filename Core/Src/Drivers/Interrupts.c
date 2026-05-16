/// @file Interrupts.c
///
/// @brief GPIO EXTI interrupt dispatcher.
///
/// Implements HAL_GPIO_EXTI_Rising_Callback and HAL_GPIO_EXTI_Falling_Callback.
/// Each callback iterates a static pin-to-handler table and forwards the event
/// to the appropriate driver handler only when FlagInterruptsEnabled is set in
/// SystemStatusFlagsHandle. All handlers in the table must conform to the
/// ISR contract: no FreeRTOS blocking APIs, no direct SPI/I2C, volatile-flag
/// or event-flag set only.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "main.h"
#include "Platform.h"
#include "SystemStatusFlags.h"
#include "event_groups.h"
#include "PowerManager.h"
#include "Thermocouple.h"
#include "USBPowerDelivery.h"
#include "Triac.h"

/// @brief Function pointer type for GPIO edge interrupt handlers.
/// @param[in] GPIO_Pin The HAL pin bitmask that triggered the interrupt.
typedef void (*EdgeInterruptHandler)( uint16_t GPIO_Pin );

/// @brief Edge polarity that a table entry responds to.
typedef enum {
    RisingEdge,   ///< Triggered on a low-to-high GPIO transition
    FallingEdge   ///< Triggered on a high-to-low GPIO transition
} EdgeType;

/// @brief Static pin-to-handler dispatch table.
///
/// Each entry maps a GPIO pin bitmask and edge polarity to a handler function.
/// Multiple entries with the same pin are supported (e.g. ZCD is dispatched to
/// both the TRIAC sequencer and the power manager).
///
/// @note Only entries matching the edge direction of the current callback are invoked.
static struct {
    uint16_t             pin;
    EdgeType             type;
    EdgeInterruptHandler handler;
} interruptHandlers[] = {
    { ZCD_Pin,          RisingEdge,  ZCDHandler                },  ///< TRIAC zero-cross sequencer
    { ZCD_Pin,          RisingEdge,  PMHandleZCDInterrupt      },  ///< Power manager AC-live watchdog
    { ESTOP_Pin,        RisingEdge,  PMHandleEStopInterrupt    },  ///< Emergency stop
    { THERM1_DRDY_Pin,  RisingEdge,  TCHandleDRDYInterrupt     },  ///< TC1 data-ready
    { THERM2_DRDY_Pin,  RisingEdge,  TCHandleDRDYInterrupt     },  ///< TC2 data-ready
    { THERM1_FAULT_Pin, RisingEdge,  TCHandleFaultInterrupt    },  ///< TC1 fault assertion
    { THERM2_FAULT_Pin, RisingEdge,  TCHandleFaultInterrupt    },  ///< TC2 fault assertion
    { FLGN_Pin,         FallingEdge, USBPDHandleFLGNInterrupt  },  ///< TCPP03 FLAG_N (active low)
    { PD_SRC_INT_Pin,   FallingEdge, USBPDHandleSourceInterrupt}   ///< STPD01 source interrupt
};

/// @brief HAL rising-edge EXTI callback — dispatches to registered rising-edge handlers.
///
/// Guards all dispatch behind FlagInterruptsEnabled to prevent premature
/// interrupts before the system has finished initialising.
///
/// @param[in] GPIO_Pin HAL pin bitmask for the GPIO that transitioned high.
/// @warning ISR context — do not call any FreeRTOS blocking API from this function
///          or from any handler it invokes.
void HAL_GPIO_EXTI_Rising_Callback( uint16_t GPIO_Pin ) {
    if ( xEventGroupGetBitsFromISR( (EventGroupHandle_t)SystemStatusFlagsHandle ) & BIT( FlagInterruptsEnabled ) ) {
        for ( uint8_t i = 0; i < sizeof( interruptHandlers ) / sizeof( interruptHandlers[ 0 ] ); i++ ) {
            if ( GPIO_Pin == interruptHandlers[ i ].pin && interruptHandlers[ i ].type == RisingEdge ) {
                interruptHandlers[ i ].handler( GPIO_Pin );
            }
        }
    }
}

/// @brief HAL falling-edge EXTI callback — dispatches to registered falling-edge handlers.
///
/// Guards all dispatch behind FlagInterruptsEnabled to prevent premature
/// interrupts before the system has finished initialising.
///
/// @param[in] GPIO_Pin HAL pin bitmask for the GPIO that transitioned low.
/// @warning ISR context — do not call any FreeRTOS blocking API from this function
///          or from any handler it invokes.
void HAL_GPIO_EXTI_Falling_Callback( uint16_t GPIO_Pin ) {
    if ( xEventGroupGetBitsFromISR( (EventGroupHandle_t)SystemStatusFlagsHandle ) & BIT( FlagInterruptsEnabled ) ) {
        for ( uint8_t i = 0; i < sizeof( interruptHandlers ) / sizeof( interruptHandlers[ 0 ] ); i++ ) {
            if ( GPIO_Pin == interruptHandlers[ i ].pin && interruptHandlers[ i ].type == FallingEdge ) {
                interruptHandlers[ i ].handler( GPIO_Pin );
            }
        }
    }
}
