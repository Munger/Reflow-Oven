#ifndef SPIMANAGER_H
#define SPIMANAGER_H

#include "SystemStatusFlags.h"
#include "types.h"
#include "main.h"

typedef enum {
    FlagSPIStatusReady = 0,
    FlagSPIStatusBusError,
    FlagSPIStatusOverrun,
    FlagSPIStatusTimeout,
    FlagSPIStatusCRCError,
    FlagSPIStatusDMAError,
    FlagSPIStatusResourceConflict, // High-priority diagnostic for RTOS task collisions
    FlagSPIStatusInvalidParam,
    FlagSPIStatusConfigError,       // Diagnostic for peripheral register corruption
    
    SPIFlagsCount
} SPIStatusBit;

extern osEventFlagsId_t SPIStatusFlags;

_Static_assert( SPIFlagsCount <= 24, "SPIStatusFlags out of bounds" );

typedef void ( *SPICallback )( bool success );

// Lifecycle
void SPIInitModule( void );

// --- Asynchronous API (Non-blocking) ---
void SPIReadAsync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, SPICallback cb );
void SPIWriteAsync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, SPICallback cb );
void SPITransceiveAsync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pTxData, uint16_t txLen, uint8_t* pRxData, uint16_t rxLen, SPICallback cb );

// --- Synchronous API (Blocking) ---
bool SPIWriteSync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, uint32_t timeout );
bool SPIReadSync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, uint32_t timeout );
bool SPITransceiveSync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pTxData, uint16_t txLen, uint8_t* pRxData, uint16_t rxLen, uint32_t timeout );

#endif // SPIMANAGER_H