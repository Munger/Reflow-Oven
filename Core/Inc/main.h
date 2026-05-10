/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

#include "stm32g0xx_ll_ucpd.h"
#include "stm32g0xx_ll_bus.h"
#include "stm32g0xx_ll_cortex.h"
#include "stm32g0xx_ll_rcc.h"
#include "stm32g0xx_ll_system.h"
#include "stm32g0xx_ll_utils.h"
#include "stm32g0xx_ll_pwr.h"
#include "stm32g0xx_ll_gpio.h"
#include "stm32g0xx_ll_dma.h"

#include "stm32g0xx_ll_exti.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define HDL_BOOT0_Pin GPIO_PIN_13
#define HDL_BOOT0_GPIO_Port GPIOC
#define OSC32_IN_Pin GPIO_PIN_14
#define OSC32_IN_GPIO_Port GPIOC
#define OSC32_OUT_Pin GPIO_PIN_15
#define OSC32_OUT_GPIO_Port GPIOC
#define OSC_IN_Pin GPIO_PIN_0
#define OSC_IN_GPIO_Port GPIOF
#define IANA_Pin GPIO_PIN_0
#define IANA_GPIO_Port GPIOA
#define MAINS_PWR_N_Pin GPIO_PIN_1
#define MAINS_PWR_N_GPIO_Port GPIOA
#define CJT1_THERM_Pin GPIO_PIN_2
#define CJT1_THERM_GPIO_Port GPIOA
#define CJT2_THERM_Pin GPIO_PIN_3
#define CJT2_THERM_GPIO_Port GPIOA
#define FLASH_CS_Pin GPIO_PIN_4
#define FLASH_CS_GPIO_Port GPIOA
#define OVEN_THERM_Pin GPIO_PIN_0
#define OVEN_THERM_GPIO_Port GPIOB
#define HDL_NRST_Pin GPIO_PIN_1
#define HDL_NRST_GPIO_Port GPIOB
#define BUZZER_EN_N_Pin GPIO_PIN_2
#define BUZZER_EN_N_GPIO_Port GPIOB
#define THERM1_DRDY_Pin GPIO_PIN_10
#define THERM1_DRDY_GPIO_Port GPIOB
#define THERM1_DRDY_EXTI_IRQn EXTI4_15_IRQn
#define THERM1_FAULT_Pin GPIO_PIN_11
#define THERM1_FAULT_GPIO_Port GPIOB
#define THERM1_FAULT_EXTI_IRQn EXTI4_15_IRQn
#define THERM2_CS_Pin GPIO_PIN_12
#define THERM2_CS_GPIO_Port GPIOB
#define THERM2_DRDY_Pin GPIO_PIN_13
#define THERM2_DRDY_GPIO_Port GPIOB
#define THERM2_DRDY_EXTI_IRQn EXTI4_15_IRQn
#define THERM2_FAULT_Pin GPIO_PIN_14
#define THERM2_FAULT_GPIO_Port GPIOB
#define THERM2_FAULT_EXTI_IRQn EXTI4_15_IRQn
#define HDL_CLK_Pin GPIO_PIN_6
#define HDL_CLK_GPIO_Port GPIOC
#define HDL_DIO_Pin GPIO_PIN_7
#define HDL_DIO_GPIO_Port GPIOC
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define FLGN_Pin GPIO_PIN_15
#define FLGN_GPIO_Port GPIOA
#define ESTOP_Pin GPIO_PIN_0
#define ESTOP_GPIO_Port GPIOD
#define ESTOP_EXTI_IRQn EXTI0_1_IRQn
#define ZCD_Pin GPIO_PIN_1
#define ZCD_GPIO_Port GPIOD
#define ZCD_EXTI_IRQn EXTI0_1_IRQn
#define PD_SRC_INT_Pin GPIO_PIN_2
#define PD_SRC_INT_GPIO_Port GPIOD
#define PD_SRC_INT_EXTI_IRQn EXTI2_3_IRQn
#define PD_SRC_PON_Pin GPIO_PIN_3
#define PD_SRC_PON_GPIO_Port GPIOD
#define HOT_SIDE_PWR_EN_N_Pin GPIO_PIN_3
#define HOT_SIDE_PWR_EN_N_GPIO_Port GPIOB
#define OVEN_FAN_EN_N_Pin GPIO_PIN_4
#define OVEN_FAN_EN_N_GPIO_Port GPIOB
#define HTR_TOP_EN_N_Pin GPIO_PIN_5
#define HTR_TOP_EN_N_GPIO_Port GPIOB
#define HTR_REAR_EN_N_Pin GPIO_PIN_6
#define HTR_REAR_EN_N_GPIO_Port GPIOB
#define HTR_BOT_EN_N_Pin GPIO_PIN_7
#define HTR_BOT_EN_N_GPIO_Port GPIOB
#define LIGHT_EN_N_Pin GPIO_PIN_8
#define LIGHT_EN_N_GPIO_Port GPIOB
#define THERM1_CS_Pin GPIO_PIN_9
#define THERM1_CS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
