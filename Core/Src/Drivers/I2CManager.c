/// @file I2CManager.c
///
/// @brief I2C bus manager — interrupt-driven and blocking transfer implementation.
///
/// Owns a single I2CManager context that wraps hi2c1. All public operations
/// are serialised through a binary semaphore (I2CBusSemHandle). The async API
/// registers HAL callbacks that release the semaphore and fire the caller's
/// callback from ISR context. The sync API blocks the calling task until the
/// transfer completes or the timeout expires.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "I2CManager.h"
#include "i2c.h"

/// @brief Semaphore created by CubeMX / app_freertos.c — guards exclusive bus access.
extern osSemaphoreId_t I2CBusSemHandle;

/// @brief Private event flag group for I2C bus diagnostic status.
/// @note Replaces the former public I2CStatusFlags extern. Use I2CInitModule() to
///       initialise and the public API status bits to observe.
static osEventFlagsId_t i2cStatus;

/// @brief Internal state for the singleton I2C bus manager.
typedef struct {
    I2C_HandleTypeDef* hi2c;            ///< Bound HAL handle
    osSemaphoreId_t    mutex;           ///< Semaphore protecting exclusive bus access
    osEventFlagsId_t   flags;           ///< Private diagnostic event flags
    I2CCallback        currentCallback; ///< Callback to invoke when the current transfer completes
    volatile bool      isBusy;          ///< True while a transfer is in flight
} I2CManager;

/// @brief Singleton manager instance — not accessible outside this translation unit.
static I2CManager manager;

static void TransferComplete( I2C_HandleTypeDef* hi2c );
static void TransferError( I2C_HandleTypeDef* hi2c );

/// @brief Initialise the I2C manager, bind hardware handles, and register HAL callbacks.
/// @note Must be called once from task context before any transfers are attempted.
void I2CInitModule( void ) {
    i2cStatus = osEventFlagsNew( NULL );

    manager.hi2c = &hi2c1;
    manager.mutex = I2CBusSemHandle;
    manager.flags = i2cStatus;
    manager.isBusy = false;
    manager.currentCallback = NULL;

    if ( manager.flags != NULL ) {
        osEventFlagsClear( manager.flags, 0xFFFFFF );
        osEventFlagsSet( manager.flags, BIT( FlagI2C1StatusReady ) );
    }

    HAL_I2C_RegisterCallback( manager.hi2c, HAL_I2C_MEM_RX_COMPLETE_CB_ID, TransferComplete );
    HAL_I2C_RegisterCallback( manager.hi2c, HAL_I2C_ERROR_CB_ID, TransferError );
}

/// @brief Start an asynchronous interrupt-driven memory read.
///
/// Acquires the bus semaphore with zero timeout (non-blocking). If the bus is
/// already in use, returns HAL_BUSY immediately without modifying state.
/// On HAL failure, the semaphore is released and the status flags are updated
/// before returning.
///
/// @param[in]  devAddr 7-bit device address shifted left by 1.
/// @param[in]  memAddr Register or memory address to read from.
/// @param[in]  size    Memory address size (I2C_MEMADD_SIZE_8BIT or _16BIT).
/// @param[out] pData   Destination buffer; must remain valid until @p cb fires.
/// @param[in]  len     Number of bytes to read.
/// @param[in]  cb      Completion callback invoked from HAL ISR context.
/// @return HAL_OK if queued, HAL_BUSY if bus is in use.
/// @warning Do not call from ISR context.
HAL_StatusTypeDef I2CReadAsync( uint16_t devAddr, uint16_t memAddr, uint16_t size, uint8_t* pData, uint16_t len,
                                   I2CCallback cb ) {
    if ( osSemaphoreAcquire( manager.mutex, 0 ) != osOK ) {
        return HAL_BUSY;
    }

    manager.isBusy = true;
    manager.currentCallback = cb;

    HAL_StatusTypeDef status = HAL_I2C_Mem_Read_IT( manager.hi2c, devAddr, memAddr, size, pData, len );

    if ( status != HAL_OK ) {
        manager.isBusy = false;
        osSemaphoreRelease( manager.mutex );

        if ( status == HAL_BUSY && manager.flags != NULL ) {
            osEventFlagsSet( manager.flags, BIT( FlagI2C1StatusLocked ) );
        }
    }

    return status;
}

