/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(HDL_BOOT0_GPIO_Port, HDL_BOOT0_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, FLASH_CSB0_Pin|HDL_NRST_Pin|BUZZER_EN_N_Pin|THERM2_CS_Pin
                          |HOT_SIDE_PWR_EN_N_Pin|OVEN_FAN_EN_N_Pin|HTR_TOP_EN_N_Pin|HTR_REAR_EN_N_Pin
                          |HTR_BOT_EN_N_Pin|LIGHT_EN_N_Pin|THERM1_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : HDL_BOOT0_Pin */
  GPIO_InitStruct.Pin = HDL_BOOT0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HDL_BOOT0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : MAINS_PWR_N_Pin FLGN_Pin */
  GPIO_InitStruct.Pin = MAINS_PWR_N_Pin|FLGN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : FLASH_CS_Pin */
  GPIO_InitStruct.Pin = FLASH_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(FLASH_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : FLASH_CSB0_Pin HDL_NRST_Pin BUZZER_EN_N_Pin THERM2_CS_Pin
                           HOT_SIDE_PWR_EN_N_Pin OVEN_FAN_EN_N_Pin HTR_TOP_EN_N_Pin HTR_REAR_EN_N_Pin
                           HTR_BOT_EN_N_Pin LIGHT_EN_N_Pin THERM1_CS_Pin */
  GPIO_InitStruct.Pin = FLASH_CSB0_Pin|HDL_NRST_Pin|BUZZER_EN_N_Pin|THERM2_CS_Pin
                          |HOT_SIDE_PWR_EN_N_Pin|OVEN_FAN_EN_N_Pin|HTR_TOP_EN_N_Pin|HTR_REAR_EN_N_Pin
                          |HTR_BOT_EN_N_Pin|LIGHT_EN_N_Pin|THERM1_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : THERM1_DRDY_Pin THERM1_FAULT_Pin THERM2_DRDY_Pin THERM2_FAULT_Pin */
  GPIO_InitStruct.Pin = THERM1_DRDY_Pin|THERM1_FAULT_Pin|THERM2_DRDY_Pin|THERM2_FAULT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : ESTOP_Pin ZCD_Pin PD_SRC_INT_Pin */
  GPIO_InitStruct.Pin = ESTOP_Pin|ZCD_Pin|PD_SRC_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : PD_SRC_PON_Pin */
  GPIO_InitStruct.Pin = PD_SRC_PON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PD_SRC_PON_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);

  HAL_NVIC_SetPriority(EXTI2_3_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);

  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
