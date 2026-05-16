/// @file pPatform.h
///
/// @brief Platform abstraction header — pulls in the STM32G0 HAL.
///
/// Acts as a single indirection point for the target platform's HAL include.
/// All driver and task files that need HAL types (GPIO_TypeDef, SPI_HandleTypeDef,
/// etc.) should include this header rather than the HAL directly, so that the
/// include path can be changed in one place if the platform ever changes.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef PLATFORM_H
#define PLATFORM_H

#include "stm32g0xx_hal.h"

#endif // PLATFORM_H