/// @brief Perform a synchronous blocking memory write.
///
/// Acquires the bus semaphore for up to @p timeout milliseconds. On success,
/// issues a HAL blocking write and releases the semaphore on return.
/// Any HAL error is reflected in the local status flags and in FaultFlagsHandle.
///
/// @param[in] devAddr  7-bit device address shifted left by 1.
/// @param[in] memAddr  Register or memory address to write to.
/// @param[in] size     Memory address size (I2C_MEMADD_SIZE_8BIT or _16BIT).
/// @param[in] pData    Source buffer.
/// @param[in] len      Number of bytes to write.
/// @param[in] timeout  Maximum wait in milliseconds for the semaphore and transfer.
/// @return HAL_OK on success, HAL_BUSY if the semaphore timed out, HAL_ERROR on bus fault.
/// @warning Only call from task context, not ISR.
HAL_StatusTypeDef I2CWriteSync( uint16_t devAddr, uint16_t memAddr, uint16_t size, uint8_t* pData, uint16_t len,
                                 uint32_t timeout ) {
    if ( osSemaphoreAcquire( manager.mutex, timeout ) != osOK ) {
        return HAL_BUSY;
    }

    HAL_StatusTypeDef status = HAL_I2C_Mem_Write( manager.hi2c, devAddr, memAddr, size, pData, len, timeout );

    if ( status != HAL_OK ) {
        if ( manager.flags != NULL ) {
            osEventFlagsSet( manager.flags, BIT( FlagI2C1StatusBusError ) );
        }
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagI2CFault ) );
    }

    osSemaphoreRelease( manager.mutex );
    return status;
}

/// @brief HAL callback invoked on successful interrupt transfers.
///
/// Clears transient error flags, releases the bus semaphore, and fires the
/// caller's completion callback with success=true.
///
/// @param[in] hi2c HAL handle (unused; singleton manager is used directly).
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void TransferComplete( I2C_HandleTypeDef* hi2c ) {
    UNUSED( hi2c );
    I2CCallback cb = manager.currentCallback;
    manager.isBusy = false;

    if ( manager.flags != NULL ) {
        osEventFlagsClear( manager.flags, BIT( FlagI2C1StatusBusError ) | BIT( FlagI2C1StatusLocked ) );
    }

    osSemaphoreRelease( manager.mutex );
    if ( cb ) {
        cb( true );
    }
}

/// @brief HAL callback invoked on failed transfers.
///
/// Decodes the HAL error code, sets the appropriate diagnostic flags,
/// raises FlagI2CFault in the global fault group, releases the semaphore,
/// and fires the caller's completion callback with success=false.
///
/// @param[in] hi2c HAL handle used to retrieve the HAL error code.
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void TransferError( I2C_HandleTypeDef* hi2c ) {
    uint32_t err = HAL_I2C_GetError( hi2c );

    if ( manager.flags != NULL ) {
        if ( err & HAL_I2C_ERROR_AF ) {
            osEventFlagsSet( manager.flags, BIT( FlagI2C1StatusBusError ) );
        }
        if ( err & HAL_I2C_ERROR_BERR ) {
            osEventFlagsSet( manager.flags, BIT( FlagI2C1StatusBusError ) );
        }
        if ( err & HAL_I2C_ERROR_ARLO ) {
            osEventFlagsSet( manager.flags, BIT( FlagI2C1StatusArbitrationLost ) );
        }
    }

    osEventFlagsSet( FaultFlagsHandle, BIT( FlagI2CFault ) );

    I2CCallback cb = manager.currentCallback;
    manager.isBusy = false;
    osSemaphoreRelease( manager.mutex );

    if ( cb ) {
        cb( false );
    }
}
