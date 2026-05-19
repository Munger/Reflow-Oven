/// @file TaskUtils.c
///
/// @brief Bare-metal fallback implementations of FreeRTOS task notification APIs.
///
/// All functions in this file are guarded by `#ifndef xXxx` / `__weak` so that
/// the linker silently prefers the real FreeRTOS implementation when the project
/// is built with `FREE_RTOS=1`. In bare-metal mode the stubs operate on the
/// single global `vBareMetalTaskNotifyValue` word using a nestable
/// disable/enable critical section.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Types.h"
#include "TaskUtils.h"

#if !FREE_RTOS
#include "stm32g0xx.h" // For __disable_irq/enable_irq and IRQ state
#endif

/// @brief Bare-metal task notification shadow word.
///
/// Replaces the per-task notification field that FreeRTOS maintains internally.
/// All bare-metal notification stubs read from and write to this word.
volatile uint32_t vBareMetalTaskNotifyValue = 0;

#if !FREE_RTOS

/// @brief Current interrupt-disable nesting depth for the bare-metal critical section.
static volatile uint32_t critical_nesting_level = 0;

/// @brief Disable interrupts and increment the nesting counter.
///
/// Nestable: multiple calls are safe as long as each is matched by a
/// `vTaskExitCritical()`. Interrupts are only re-enabled when the counter
/// returns to zero.
void vTaskEnterCritical( void ) {
    __disable_irq();
    critical_nesting_level++;
}

/// @brief Decrement the nesting counter and re-enable interrupts when it reaches zero.
///
/// No-op if the counter is already zero (protects against mismatched calls).
void vTaskExitCritical( void ) {
    if ( critical_nesting_level > 0 ) {
        critical_nesting_level--;
        if ( critical_nesting_level == 0 ) {
            __enable_irq();
        }
    }
}
#endif

// We wrap these in #ifndef to prevent collision with FreeRTOS macros in task.h

#ifndef xTaskNotify
/// @brief Bare-metal stub for xTaskNotify — applies ulValue to the global notify word.
/// @param[in] xTaskToNotify  Ignored in bare-metal mode.
/// @param[in] ulValue        Value to apply.
/// @param[in] eAction        Only eSetBits is handled; all others overwrite.
/// @return pdPASS always.
__weak BaseType_t xTaskNotify( TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction ) {
    taskENTER_CRITICAL();
    if ( eAction == eSetBits ) {
        vBareMetalTaskNotifyValue |= ulValue;
    } else {
        vBareMetalTaskNotifyValue = ulValue;
    }
    taskEXIT_CRITICAL();
    return pdPASS;
}
#endif

#ifndef xTaskNotifyFromISR
/// @brief Bare-metal stub for xTaskNotifyFromISR — atomically ORs ulValue into the notify word.
///
/// A 32-bit write on Cortex-M0+ is atomic so no critical section is needed here.
/// @param[in]  xTaskToNotify              Ignored in bare-metal mode.
/// @param[in]  ulValue                    Value to OR in.
/// @param[in]  eAction                    Ignored; always treats as eSetBits.
/// @param[out] pxHigherPriorityTaskWoken  Always set to pdFALSE.
/// @return pdPASS always.
__weak BaseType_t xTaskNotifyFromISR( TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, BaseType_t *pxHigherPriorityTaskWoken ) {
    // In ISR context, we assume Bitwise OR for signals.
    // Atomic write on 32-bit ARM usually doesn't need critical sections in ISR.
    vBareMetalTaskNotifyValue |= ulValue;
    if ( pxHigherPriorityTaskWoken ) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
    return pdPASS;
}
#endif

#ifndef xTaskNotifyWait
/// @brief Bare-metal stub for xTaskNotifyWait — checks the global notify word synchronously.
///
/// There is no blocking; if the notify word is zero the function returns pdFAIL immediately.
/// @param[in]  ulBitsToClearOnEntry  Cleared from the notify word before the check.
/// @param[in]  ulBitsToClearOnExit   Cleared from the notify word after a successful read.
/// @param[out] pulNotificationValue  Receives the notify word value (before exit clear).
/// @param[in]  xTicksToWait          Ignored in bare-metal mode.
/// @return pdPASS if the notify word was non-zero, pdFAIL otherwise.
__weak BaseType_t xTaskNotifyWait( uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit, uint32_t *pulNotificationValue, TickType_t xTicksToWait ) {
    BaseType_t result = pdFAIL;

    taskENTER_CRITICAL();
    vBareMetalTaskNotifyValue &= ~ulBitsToClearOnEntry;

    if ( vBareMetalTaskNotifyValue != 0 ) {
        if ( pulNotificationValue ) {
            *pulNotificationValue = vBareMetalTaskNotifyValue;
        }
        vBareMetalTaskNotifyValue &= ~ulBitsToClearOnExit;
        result = pdPASS;
    }
    taskEXIT_CRITICAL();

    return result;
}
#endif

#ifndef vTaskNotifyGiveFromISR
/// @brief Bare-metal stub for vTaskNotifyGiveFromISR — sets bit 0 of the notify word.
/// @param[in]  xTaskToNotify              Ignored in bare-metal mode.
/// @param[out] pxHigherPriorityTaskWoken  Always set to pdFALSE.
__weak void vTaskNotifyGiveFromISR( TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken ) {
    vBareMetalTaskNotifyValue |= (1UL << 0);
    if ( pxHigherPriorityTaskWoken ) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
}
#endif

#ifndef ulTaskNotifyTake
/// @brief Bare-metal stub for ulTaskNotifyTake — reads and optionally clears the notify word.
/// @param[in] xClearCountOnExit  If pdTRUE, clears the entire word; otherwise decrements.
/// @param[in] xTicksToWait       Ignored in bare-metal mode.
/// @return The notify word value captured before any decrement or clear.
__weak uint32_t ulTaskNotifyTake( BaseType_t xClearCountOnExit, TickType_t xTicksToWait ) {
    uint32_t val;
    taskENTER_CRITICAL();
    val = vBareMetalTaskNotifyValue;
    if ( xClearCountOnExit ) {
        vBareMetalTaskNotifyValue = 0;
    } else if ( vBareMetalTaskNotifyValue > 0 ) {
        vBareMetalTaskNotifyValue--;
    }
    taskEXIT_CRITICAL();
    return val;
}
#endif

#if FREE_RTOS

bool WaitForFlags( osEventFlagsId_t handle, uint32_t bits, uint32_t ms ) {
    uint32_t result = osEventFlagsWait( handle, bits, osFlagsWaitAny | osFlagsNoClear, ms );
    return ( result & bits ) != 0;
}

#endif // FREE_RTOS

#ifndef vPortYieldFromISR
/// @brief Bare-metal stub for vPortYieldFromISR — no-op.
///
/// In bare-metal mode there is no scheduler to yield to. The main loop
/// processes signals after the current ISR exits naturally.
/// @param[in] x  Ignored.
__weak void vPortYieldFromISR( BaseType_t x ) {
    // No-op in bare metal. Main loop checks signals after current ISR exits.
}
#endif
