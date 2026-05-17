/// @file SPIManager.c
///
/// @brief SPI bus manager — multi-instance interrupt-driven and blocking transfer implementation.
///
/// Each SPI bus is represented by a struct SPIInstance. SPIInitModule() binds all
/// hardware handles and registers HAL callbacks. SPIOpen() returns an opaque SPIRef
/// that callers pass to every transfer function — no HAL handle or bus number appears
/// at call sites. All operations are serialised through a per-instance semaphore.
/// CS assertion and de-assertion are managed internally.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "SPIManager.h"
#include "spi.h"

/// @brief Semaphore created by CubeMX / app_freertos.c — guards exclusive SPIBus1 access.
extern osSemaphoreId_t SPIBusSemHandle;

/// @brief Full internal state of one SPI bus instance.
typedef struct SPIInstance {
    SPIID              id;              ///< Bus identifier
    SPI_HandleTypeDef* hspi;            ///< Bound HAL handle
    osSemaphoreId_t    mutex;           ///< Semaphore protecting exclusive bus access
    osEventFlagsId_t   statusHandle;    ///< Per-instance diagnostic event flags
    SPICallback        currentCallback; ///< Callback to invoke on transfer completion
    GPIO_TypeDef*      currentCsPort;   ///< CS port for the active transfer
    uint16_t           currentCsPin;    ///< CS pin for the active transfer
} SPIInstance, *SPIInstancePtr;

/// @brief All SPI bus instances — indexed by SPIID.
static SPIInstance instances[ SPIBusCount ];

static void TransferComplete( SPI_HandleTypeDef* hspi );
static void TransferError( SPI_HandleTypeDef* hspi );

/// @brief Find the instance whose bound handle matches @p hspi.
/// @return Pointer to the matching instance, or NULL if not found.
static SPIInstancePtr FindByHandle( SPI_HandleTypeDef* hspi ) {
    for ( uint8_t i = 0; i < SPIBusCount; i++ ) {
        if ( instances[ i ].hspi == hspi ) return &instances[ i ];
    }
    return NULL;
}

/// @brief Initialise all SPI bus instances, bind hardware handles, and register HAL callbacks.
///
/// Verifies each peripheral is configured for 8-bit master mode and sets
/// FlagSPIStatusConfigError if the data-size register does not match.
/// @note Must be called once from task context before any transfers are attempted.
void SPIInitModule( void ) {
    SPIInstancePtr spi = &instances[ SPIBus1 ];
    spi->id            = SPIBus1;
    spi->hspi          = &hspi1;
    spi->mutex         = SPIBusSemHandle;
    spi->statusHandle  = osEventFlagsNew( NULL );
    spi->currentCallback = NULL;

    if ( spi->statusHandle != NULL ) {
        osEventFlagsClear( spi->statusHandle, 0xFFFFFF );
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusReady ) );
    }

    if ( spi->hspi->Init.DataSize != SPI_DATASIZE_8BIT ) {
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusConfigError ) );
    }

    HAL_SPI_RegisterCallback( spi->hspi, HAL_SPI_RX_COMPLETE_CB_ID,    TransferComplete );
    HAL_SPI_RegisterCallback( spi->hspi, HAL_SPI_TX_COMPLETE_CB_ID,    TransferComplete );
    HAL_SPI_RegisterCallback( spi->hspi, HAL_SPI_TX_RX_COMPLETE_CB_ID, TransferComplete );
    HAL_SPI_RegisterCallback( spi->hspi, HAL_SPI_ERROR_CB_ID,          TransferError );
}

/// @brief Return a handle to a specific SPI bus instance.
SPIRef SPIOpen( SPIID id ) {
    if ( id >= SPIBusCount ) return NULL;
    return &instances[ id ];
}

/// @brief Return the full status bitmask for a specific SPI bus instance.
uint32_t SPIGetStatus( SPIRef spi ) {
    return ( spi != NULL ) ? osEventFlagsGet( spi->statusHandle ) : 0;
}

