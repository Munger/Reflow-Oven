/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "apicore.h"
#include "apicodec.h"
#include "cJSON.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for WatchdogTask */
osThreadId_t WatchdogTaskHandle;
const osThreadAttr_t WatchdogTask_attributes = {
  .name = "WatchdogTask",
  .priority = (osPriority_t) osPriorityHigh,
  .stack_size = 128 * 4
};
/* Definitions for SensorsTask */
osThreadId_t SensorsTaskHandle;
const osThreadAttr_t SensorsTask_attributes = {
  .name = "SensorsTask",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 512 * 4
};
/* Definitions for InterfaceTask */
osThreadId_t InterfaceTaskHandle;
const osThreadAttr_t InterfaceTask_attributes = {
  .name = "InterfaceTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};
/* Definitions for LoggingTask */
osThreadId_t LoggingTaskHandle;
const osThreadAttr_t LoggingTask_attributes = {
  .name = "LoggingTask",
  .priority = (osPriority_t) osPriorityBelowNormal,
  .stack_size = 256 * 4
};
/* Definitions for SensorsQueue */
osMessageQueueId_t SensorsQueueHandle;
const osMessageQueueAttr_t SensorsQueue_attributes = {
  .name = "SensorsQueue"
};
/* Definitions for SPIBusMutex */
osMutexId_t SPIBusMutexHandle;
const osMutexAttr_t SPIBusMutex_attributes = {
  .name = "SPIBusMutex"
};
/* Definitions for I2CBusMutex */
osMutexId_t I2CBusMutexHandle;
const osMutexAttr_t I2CBusMutex_attributes = {
  .name = "I2CBusMutex"
};
/* Definitions for SystemStatusFlags */
osEventFlagsId_t SystemStatusFlagsHandle;
const osEventFlagsAttr_t SystemStatusFlags_attributes = {
  .name = "SystemStatusFlags"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

void APIReset( void );

/* USER CODE END FunctionPrototypes */

void StartWatchdogTask(void *argument);
void StartSensorsTask(void *argument);
void StartInterfaceTask(void *argument);
void StartLoggingTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  APIReset();

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of SPIBusMutex */
  SPIBusMutexHandle = osMutexNew(&SPIBusMutex_attributes);

  /* creation of I2CBusMutex */
  I2CBusMutexHandle = osMutexNew(&I2CBusMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of SensorsQueue */
  SensorsQueueHandle = osMessageQueueNew (3, 48, &SensorsQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of WatchdogTask */
  WatchdogTaskHandle = osThreadNew(StartWatchdogTask, NULL, &WatchdogTask_attributes);

  /* creation of SensorsTask */
  SensorsTaskHandle = osThreadNew(StartSensorsTask, NULL, &SensorsTask_attributes);

  /* creation of InterfaceTask */
  InterfaceTaskHandle = osThreadNew(StartInterfaceTask, NULL, &InterfaceTask_attributes);

  /* creation of LoggingTask */
  LoggingTaskHandle = osThreadNew(StartLoggingTask, NULL, &LoggingTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* creation of SystemStatusFlags */
  SystemStatusFlagsHandle = osEventFlagsNew(&SystemStatusFlags_attributes);

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartWatchdogTask */
/**
  * @brief  Function implementing the WatchdogTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartWatchdogTask */
void StartWatchdogTask(void *argument)
{
  /* init code for USB_Device */
  MX_USB_Device_Init();
  /* USER CODE BEGIN StartWatchdogTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartWatchdogTask */
}

/* USER CODE BEGIN Header_StartSensorsTask */
/**
* @brief Function implementing the SensorsTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSensorsTask */
void StartSensorsTask(void *argument)
{
  /* USER CODE BEGIN StartSensorsTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartSensorsTask */
}

/* USER CODE BEGIN Header_StartInterfaceTask */
/**
* @brief Function implementing the InterfaceTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartInterfaceTask */
void StartInterfaceTask(void *argument)
{
  /* USER CODE BEGIN StartInterfaceTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartInterfaceTask */
}

/* USER CODE BEGIN Header_StartLoggingTask */
/**
* @brief Function implementing the LoggingTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartLoggingTask */
void StartLoggingTask(void *argument)
{
  /* USER CODE BEGIN StartLoggingTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartLoggingTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void APIReset( void ) {

    APICoreInit();
    APIStreamInit();

    // Link cJSON to FreeRTOS Heap
    cJSON_Hooks hooks = {
        .malloc_fn = pvPortMalloc,
        .free_fn = vPortFree
    };
    cJSON_InitHooks( &hooks );
}

/* USER CODE END Application */

