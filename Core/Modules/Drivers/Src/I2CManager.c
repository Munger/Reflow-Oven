/// @file I2CManager.c
///
/// @brief I2C bus manager — multi-instance interrupt-driven and blocking transfer implementation.
///
/// Each I2C bus is represented by a struct I2CInstance. I2CInitModule() binds all
/// hardware handles and registers HAL callbacks. I2COpen() returns an opaque I2CRef
/// that callers pass to every transfer function — no HAL handle or bus number appears
/// at call sites. All operations are serialised through a per-instance semaphore.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "I2CManager.h"
#include "i2c.h"

/// @brief Semaphore created by CubeMX / app_freertos.c — guards exclusive I2CBus1 access.
extern osSemaphoreId_t I2CBusSemHandle;

/// @brief Full internal state of one I2C bus instance.
typedef struct I2CInstance {
    I2CID              id;              ///< Bus identifier
    I2C_HandleTypeDef* hi2c;           ///< Bound HAL handle
    osSemaphoreId_t    mutex;           ///< Semaphore protecting exclusive bus access
    osEventFlagsId_t   statusHandle;    ///< Per-instance diagnostic event flags
    I2CCallback        currentCallback; ///< Callback to invoke when the current transfer completes
} I2CInstance, *I2CInstancePtr;

/// @brief All I2C bus instances — indexed by I2CID.
static I2CInstance instances[ I2CBusCount ];

static void TransferComplete( I2C_HandleTypeDef* hi2c );
static void TransferError( I2C_HandleTypeDef* hi2c );

/// @brief Find the instance whose bound handle matches @p hi2c.
/// @return Pointer to the matching instance, or NULL if not found.
static I2CInstancePtr FindByHandle( I2C_HandleTypeDef* hi2c ) {
    for ( uint8_t i = 0; i < I2CBusCount; i++ ) {
        if ( instances[ i ].hi2c == hi2c ) return &instances[ i ];
    }
    return NULL;
}

/// @brief Initialise all I2C bus instances, bind hardware handles, and register HAL callbacks.
/// @note Must be called once from task context before any transfers are attempted.
void I2CInitModule( void ) {
    I2CInstancePtr i2c = &instances[ I2CBus1 ];
    i2c->id              = I2CBus1;
    i2c->hi2c            = &hi2c1;
    i2c->mutex           = I2CBusSemHandle;
    i2c->statusHandle    = osEventFlagsNew( NULL );
    i2c->currentCallback = NULL;

    if ( i2c->statusHandle != NULL ) {
        osEventFlagsClear( i2c->statusHandle, 0xFFFFFF );
        osEventFlagsSet( i2c->statusHandle, BIT( FlagI2CStatusReady ) );
    }

    HAL_I2C_RegisterCallback( i2c->hi2c, HAL_I2C_MEM_RX_COMPLETE_CB_ID,    TransferComplete );
    HAL_I2C_RegisterCallback( i2c->hi2c, HAL_I2C_MASTER_RX_COMPLETE_CB_ID, TransferComplete );
    HAL_I2C_RegisterCallback( i2c->hi2c, HAL_I2C_ERROR_CB_ID,              TransferError );
}

/// @brief Return a handle to a specific I2C bus instance.
I2CRef I2COpen( I2CID id ) {
    if ( id >= I2CBusCount ) return NULL;
    return &instances[ id ];
}

/// @brief Return the full status bitmask for a specific I2C bus instance.
uint32_t I2CGetStatus( I2CRef i2c ) {
    return ( i2c != NULL ) ? osEventFlagsGet( i2c->statusHandle ) : 0;
}

/// @brief Start an asynchronous interrupt-driven memory read.
///
/// Acquires the bus semaphore with zero timeout (non-blocking). If the bus is
/// already in use, returns HAL_BUSY immediately without modifying state.
/// On HAL failure, the semaphore is released and the status flags are updated.
/// @warning Do not call from ISR context.
HAL_StatusTypeDef I2CReadAsync( I2CRef i2c, uint16_t devAddr, uint16_t memAddr, uint16_t size,
                                uint8_t* pData, uint16_t len, I2CCallback cb ) {
    if ( i2c == NULL ) return HAL_ERROR;

    if ( osSemaphoreAcquire( i2c->mutex, 0 ) != osOK ) {
        return HAL_BUSY;
    }

    i2c->currentCallback = cb;

    HAL_StatusTypeDef status = HAL_I2C_Mem_Read_IT( i2c->hi2c, devAddr, memAddr, size, pData, len );

    if ( status != HAL_OK ) {
        osSemaphoreRelease( i2c->mutex );

        if ( status == HAL_BUSY && i2c->statusHandle != NULL ) {
            osEventFlagsSet( i2c->statusHandle, BIT( FlagI2CStatusLocked ) );
        }
    }

    return status;
}

/// @brief Start an asynchronous interrupt-driven master receive (no register address).
///
/// Use this for read-only devices that have no register address byte in their
/// protocol (e.g. MCP3221). Acquires the bus semaphore with zero timeout — if the
/// bus is in use, returns HAL_BUSY immediately.
/// @warning Do not call from ISR context.
HAL_StatusTypeDef I2CReceiveAsync( I2CRef i2c, uint16_t devAddr,
                                   uint8_t* pData, uint16_t len, I2CCallback cb ) {
    if ( i2c == NULL ) return HAL_ERROR;

    if ( osSemaphoreAcquire( i2c->mutex, 0 ) != osOK ) {
        return HAL_BUSY;
    }

    i2c->currentCallback = cb;

    HAL_StatusTypeDef status = HAL_I2C_Master_Receive_IT( i2c->hi2c, devAddr, pData, len );

    if ( status != HAL_OK ) {
        osSemaphoreRelease( i2c->mutex );

        if ( status == HAL_BUSY && i2c->statusHandle != NULL ) {
            osEventFlagsSet( i2c->statusHandle, BIT( FlagI2CStatusLocked ) );
        }
    }

    return status;
}

