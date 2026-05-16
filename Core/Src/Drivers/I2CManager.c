#include "I2CManager.h"
#include "i2c.h"

extern osSemaphoreId_t I2CBusSemHandle;

typedef struct {
    I2C_HandleTypeDef* hi2c;
    osSemaphoreId_t    mutex;
    osEventFlagsId_t   flags;
    I2CCallback        currentCallback;
    volatile bool      isBusy;
} I2CManager;

static I2CManager manager;

static void TransferComplete( I2C_HandleTypeDef* hi2c );
static void TransferError( I2C_HandleTypeDef* hi2c );

// Initialises the I2C manager, binds hardware handles, and registers HAL callbacks
void I2CInitModule( void ) {
    manager.hi2c = &hi2c1;
    manager.mutex = I2CBusSemHandle;
    manager.flags = I2CStatusFlags;
    manager.isBusy = false;
    manager.currentCallback = NULL;

    if ( manager.flags != NULL ) {
        osEventFlagsClear( manager.flags, 0xFFFFFF );
        osEventFlagsSet( manager.flags, BIT( FlagI2C1StatusReady ) );
    }

    HAL_I2C_RegisterCallback( manager.hi2c, HAL_I2C_MEM_RX_COMPLETE_CB_ID, TransferComplete );
    HAL_I2C_RegisterCallback( manager.hi2c, HAL_I2C_ERROR_CB_ID, TransferError );
}

// Starts an asynchronous interrupt-driven memory read and executes the callback on completion
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

// Performs a synchronous blocking memory write within the specified timeout period
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

// Internal HAL callback for successful interrupt transfers; clears error flags and releases semaphore
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

// Internal HAL callback for failed transfers; updates system fault flags and releases semaphore
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