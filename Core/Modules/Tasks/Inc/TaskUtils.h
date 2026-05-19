/// @file TaskUtils.h
///
/// @brief FreeRTOS / bare-metal portability layer.
///
/// Defines a compile-time switch (`FREE_RTOS`) that selects between a full
/// FreeRTOS + CMSIS-OS2 build and a bare-metal fallback. In FreeRTOS mode the
/// real kernel headers are included and no types are redefined. In bare-metal
/// mode, compatible type aliases and weak function prototypes are provided so
/// that driver code compiles and links without a kernel.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef TASKUTILS_H
#define TASKUTILS_H

#include <stdint.h>
#include <stdbool.h>

/// @brief Set to 1 to build against FreeRTOS + CMSIS-OS2; set to 0 for bare-metal fallback.
#define FREE_RTOS 1

#if FREE_RTOS
    // Pull in the real kernel definitions
    #include "FreeRTOS.h"
    #include "task.h"
    #include "cmsis_os2.h"

    // In RTOS mode, we don't redefine TaskHandle_t or osThreadId_t
    // because task.h and cmsis_os2.h have already defined them.

#else
    // Bare Metal Fallback Definitions

    /// @brief Bare-metal alias for the FreeRTOS tick type.
    typedef uint32_t TickType_t;

    /// @brief Bare-metal alias for the FreeRTOS base integer type.
    typedef int32_t  BaseType_t;

    /// @brief Bare-metal alias for a task handle (unused in bare-metal mode).
    typedef void* TaskHandle_t;

    /// @brief Bare-metal alias for the CMSIS-OS2 thread ID type.
    typedef void* osThreadId_t;

    /// @brief Boolean false constant, matching the FreeRTOS definition.
    #define pdFALSE          (0)

    /// @brief Boolean true constant, matching the FreeRTOS definition.
    #define pdTRUE           (1)

    /// @brief Pass result constant, matching the FreeRTOS definition.
    #define pdPASS           (pdTRUE)

    /// @brief Fail result constant, matching the FreeRTOS definition.
    #define pdFAIL           (pdFALSE)

    /// @brief Maximum delay value — used to block indefinitely in task loops.
    #define portMAX_DELAY    (0xFFFFFFFFUL)

    /// @brief Notification action type, mirroring the FreeRTOS eNotifyAction enum.
    typedef enum {
        eNoAction = 0,            ///< No action on the notification value.
        eSetBits,                 ///< OR the notification value with ulValue.
        eIncrement,               ///< Increment the notification value.
        eSetValueWithOverwrite,   ///< Overwrite the notification value unconditionally.
        eSetValueWithoutOverwrite ///< Write the value only if the task has no pending notification.
    } eNotifyAction;

    // Bare Metal Prototypes

    /// @brief Bare-metal stub: send a task notification.
    /// @param[in] xTaskToNotify  Target task handle (unused in bare-metal mode).
    /// @param[in] ulValue        Value to apply.
    /// @param[in] eAction        How to apply the value.
    /// @return pdPASS always.
    BaseType_t xTaskNotify( TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction );

    /// @brief Bare-metal stub: send a task notification from an ISR context.
    /// @param[in]  xTaskToNotify              Target task handle (unused).
    /// @param[in]  ulValue                    Value to OR into the notify word.
    /// @param[in]  eAction                    How to apply the value.
    /// @param[out] pxHigherPriorityTaskWoken  Always set to pdFALSE.
    /// @return pdPASS always.
    BaseType_t xTaskNotifyFromISR( TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, BaseType_t *pxHigherPriorityTaskWoken );

    /// @brief Bare-metal stub: wait for a task notification.
    /// @param[in]  ulBitsToClearOnEntry  Bits to clear before checking the value.
    /// @param[in]  ulBitsToClearOnExit   Bits to clear after reading the value.
    /// @param[out] pulNotificationValue  Receives the current notification word.
    /// @param[in]  xTicksToWait          Ignored in bare-metal mode.
    /// @return pdPASS if a non-zero value was found, pdFAIL otherwise.
    BaseType_t xTaskNotifyWait( uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit, uint32_t *pulNotificationValue, TickType_t xTicksToWait );

    /// @brief Bare-metal stub: give a notification from an ISR (counting semaphore increment).
    /// @param[in]  xTaskToNotify              Target task handle (unused).
    /// @param[out] pxHigherPriorityTaskWoken  Always set to pdFALSE.
    void vTaskNotifyGiveFromISR( TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken );

    /// @brief Bare-metal stub: take a task notification (counting semaphore decrement).
    /// @param[in] xClearCountOnExit  If pdTRUE, clears the count after taking.
    /// @param[in] xTicksToWait       Ignored in bare-metal mode.
    /// @return The notification count before decrement.
    uint32_t ulTaskNotifyTake( BaseType_t xClearCountOnExit, TickType_t xTicksToWait );

    /// @brief Bare-metal stub: yield to a higher-priority task from ISR context.
    /// @param[in] xHigherPriorityTaskWoken  Ignored in bare-metal mode.
    void vPortYieldFromISR( BaseType_t xHigherPriorityTaskWoken );

    // Bare Metal Critical Section Prototypes to match FreeRTOS naming

    /// @brief Bare-metal nested critical section enter — disables interrupts.
    void vTaskEnterCritical( void );

    /// @brief Bare-metal nested critical section exit — re-enables interrupts when nesting reaches zero.
    void vTaskExitCritical( void );

    /// @brief Enter a critical section (disable interrupts, nestable).
    #define taskENTER_CRITICAL() vTaskEnterCritical()

    /// @brief Exit a critical section (re-enable interrupts when nesting reaches zero).
    #define taskEXIT_CRITICAL()  vTaskExitCritical()

    /// @brief Global notification value used by the bare-metal task notification stubs.
    extern volatile uint32_t vBareMetalTaskNotifyValue;
#endif

/// @brief Portable ISR yield macro. Maps to vPortYieldFromISR in bare-metal mode;
///        the FreeRTOS kernel header defines the real implementation when FREE_RTOS=1.
#ifndef portYIELD_FROM_ISR
    #define portYIELD_FROM_ISR(x) vPortYieldFromISR(x)
#endif

#if FREE_RTOS

/// @brief Wait up to @p ms for any bit in @p bits to be set in @p handle, without clearing.
///
/// Standardises the osFlagsWaitAny | osFlagsNoClear pattern used throughout the
/// codebase. Any task may call this to block on any event flag group.
///
/// @param[in] handle  CMSIS-RTOS2 event flag group to wait on.
/// @param[in] bits    Bitmask of flag bits to watch (any bit fires the return).
/// @param[in] ms      Maximum wait time in milliseconds; use osWaitForever to block indefinitely.
/// @return True if any watched bit was set before the timeout.
bool WaitForFlags( osEventFlagsId_t handle, uint32_t bits, uint32_t ms );

#endif // FREE_RTOS

#endif // TASKUTILS_H
