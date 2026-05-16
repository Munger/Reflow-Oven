#ifndef I2CMANAGER_H
#define I2CMANAGER_H

#include "SystemStatusFlags.h"
#include "types.h"
#include "main.h"

typedef enum {
    FlagI2C1StatusReady = 0,
    FlagI2C1StatusBusError,
    FlagI2C1StatusArbitrationLost,
    FlagI2C1StatusTimeout,
    FlagI2C1StatusLocked, // SDA held low by a peripheral

    I2C1FlagsCount
} I2CStatusBit;

extern osEventFlagsId_t I2CStatusFlags;

_Static_assert( I2C1FlagsCount <= 24, "I2CStatusFlags out of bounds" );

typedef void ( *I2CCallback )( bool success );

// Lifecycle
void              I2CInitModule( void );

// Asynchronous Memory Read (Interrupt-driven)
HAL_StatusTypeDef I2CReadAsync( uint16_t devAddr, uint16_t memAddr, uint16_t size, uint8_t* pData, uint16_t len,
                                   I2CCallback cb );

// Synchronous Memory Write (Blocking)
HAL_StatusTypeDef I2CWriteSync( uint16_t devAddr, uint16_t memAddr, uint16_t size, uint8_t* pData, uint16_t len,
                                 uint32_t timeout );

#endif // I2CMANAGER_H