/// @brief Start an asynchronous interrupt-driven SPI read.
///
/// Acquires the bus semaphore with zero timeout. Asserts CS before starting
/// the HAL receive; de-asserts CS automatically from the HAL callback on
/// completion or error.
/// @warning Do not call from ISR context.
void SPIReadAsync( SPIRef spi, GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, SPICallback cb ) {
    if ( spi == NULL || pData == NULL || len == 0 ) {
        if ( spi != NULL ) osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusInvalidParam ) );
        if ( cb ) cb( false );
        return;
    }

    if ( osSemaphoreAcquire( spi->mutex, 0 ) != osOK ) {
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusResourceConflict ) );
        if ( cb ) cb( false );
        return;
    }

    spi->currentCallback = cb;
    spi->currentCsPort   = csPort;
    spi->currentCsPin    = csPin;

    HAL_GPIO_WritePin( spi->currentCsPort, spi->currentCsPin, GPIO_PIN_RESET );

    if ( HAL_SPI_Receive_IT( spi->hspi, pData, len ) != HAL_OK ) {
        HAL_GPIO_WritePin( spi->currentCsPort, spi->currentCsPin, GPIO_PIN_SET );
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusBusError ) );
        osSemaphoreRelease( spi->mutex );
        if ( cb ) cb( false );
    }
}

/// @brief Start an asynchronous interrupt-driven SPI write.
/// @warning Do not call from ISR context.
void SPIWriteAsync( SPIRef spi, GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, SPICallback cb ) {
    if ( spi == NULL || pData == NULL || len == 0 ) {
        if ( spi != NULL ) osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusInvalidParam ) );
        if ( cb ) cb( false );
        return;
    }

    if ( osSemaphoreAcquire( spi->mutex, 0 ) != osOK ) {
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusResourceConflict ) );
        if ( cb ) cb( false );
        return;
    }

    spi->currentCallback = cb;
    spi->currentCsPort   = csPort;
    spi->currentCsPin    = csPin;

    HAL_GPIO_WritePin( spi->currentCsPort, spi->currentCsPin, GPIO_PIN_RESET );

    if ( HAL_SPI_Transmit_IT( spi->hspi, pData, len ) != HAL_OK ) {
        HAL_GPIO_WritePin( spi->currentCsPort, spi->currentCsPin, GPIO_PIN_SET );
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusBusError ) );
        osSemaphoreRelease( spi->mutex );
        if ( cb ) cb( false );
    }
}

/// @brief Start an atomic asynchronous write-then-read without CS toggling between phases.
///
/// The transfer length passed to HAL is max(txLen, rxLen) so the peripheral
/// clocks the longer phase correctly in full-duplex mode.
/// @warning Do not call from ISR context.
void SPITransceiveAsync( SPIRef spi, GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pTxData, uint16_t txLen, uint8_t* pRxData, uint16_t rxLen, SPICallback cb ) {
    if ( spi == NULL || pTxData == NULL || pRxData == NULL || txLen == 0 || rxLen == 0 ) {
        if ( spi != NULL ) osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusInvalidParam ) );
        if ( cb ) cb( false );
        return;
    }

    if ( osSemaphoreAcquire( spi->mutex, 0 ) != osOK ) {
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusResourceConflict ) );
        if ( cb ) cb( false );
        return;
    }

    spi->currentCallback = cb;
    spi->currentCsPort   = csPort;
    spi->currentCsPin    = csPin;

    HAL_GPIO_WritePin( spi->currentCsPort, spi->currentCsPin, GPIO_PIN_RESET );

    if ( HAL_SPI_TransmitReceive_IT( spi->hspi, pTxData, pRxData, ( txLen > rxLen ) ? txLen : rxLen ) != HAL_OK ) {
        HAL_GPIO_WritePin( spi->currentCsPort, spi->currentCsPin, GPIO_PIN_SET );
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusBusError ) );
        osSemaphoreRelease( spi->mutex );
        if ( cb ) cb( false );
    }
}

/// @brief Perform a synchronous blocking SPI read.
/// @warning Only call from task context, not ISR.
bool SPIReadSync( SPIRef spi, GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, uint32_t timeout ) {
    if ( spi == NULL || pData == NULL || len == 0 ) {
        if ( spi != NULL ) osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusInvalidParam ) );
        return false;
    }

    if ( osSemaphoreAcquire( spi->mutex, timeout ) != osOK ) {
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusTimeout ) );
        return false;
    }

    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_RESET );
    HAL_StatusTypeDef status = HAL_SPI_Receive( spi->hspi, pData, len, timeout );
    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_SET );

    if ( status != HAL_OK ) {
        osEventFlagsSet( spi->statusHandle, ( status == HAL_TIMEOUT ) ? BIT( FlagSPIStatusTimeout ) : BIT( FlagSPIStatusBusError ) );
    }

    osSemaphoreRelease( spi->mutex );
    return ( status == HAL_OK );
}

