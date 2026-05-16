/// @file I2CManager.h
///
/// @brief I2C bus manager with asynchronous and synchronous transfer APIs.
///
/// Wraps HAL I2C interrupt-driven and blocking transfers behind a single
/// semaphore-guarded interface. All bus arbitration is handled internally;
/// callers supply a callback for async completion notification.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef I2CMANAGER_H
#define I2CMANAGER_H

#include "SystemStatusFlags.h"
#include "Types.h"
#include "main.h"

/// @brief Status and diagnostic flag bit positions for the I2C bus.
/// These map 1:1 to the bits in the private i2cStatus event flag group.
typedef enum {
    FlagI2C1StatusReady = 0,          ///< Bus initialised and ready for transfers
    FlagI2C1StatusBusError,           ///< HAL reported a bus error (BERR or NACK)
    FlagI2C1StatusArbitrationLost,    ///< Multi-master arbitration lost (ARLO)
    FlagI2C1StatusTimeout,            ///< Transfer exceeded the caller-supplied timeout
    FlagI2C1StatusLocked,             ///< SDA held low by a peripheral (bus locked)

    I2C1FlagsCount
} I2CStatusBit;

_Static_assert( I2C1FlagsCount <= 24, "I2CStatusFlags out of bounds" );

/// @brief Completion callback type for asynchronous I2C operations.
/// @param[in] success true if the transfer completed without error, false otherwise.
/// @warning Called from HAL interrupt context. Must not call any FreeRTOS blocking API.
typedef void ( *I2CCallback )( bool success );

/// @brief Initialise the I2C manager, bind the hardware handle, and register HAL callbacks.
void              I2CInitModule( void );

/// @brief Start an asynchronous interrupt-driven memory read.
/// @param[in]  devAddr 7-bit device address shifted left by 1.
/// @param[in]  memAddr Register or memory address to read from.
/// @param[in]  size    Memory address size (I2C_MEMADD_SIZE_8BIT or _16BIT).
/// @param[out] pData   Destination buffer; must remain valid until @p cb fires.
/// @param[in]  len     Number of bytes to read.
/// @param[in]  cb      Completion callback; called from ISR context.
/// @return HAL_OK if the transfer was queued, HAL_BUSY if the bus is unavailable.
/// @warning Do not free or reuse @p pData before the callback fires.
HAL_StatusTypeDef I2CReadAsync( uint16_t devAddr, uint16_t memAddr, uint16_t size, uint8_t* pData, uint16_t len,
                                   I2CCallback cb );

/// @brief Perform a synchronous blocking memory write.
/// @param[in] devAddr  7-bit device address shifted left by 1.
/// @param[in] memAddr  Register or memory address to write to.
/// @param[in] size     Memory address size (I2C_MEMADD_SIZE_8BIT or _16BIT).
/// @param[in] pData    Source buffer.
/// @param[in] len      Number of bytes to write.
/// @param[in] timeout  Maximum wait time in milliseconds for the bus semaphore and transfer.
/// @return HAL_OK on success, HAL_BUSY if semaphore timed out, HAL_ERROR on bus fault.
/// @warning Only call from task context, not ISR.
HAL_StatusTypeDef I2CWriteSync( uint16_t devAddr, uint16_t memAddr, uint16_t size, uint8_t* pData, uint16_t len,
                                 uint32_t timeout );

#endif // I2CMANAGER_H
