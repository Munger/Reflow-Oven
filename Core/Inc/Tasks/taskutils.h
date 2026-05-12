#ifndef TASKUTILS_H
#define TASKUTILS_H

#include <stdint.h>
#include <stdbool.h>

// Set this to 0 to switch the entire project to Bare Metal mode
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
    
    typedef uint32_t TickType_t;
    typedef int32_t  BaseType_t;
    typedef void* TaskHandle_t;
    typedef void* osThreadId_t;

    #define pdFALSE          (0)
    #define pdTRUE           (1)
    #define pdPASS           (pdTRUE)
    #define pdFAIL           (pdFALSE)
    #define portMAX_DELAY    (0xFFFFFFFFUL)

    typedef enum {
        eNoAction = 0,
        eSetBits,
        eIncrement,
        eSetValueWithOverwrite,
        eSetValueWithoutOverwrite
    } eNotifyAction;

    // Bare Metal Prototypes
    BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction);
    BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction, BaseType_t *pxHigherPriorityTaskWoken);
    BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit, uint32_t *pulNotificationValue, TickType_t xTicksToWait);
    void vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken);
    uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait);
    void vPortYieldFromISR(BaseType_t xHigherPriorityTaskWoken);

    // Bare Metal Critical Section Prototypes to match FreeRTOS naming
    void vTaskEnterCritical(void);
    void vTaskExitCritical(void);
    #define taskENTER_CRITICAL() vTaskEnterCritical()
    #define taskEXIT_CRITICAL()  vTaskExitCritical()

    extern volatile uint32_t vBareMetalTaskNotifyValue;
#endif

// Standardize the yield macro for both modes
#ifndef portYIELD_FROM_ISR
    #define portYIELD_FROM_ISR(x) vPortYieldFromISR(x)
#endif

#endif // TASKUTILS_H