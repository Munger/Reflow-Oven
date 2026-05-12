#include "types.h"
#include "taskutils.h"

#if !FREE_RTOS
#include "stm32g0xx.h" // For __disable_irq/enable_irq and IRQ state
#endif

volatile uint32_t vBareMetalTaskNotifyValue = 0;

#if !FREE_RTOS
// Nested Critical Section Implementation for Bare Metal
static volatile uint32_t critical_nesting_level = 0;

void vTaskEnterCritical(void) {
    __disable_irq();
    critical_nesting_level++;
}

void vTaskExitCritical(void) {
    if (critical_nesting_level > 0) {
        critical_nesting_level--;
        if (critical_nesting_level == 0) {
            __enable_irq();
        }
    }
}
#endif

// We wrap these in #ifndef to prevent collision with FreeRTOS macros in task.h
#ifndef xTaskNotify
__weak BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction) {
    taskENTER_CRITICAL();
    if (eAction == eSetBits) {
        vBareMetalTaskNotifyValue |= ulValue;
    } else {
        vBareMetalTaskNotifyValue = ulValue;
    }
    taskEXIT_CRITICAL();
    return pdPASS;
}
#endif

#ifndef xTaskNotifyFromISR
__weak BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, BaseType_t *pxHigherPriorityTaskWoken) {
    // In ISR context, we assume Bitwise OR for signals. 
    // Atomic write on 32-bit ARM usually doesn't need critical sections in ISR.
    vBareMetalTaskNotifyValue |= ulValue;
    if (pxHigherPriorityTaskWoken) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
    return pdPASS;
}
#endif

#ifndef xTaskNotifyWait
__weak BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit, uint32_t *pulNotificationValue, TickType_t xTicksToWait) {
    BaseType_t result = pdFAIL;
    
    taskENTER_CRITICAL();
    vBareMetalTaskNotifyValue &= ~ulBitsToClearOnEntry;

    if (vBareMetalTaskNotifyValue != 0) {
        if (pulNotificationValue) {
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
__weak void vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken) {
    vBareMetalTaskNotifyValue |= (1UL << 0);
    if (pxHigherPriorityTaskWoken) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
}
#endif

#ifndef ulTaskNotifyTake
__weak uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait) {
    uint32_t val;
    taskENTER_CRITICAL();
    val = vBareMetalTaskNotifyValue;
    if (xClearCountOnExit) {
        vBareMetalTaskNotifyValue = 0;
    } else if (vBareMetalTaskNotifyValue > 0) {
        vBareMetalTaskNotifyValue--;
    }
    taskEXIT_CRITICAL();
    return val;
}
#endif

#ifndef vPortYieldFromISR
__weak void vPortYieldFromISR(BaseType_t x) {
    // No-op in bare metal. Main loop checks signals after current ISR exits.
}
#endif