/// @brief Perform a synchronous blocking master receive (no register address).
///
/// Use this for read-only devices that have no register address byte in their
/// protocol (e.g. MCP3221). Acquires the bus semaphore for up to @p timeout
/// milliseconds.
/// @warning Only call from task context, not ISR.
HAL_StatusTypeDef I2CReceiveSync( I2CRef i2c, uint16_t devAddr,
                                  uint8_t* pData, uint16_t len, uint32_t timeout ) {
    if ( i2c == NULL ) return HAL_ERROR;

    if ( osSemaphoreAcquire( i2c->mutex, timeout ) != osOK ) {
        return HAL_BUSY;
    }

    HAL_StatusTypeDef status = HAL_I2C_Master_Receive( i2c->hi2c, devAddr, pData, len, timeout );

    if ( status != HAL_OK && i2c->statusHandle != NULL ) {
        osEventFlagsSet( i2c->statusHandle, BIT( FlagI2CStatusBusError ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagI2CFault ) );
    }

    osSemaphoreRelease( i2c->mutex );
    return status;
}

/// @brief Perform a synchronous blocking memory read.
///
/// Acquires the bus semaphore for up to @p timeout milliseconds. On success,
/// issues a HAL blocking read and releases the semaphore on return.
/// Any HAL error is reflected in the local status flags and in FaultFlagsHandle.
/// @warning Only call from task context, not ISR.
HAL_StatusTypeDef I2CReadSync( I2CRef i2c, uint16_t devAddr, uint16_t memAddr, uint16_t size,
                               uint8_t* pData, uint16_t len, uint32_t timeout ) {
    if ( i2c == NULL ) return HAL_ERROR;

    if ( osSemaphoreAcquire( i2c->mutex, timeout ) != osOK ) {
        return HAL_BUSY;
    }

    HAL_StatusTypeDef status = HAL_I2C_Mem_Read( i2c->hi2c, devAddr, memAddr, size, pData, len, timeout );

    if ( status != HAL_OK && i2c->statusHandle != NULL ) {
        osEventFlagsSet( i2c->statusHandle, BIT( FlagI2CStatusBusError ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagI2CFault ) );
    }

    osSemaphoreRelease( i2c->mutex );
    return status;
}

/// @brief Perform a synchronous blocking memory write.
///
/// Acquires the bus semaphore for up to @p timeout milliseconds. On success,
/// issues a HAL blocking write and releases the semaphore on return.
/// Any HAL error is reflected in the local status flags and in FaultFlagsHandle.
/// @warning Only call from task context, not ISR.
HAL_StatusTypeDef I2CWriteSync( I2CRef i2c, uint16_t devAddr, uint16_t memAddr, uint16_t size,
                                uint8_t* pData, uint16_t len, uint32_t timeout ) {
    if ( i2c == NULL ) return HAL_ERROR;

    if ( osSemaphoreAcquire( i2c->mutex, timeout ) != osOK ) {
        return HAL_BUSY;
    }

    HAL_StatusTypeDef status = HAL_I2C_Mem_Write( i2c->hi2c, devAddr, memAddr, size, pData, len, timeout );

    if ( status != HAL_OK && i2c->statusHandle != NULL ) {
        osEventFlagsSet( i2c->statusHandle, BIT( FlagI2CStatusBusError ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagI2CFault ) );
    }

    osSemaphoreRelease( i2c->mutex );
    return status;
}

/// @brief HAL callback invoked on successful interrupt transfers.
///
/// Clears transient error flags, releases the bus semaphore, and fires the
/// caller's completion callback with success=true.
///
/// @param[in] hi2c HAL handle; used to locate the correct instance.
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void TransferComplete( I2C_HandleTypeDef* hi2c ) {
    I2CInstancePtr i2c = FindByHandle( hi2c );
    if ( i2c == NULL ) return;

    I2CCallback cb = i2c->currentCallback;

    if ( i2c->statusHandle != NULL ) {
        osEventFlagsClear( i2c->statusHandle, BIT( FlagI2CStatusBusError ) | BIT( FlagI2CStatusLocked ) );
    }

    osSemaphoreRelease( i2c->mutex );
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
/// @param[in] hi2c HAL handle; used to locate the instance and retrieve the error code.
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void TransferError( I2C_HandleTypeDef* hi2c ) {
    I2CInstancePtr i2c = FindByHandle( hi2c );
    if ( i2c == NULL ) return;

    uint32_t err = HAL_I2C_GetError( hi2c );

    if ( i2c->statusHandle != NULL ) {
        if ( err & ( HAL_I2C_ERROR_AF | HAL_I2C_ERROR_BERR ) ) {
            osEventFlagsSet( i2c->statusHandle, BIT( FlagI2CStatusBusError ) );
        }
        if ( err & HAL_I2C_ERROR_ARLO ) {
            osEventFlagsSet( i2c->statusHandle, BIT( FlagI2CStatusArbitrationLost ) );
        }
    }

    osEventFlagsSet( FaultFlagsHandle, BIT( FlagI2CFault ) );

    I2CCallback cb = i2c->currentCallback;
    osSemaphoreRelease( i2c->mutex );

    if ( cb ) {
        cb( false );
    }
}
