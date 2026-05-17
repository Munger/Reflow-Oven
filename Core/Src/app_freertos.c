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

#include "APITask.h"
#include "DeviceTask.h"
#include "LoggingTask.h"
#include "USBPDTask.h"
#include "ManagerTask.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticSemaphore_t osStaticSemaphoreDef_t;
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
/* Definitions for ManagerTask */
osThreadId_t ManagerTaskHandle;
const osThreadAttr_t ManagerTask_attributes = {
  .name = "ManagerTask",
  .priority = (osPriority_t) osPriorityHigh,
  .stack_size = 384 * 4
};
/* Definitions for DeviceTask */
osThreadId_t DeviceTaskHandle;
const osThreadAttr_t DeviceTask_attributes = {
  .name = "DeviceTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};
/* Definitions for APITask */
osThreadId_t APITaskHandle;
const osThreadAttr_t APITask_attributes = {
  .name = "APITask",
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
/* Definitions for USBPDTask */
osThreadId_t USBPDTaskHandle;
const osThreadAttr_t USBPDTask_attributes = {
  .name = "USBPDTask",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 512 * 4
};
/* Definitions for SensorsQueue */
osMessageQueueId_t SensorsQueueHandle;
const osMessageQueueAttr_t SensorsQueue_attributes = {
  .name = "SensorsQueue"
};
/* Definitions for I2CBusSem */
osSemaphoreId_t I2CBusSemHandle;
osStaticSemaphoreDef_t I2CBusSemControlBlock;
const osSemaphoreAttr_t I2CBusSem_attributes = {
  .name = "I2CBusSem",
  .cb_mem = &I2CBusSemControlBlock,
  .cb_size = sizeof(I2CBusSemControlBlock),
};
/* Definitions for SPIBusSem */
osSemaphoreId_t SPIBusSemHandle;
osStaticSemaphoreDef_t SPIBusSemControlBlock;
const osSemaphoreAttr_t SPIBusSem_attributes = {
  .name = "SPIBusSem",
  .cb_mem = &SPIBusSemControlBlock,
  .cb_size = sizeof(SPIBusSemControlBlock),
};
/* Definitions for SystemStatusFlags */
osEventFlagsId_t SystemStatusFlagsHandle;
const osEventFlagsAttr_t SystemStatusFlags_attributes = {
  .name = "SystemStatusFlags"
};
/* Definitions for DeviceStatusFlags */
osEventFlagsId_t DeviceStatusFlagsHandle;
const osEventFlagsAttr_t DeviceStatusFlags_attributes = {
  .name = "DeviceStatusFlags"
};
/* Definitions for FaultFlags */
osEventFlagsId_t FaultFlagsHandle;
const osEventFlagsAttr_t FaultFlags_attributes = {
  .name = "FaultFlags"
};
/* Definitions for ReflowStatusFlags */
osEventFlagsId_t ReflowStatusFlagsHandle;
const osEventFlagsAttr_t ReflowStatusFlags_attributes = {
  .name = "ReflowStatusFlags"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartManagerTask(void *argument);
void StartDeviceTask(void *argument);
void StartAPITask(void *argument);
void StartLoggingTask(void *argument);
void StartUSBPDTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN 4 */
void                       vApplicationStackOverflowHook( xTaskHandle xTask, signed char* pcTaskName ) {
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

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
    /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of I2CBusSem */
  I2CBusSemHandle = osSemaphoreNew(1, 1, &I2CBusSem_attributes);

  /* creation of SPIBusSem */
  SPIBusSemHandle = osSemaphoreNew(1, 1, &SPIBusSem_attributes);

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
  /* creation of ManagerTask */
  ManagerTaskHandle = osThreadNew(StartManagerTask, NULL, &ManagerTask_attributes);

  /* creation of DeviceTask */
  DeviceTaskHandle = osThreadNew(StartDeviceTask, NULL, &DeviceTask_attributes);

  /* creation of APITask */
  APITaskHandle = osThreadNew(StartAPITask, NULL, &APITask_attributes);

  /* creation of LoggingTask */
  LoggingTaskHandle = osThreadNew(StartLoggingTask, NULL, &LoggingTask_attributes);

  /* creation of USBPDTask */
  USBPDTaskHandle = osThreadNew(StartUSBPDTask, NULL, &USBPDTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
    /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* Create the event(s) */
  /* creation of SystemStatusFlags */
  SystemStatusFlagsHandle = osEventFlagsNew(&SystemStatusFlags_attributes);

  /* creation of DeviceStatusFlags */
  DeviceStatusFlagsHandle = osEventFlagsNew(&DeviceStatusFlags_attributes);

  /* creation of FaultFlags */
  FaultFlagsHandle = osEventFlagsNew(&FaultFlags_attributes);

  /* creation of ReflowStatusFlags */
  ReflowStatusFlagsHandle = osEventFlagsNew(&ReflowStatusFlags_attributes);

  /* USER CODE BEGIN RTOS_EVENTS */
    /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartManagerTask */
/**
  * @brief  Function implementing the ManagerTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartManagerTask */
void StartManagerTask(void *argument)
{
  /* init code for USB_Device */
  MX_USB_Device_Init();
  /* USER CODE BEGIN StartManagerTask */
  ManagerTaskInit();
  for(;;){
    ManagerTaskLoop();
  }
  /* USER CODE END StartManagerTask */
}

/* USER CODE BEGIN Header_StartDeviceTask */
/**
* @brief Function implementing the DeviceTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDeviceTask */
void StartDeviceTask(void *argument)
{
  /* USER CODE BEGIN StartDeviceTask */
    DeviceTaskInit();

    for ( ;; ) {
      DeviceTaskLoop();
    }
  /* USER CODE END StartDeviceTask */
}

/* USER CODE BEGIN Header_StartAPITask */
/**
 * @brief Function implementing the APITask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartAPITask */
void StartAPITask(void *argument)
{
  /* USER CODE BEGIN StartAPITask */
    APITaskInit();

    for ( ;; ) {
      APITaskLoop();
    }
  /* USER CODE END StartAPITask */
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
    LoggingTaskInit();

    for ( ;; ) {
      LoggingTaskLoop();
    }
  /* USER CODE END StartLoggingTask */
}

/* USER CODE BEGIN Header_StartUSBPDTask */
/**
* @brief Function implementing the USBPDTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUSBPDTask */
void StartUSBPDTask(void *argument)
{
  /* USER CODE BEGIN StartUSBPDTask */
  USBPDTaskInit();
  
  for(;;) {
    USBPDTaskLoop();
  }
  /* USER CODE END StartUSBPDTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

