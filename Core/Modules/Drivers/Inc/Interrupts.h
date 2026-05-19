/// @file Interrupts.h
///
/// @brief System interrupt infrastructure — GPIO dispatch and software interrupt bus.
///
/// GPIO edge interrupts are dispatched from HAL callbacks via a static pin-to-handler
/// table in Interrupts.c. Software interrupts provide a broadcast mechanism for
/// system-wide events: ManagerTask calls SystemTriggerSWI() with a SWIReasonBit
/// bitmask, which fires a software-triggered NVIC interrupt, sets FlagSystemAborted,
/// and fans out to every handler in the dispatch table whose mask intersects the
/// reason. ManagerTask reboots after SystemTriggerSWI() returns.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>
#include "cmsis_os2.h"
#include "SystemStatusFlags.h"

/// @brief Reason bits passed to SystemTriggerSWI().
///
/// OR these together to signal multiple simultaneous conditions.
/// Each softwareHandlers[] entry masks against these bits to opt in to
/// the events it cares about.
typedef enum {
    SWIReasonESTOP   = 0,   ///< Emergency stop asserted
    SWIReasonAbort,         ///< General controlled abort

    SWIReasonCount          ///< Number of reason bits — must stay <= 32.
} SWIReasonBit;

_Static_assert( SWIReasonCount <= 32, "SWIReason flags out of bounds" );

/// @brief Trigger the system software interrupt with the given reason bitmask.
///
/// Stores @p reason, pends the software interrupt NVIC line, and returns. The
/// ISR sets FlagSystemAborted and dispatches to all handlers whose mask
/// intersects @p reason in table order.
///
/// @param[in] reason  One or more SWIReasonBit values OR'd together via BIT().
/// @note Safe to call from both task and ISR context.
void SystemTriggerSWI( uint32_t reason );

/// @brief System software interrupt handler — call from ADC1_COMP_IRQHandler in stm32g0xx_it.c.
/// @warning ISR context — no FreeRTOS blocking APIs.
void SystemSWIHandler( void );

#endif // INTERRUPTS_H
