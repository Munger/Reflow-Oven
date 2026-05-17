/// @file I2CManager.h
///
/// @brief I2C bus manager with asynchronous and synchronous transfer APIs.
///
/// Wraps HAL I2C interrupt-driven and blocking transfers behind a per-instance
/// semaphore-guarded interface. All bus arbitration is handled internally;
/// callers supply a callback for async completion notification.
///
/// Each I2C bus is identified by I2CID and opened with I2COpen(). All transfer
/// functions take the returned I2CRef as their first argument — no HAL handle
/// or bus number is used at call sites.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef I2CMANAGER_H
#define I2CMANAGER_H

#include "SystemStatusFlags.h"
#include "Types.h"
#include "main.h"

/// @brief Logical identifiers for I2C bus instances managed by this driver.
typedef enum {
    I2CBus1 = 0,    ///< I2C1 — fan, encoders, thermistors, USB-PD companions
    I2CBusCount
} I2CID;

/// @brief Opaque handle to an I2C bus instance.
typedef struct I2CInstance* I2CRef;

/// @brief Status and diagnostic flag bit positions for an I2C bus instance.
/// These map 1:1 to the bits in the per-instance statusHandle event flag group.
typedef enum {
    FlagI2CStatusReady = 0,          ///< Bus initialised and ready for transfers
    FlagI2CStatusBusError,           ///< HAL reported a bus error (BERR or NACK)
    FlagI2CStatusArbitrationLost,    ///< Multi-master arbitration lost (ARLO)
    FlagI2CStatusTimeout,            ///< Transfer exceeded the caller-supplied timeout
    FlagI2CStatusLocked,             ///< SDA held low by a peripheral (bus locked)

    I2CFlagsCount
} I2CStatusBit;

_Static_assert( I2CFlagsCount <= 24, "I2CStatusFlags out of bounds" );

/// @brief Completion callback type for asynchronous I2C operations.
/// @param[in] success true if the transfer completed without error, false otherwise.
/// @warning Called from HAL interrupt context. Must not call any FreeRTOS blocking API.
typedef void ( *I2CCallback )( bool success );

/// @brief Initialise all I2C bus instances, bind hardware handles, and register HAL callbacks.
/// @note Must be called once from task context before any transfers are attempted.
void              I2CInitModule( void );

/// @brief Return a handle to a specific I2C bus instance.
/// @param[in] id  Bus identifier.
/// @return Handle to the instance, or NULL if @p id is out of range.
I2CRef            I2COpen( I2CID id );

/// @brief Return the full status bitmask for a specific I2C bus instance.
/// @param[in] i2c Handle returned by I2COpen().
/// @return Bitmask of I2CStatusBit flags; 0 if @p i2c is NULL.
uint32_t          I2CGetStatus( I2CRef i2c );

/// @brief Start an asynchronous interrupt-driven memory read.
/// @param[in]  i2c     Handle returned by I2COpen().
/// @param[in]  devAddr 7-bit device address shifted left by 1.
/// @param[in]  memAddr Register or memory address to read from.
/// @param[in]  size    Memory address size (I2C_MEMADD_SIZE_8BIT or _16BIT).
/// @param[out] pData   Destination buffer; must remain valid until @p cb fires.
/// @param[in]  len     Number of bytes to read.
/// @param[in]  cb      Completion callback; called from ISR context.
/// @return HAL_OK if the transfer was queued, HAL_BUSY if the bus is unavailable.
/// @warning Do not free or reuse @p pData before the callback fires.
HAL_StatusTypeDef I2CReadAsync( I2CRef i2c, uint16_t devAddr, uint16_t memAddr, uint16_t size,
                                uint8_t* pData, uint16_t len, I2CCallback cb );

/// @brief Perform a synchronous blocking memory read.
/// @param[in]  i2c     Handle returned by I2COpen().
/// @param[in]  devAddr 7-bit device address shifted left by 1.
/// @param[in]  memAddr Register or memory address to read from.
/// @param[in]  size    Memory address size (I2C_MEMADD_SIZE_8BIT or _16BIT).
/// @param[out] pData   Destination buffer.
/// @param[in]  len     Number of bytes to read.
/// @param[in]  timeout Maximum wait time in milliseconds for the bus semaphore and transfer.
/// @return HAL_OK on success, HAL_BUSY if semaphore timed out, HAL_ERROR on bus fault.
/// @warning Only call from task context, not ISR.
HAL_StatusTypeDef I2CReadSync( I2CRef i2c, uint16_t devAddr, uint16_t memAddr, uint16_t size,
                               uint8_t* pData, uint16_t len, uint32_t timeout );

/// @brief Start an asynchronous interrupt-driven master receive (no register address).
/// @note Use for read-only devices with no register pointer byte (e.g. MCP3221).
/// @param[in]  i2c     Handle returned by I2COpen().
/// @param[in]  devAddr 7-bit device address shifted left by 1.
/// @param[out] pData   Destination buffer; must remain valid until @p cb fires.
/// @param[in]  len     Number of bytes to read.
/// @param[in]  cb      Completion callback; called from ISR context.
/// @return HAL_OK if queued, HAL_BUSY if bus is in use.
HAL_StatusTypeDef I2CReceiveAsync( I2CRef i2c, uint16_t devAddr,
                                   uint8_t* pData, uint16_t len, I2CCallback cb );

/// @brief Perform a synchronous blocking master receive (no register address).
/// @note Use for read-only devices with no register pointer byte (e.g. MCP3221).
/// @param[in]  i2c     Handle returned by I2COpen().
/// @param[in]  devAddr 7-bit device address shifted left by 1.
/// @param[out] pData   Destination buffer.
/// @param[in]  len     Number of bytes to read.
/// @param[in]  timeout Maximum wait time in milliseconds for the bus semaphore and transfer.
/// @return HAL_OK on success, HAL_BUSY if semaphore timed out, HAL_ERROR on bus fault.
/// @warning Only call from task context, not ISR.
HAL_StatusTypeDef I2CReceiveSync( I2CRef i2c, uint16_t devAddr,
                                  uint8_t* pData, uint16_t len, uint32_t timeout );

/// @brief Perform a synchronous blocking memory write.
/// @param[in] i2c     Handle returned by I2COpen().
/// @param[in] devAddr 7-bit device address shifted left by 1.
/// @param[in] memAddr Register or memory address to write to.
/// @param[in] size    Memory address size (I2C_MEMADD_SIZE_8BIT or _16BIT).
/// @param[in] pData   Source buffer.
/// @param[in] len     Number of bytes to write.
/// @param[in] timeout Maximum wait time in milliseconds for the bus semaphore and transfer.
/// @return HAL_OK on success, HAL_BUSY if semaphore timed out, HAL_ERROR on bus fault.
/// @warning Only call from task context, not ISR.
HAL_StatusTypeDef I2CWriteSync( I2CRef i2c, uint16_t devAddr, uint16_t memAddr, uint16_t size,
                                uint8_t* pData, uint16_t len, uint32_t timeout );

#endif // I2CMANAGER_H
