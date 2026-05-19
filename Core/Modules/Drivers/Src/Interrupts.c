/// @file Interrupts.c
///
/// @brief GPIO EXTI interrupt dispatcher and system software interrupt bus.
///
/// GPIO: HAL_GPIO_EXTI_Rising_Callback and HAL_GPIO_EXTI_Falling_Callback iterate
/// a static pin-to-handler table, guarded by FlagInterruptsEnabled.
///
/// Software interrupt: SystemTriggerSWI() pends a spare NVIC line. The ISR sets
/// FlagSystemAborted and iterates the softwareHandlers table in order. Drivers
/// that need orderly shutdown on abort add an entry to that table. Handlers read
/// SystemStatusFlagsHandle or FaultFlagsHandle directly to determine why the
/// interrupt fired. ManagerTask reboots after SystemTriggerSWI() returns.
///
/// All handlers in both tables must conform to the ISR contract: no FreeRTOS
/// blocking APIs, no direct SPI/I2C, volatile-flag or event-flag set only.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <stdbool.h>

#include "Features.h"
#include "main.h"
#include "Platform.h"
#include "SystemStatusFlags.h"
#include "event_groups.h"
#include "Interrupts.h"
#include "PowerManager.h"
#include "Triac.h"
#include "Thermocouple.h"
#include "USBPowerDelivery.h"

// ============================================================================
// Types
// ============================================================================

/// @brief Function pointer type for GPIO edge interrupt handlers.
/// @param[in] GPIO_Pin The HAL pin bitmask that triggered the interrupt.
typedef void (*EdgeInterruptHandler)( uint16_t GPIO_Pin );


/// @brief Function pointer type for software interrupt handlers.
/// @param[in] reason The SWIReasonBit bitmask passed to SystemTriggerSWI().
typedef void (*SoftwareInterruptHandler)( uint32_t reason );

/// @brief Edge polarity that a GPIO table entry responds to.
typedef enum {
    RisingEdge,   ///< Triggered on a low-to-high GPIO transition
    FallingEdge   ///< Triggered on a high-to-low GPIO transition
} EdgeType;



// ============================================================================
// Constants
// ============================================================================

/// @brief IRQ line reserved for the system software interrupt.
static const IRQn_Type kSwiIRQn = ADC1_COMP_IRQn;

// ============================================================================
// Dispatch tables
// ============================================================================

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
#if FEATURE_THERMOCOUPLES
    { THERM1_DRDY_Pin,  RisingEdge,  TCHandleDRDYInterrupt     },  ///< TC1 data-ready
    { THERM1_FAULT_Pin, RisingEdge,  TCHandleFaultInterrupt    },  ///< TC1 fault assertion
#endif // FEATURE_THERMOCOUPLES
#if FEATURE_USB_PD
    { FLGN_Pin,         FallingEdge, USBPDHandleFLGNInterrupt  },  ///< TCPP03 FLAG_N (active low)
    { PD_SRC_INT_Pin,   FallingEdge, USBPDHandleSourceInterrupt},  ///< STPD01 source interrupt
#endif // FEATURE_USB_PD
};

/// @brief Static software interrupt dispatch table.
///
/// Each entry maps a SWIReasonBit bitmask to a handler. The ISR calls every
/// handler whose mask intersects the reason passed to SystemTriggerSWI(). Add
/// one entry per driver that needs to respond — e.g. kill outputs, flush buffers.
static struct {
    uint32_t                 mask;
    SoftwareInterruptHandler handler;
} softwareHandlers[] = {
    // { BIT( FlagESTOP ), SomeHandler },
};

// ============================================================================
// Functions
// ============================================================================

/// @brief Trigger the system software interrupt with the given reason bitmask.
void SystemTriggerSWI( uint32_t reason ) {
    NVIC_SetPendingIRQ( kSwiIRQn );
    ( void )reason;
}

/// @brief System software interrupt handler — called from ADC1_COMP_IRQHandler.
/// @warning ISR context — no FreeRTOS blocking APIs.
void SystemSWIHandler( void ) {
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBitsFromISR( (EventGroupHandle_t)SystemStatusFlagsHandle, BIT( FlagSystemAborted ), &higherPriorityTaskWoken );
    for ( uint8_t i = 0; i < sizeof( softwareHandlers ) / sizeof( softwareHandlers[ 0 ] ); i++ ) {
        softwareHandlers[ i ].handler( 0 );
    }
    portYIELD_FROM_ISR( higherPriorityTaskWoken );
}

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