/// @brief Perform a synchronous blocking SPI write.
/// @warning Only call from task context, not ISR.
bool SPIWriteSync( SPIRef spi, GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, uint32_t timeout ) {
    if ( spi == NULL || pData == NULL || len == 0 ) {
        if ( spi != NULL ) osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusInvalidParam ) );
        return false;
    }

    if ( osSemaphoreAcquire( spi->mutex, timeout ) != osOK ) {
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusTimeout ) );
        return false;
    }

    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_RESET );
    HAL_StatusTypeDef status = HAL_SPI_Transmit( spi->hspi, pData, len, timeout );
    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_SET );

    if ( status != HAL_OK ) {
        osEventFlagsSet( spi->statusHandle, ( status == HAL_TIMEOUT ) ? BIT( FlagSPIStatusTimeout ) : BIT( FlagSPIStatusBusError ) );
    }

    osSemaphoreRelease( spi->mutex );
    return ( status == HAL_OK );
}

/// @brief Perform an atomic synchronous write-then-read without CS toggling between phases.
/// @warning Only call from task context, not ISR.
bool SPITransceiveSync( SPIRef spi, GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pTxData, uint16_t txLen, uint8_t* pRxData, uint16_t rxLen, uint32_t timeout ) {
    if ( spi == NULL || pTxData == NULL || pRxData == NULL || txLen == 0 || rxLen == 0 ) {
        if ( spi != NULL ) osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusInvalidParam ) );
        return false;
    }

    if ( osSemaphoreAcquire( spi->mutex, timeout ) != osOK ) {
        osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusTimeout ) );
        return false;
    }

    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_RESET );

    HAL_StatusTypeDef status = HAL_SPI_Transmit( spi->hspi, pTxData, txLen, timeout );
    if ( status == HAL_OK ) {
        status = HAL_SPI_Receive( spi->hspi, pRxData, rxLen, timeout );
    }

    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_SET );

    if ( status != HAL_OK ) {
        osEventFlagsSet( spi->statusHandle, ( status == HAL_TIMEOUT ) ? BIT( FlagSPIStatusTimeout ) : BIT( FlagSPIStatusBusError ) );
    }

    osSemaphoreRelease( spi->mutex );
    return ( status == HAL_OK );
}

/// @brief HAL callback invoked on successful transfers.
///
/// De-asserts CS, clears transient error flags, releases the semaphore, and
/// fires the caller's completion callback with success=true.
///
/// @param[in] hspi HAL handle; used to locate the correct instance.
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void TransferComplete( SPI_HandleTypeDef* hspi ) {
    SPIInstancePtr spi = FindByHandle( hspi );
    if ( spi == NULL ) return;

    HAL_GPIO_WritePin( spi->currentCsPort, spi->currentCsPin, GPIO_PIN_SET );

    SPICallback cb = spi->currentCallback;

    if ( spi->statusHandle != NULL ) {
        osEventFlagsClear( spi->statusHandle, BIT( FlagSPIStatusBusError ) | BIT( FlagSPIStatusOverrun ) );
    }

    osSemaphoreRelease( spi->mutex );
    if ( cb ) {
        cb( true );
    }
}

/// @brief HAL callback invoked on failed SPI transfers.
///
/// De-asserts CS, decodes the HAL error word into diagnostic flags, raises
/// FlagSPIFault in the global fault group, releases the semaphore, and fires
/// the caller's completion callback with success=false.
///
/// @param[in] hspi HAL handle used to locate the instance and retrieve the error code.
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void TransferError( SPI_HandleTypeDef* hspi ) {
    SPIInstancePtr spi = FindByHandle( hspi );
    if ( spi == NULL ) return;

    uint32_t err = HAL_SPI_GetError( hspi );

    HAL_GPIO_WritePin( spi->currentCsPort, spi->currentCsPin, GPIO_PIN_SET );

    if ( spi->statusHandle != NULL ) {
        if ( err & HAL_SPI_ERROR_OVR )  osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusOverrun ) );
        if ( err & HAL_SPI_ERROR_CRC )  osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusCRCError ) );
        if ( err & HAL_SPI_ERROR_DMA )  osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusDMAError ) );
        if ( err & HAL_SPI_ERROR_MODF ) osEventFlagsSet( spi->statusHandle, BIT( FlagSPIStatusBusError ) );
    }

    osEventFlagsSet( FaultFlagsHandle, BIT( FlagSPIFault ) );

    SPICallback cb = spi->currentCallback;
    osSemaphoreRelease( spi->mutex );

    if ( cb ) {
        cb( false );
    }
}
