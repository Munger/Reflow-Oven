/// @file SPIManager.c
///
/// @brief SPI bus manager — interrupt-driven and blocking transfer implementation.
///
/// Owns a single SPIManager context wrapping hspi1. All operations are serialised
/// through a binary semaphore (SPIBusSemHandle). CS assertion and de-assertion are
/// managed internally. Async callbacks fire from HAL ISR context; sync calls block
/// the calling task until completion or timeout.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "SPIManager.h"
#include "spi.h"

/// @brief Semaphore created by CubeMX / app_freertos.c — guards exclusive bus access.
extern osSemaphoreId_t SPIBusSemHandle;

/// @brief Private event flag group for SPI bus diagnostic status.
/// @note Replaces the former public SPIStatusFlags extern. Use SPIInitModule() to
///       initialise and the public API status bits to observe.
static osEventFlagsId_t spiStatus;

/// @brief Internal state for the singleton SPI bus manager.
typedef struct {
    SPI_HandleTypeDef* hspi;            ///< Bound HAL handle
    osSemaphoreId_t    mutex;           ///< Semaphore protecting exclusive bus access
    osEventFlagsId_t   flags;           ///< Private diagnostic event flags
    SPICallback        currentCallback; ///< Callback to invoke on transfer completion
    GPIO_TypeDef*      currentCsPort;   ///< CS port for the active transfer
    uint16_t           currentCsPin;    ///< CS pin for the active transfer
    volatile bool      isBusy;          ///< True while a transfer is in flight
} SPIManager;

/// @brief Singleton manager instance — not accessible outside this translation unit.
static SPIManager manager;

static void TransferComplete( SPI_HandleTypeDef* hspi );
static void TransferError( SPI_HandleTypeDef* hspi );

/// @brief Initialise the SPI manager, bind the hardware handle, and register HAL callbacks.
///
/// Verifies the peripheral is configured for 8-bit master mode and sets
/// FlagSPIStatusConfigError if the data-size register does not match.
/// @note Must be called once from task context before any transfers are attempted.
void SPIInitModule( void ) {
    spiStatus = osEventFlagsNew( NULL );

    manager.hspi = &hspi1;
    manager.mutex = SPIBusSemHandle;
    manager.flags = spiStatus;
    manager.isBusy = false;
    manager.currentCallback = NULL;

    if ( manager.flags != NULL ) {
        osEventFlagsClear( manager.flags, 0xFFFFFF );
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusReady ) );
    }

    // Verify peripheral configuration matches our 8-bit / Master requirements
    if ( manager.hspi->Init.DataSize != SPI_DATASIZE_8BIT ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusConfigError ) );
    }

    HAL_SPI_RegisterCallback( manager.hspi, HAL_SPI_RX_COMPLETE_CB_ID, TransferComplete );
    HAL_SPI_RegisterCallback( manager.hspi, HAL_SPI_TX_COMPLETE_CB_ID, TransferComplete );
    HAL_SPI_RegisterCallback( manager.hspi, HAL_SPI_TX_RX_COMPLETE_CB_ID, TransferComplete );
    HAL_SPI_RegisterCallback( manager.hspi, HAL_SPI_ERROR_CB_ID, TransferError );
}

/// @brief Start an asynchronous interrupt-driven SPI read.
///
/// Acquires the bus semaphore with zero timeout. Asserts CS before starting
/// the HAL receive; de-asserts CS automatically from the HAL callback on
/// completion or error.
///
/// @param[in]  csPort GPIO port for the chip-select line.
/// @param[in]  csPin  GPIO pin for the chip-select line.
/// @param[out] pData  Destination buffer; must remain valid until @p cb fires.
/// @param[in]  len    Number of bytes to receive.
/// @param[in]  cb     Completion callback invoked from HAL ISR context.
/// @warning Do not call from ISR context.
void SPIReadAsync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, SPICallback cb ) {
    if ( pData == NULL || len == 0 ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusInvalidParam ) );
        if ( cb ) cb( false );
        return;
    }

    if ( osSemaphoreAcquire( manager.mutex, 0 ) != osOK ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusResourceConflict ) );
        if ( cb ) cb( false );
        return;
    }

    manager.isBusy = true;
    manager.currentCallback = cb;
    manager.currentCsPort = csPort;
    manager.currentCsPin = csPin;

    HAL_GPIO_WritePin( manager.currentCsPort, manager.currentCsPin, GPIO_PIN_RESET );

    if ( HAL_SPI_Receive_IT( manager.hspi, pData, len ) != HAL_OK ) {
        HAL_GPIO_WritePin( manager.currentCsPort, manager.currentCsPin, GPIO_PIN_SET );
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusBusError ) );
        manager.isBusy = false;
        osSemaphoreRelease( manager.mutex );
        if ( cb ) cb( false );
    }
}

/// @brief Start an asynchronous interrupt-driven SPI write.
///
/// Ideal for offloading large flash page programs or bulk device configuration
/// without blocking the calling task.
///
/// @param[in] csPort GPIO port for the chip-select line.
/// @param[in] csPin  GPIO pin for the chip-select line.
/// @param[in] pData  Source buffer; must remain valid until @p cb fires.
/// @param[in] len    Number of bytes to transmit.
/// @param[in] cb     Completion callback invoked from HAL ISR context.
/// @warning Do not call from ISR context.
void SPIWriteAsync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, SPICallback cb ) {
    if ( pData == NULL || len == 0 ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusInvalidParam ) );
        if ( cb ) cb( false );
        return;
    }

    if ( osSemaphoreAcquire( manager.mutex, 0 ) != osOK ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusResourceConflict ) );
        if ( cb ) cb( false );
        return;
    }

    manager.isBusy = true;
    manager.currentCallback = cb;
    manager.currentCsPort = csPort;
    manager.currentCsPin = csPin;

    HAL_GPIO_WritePin( manager.currentCsPort, manager.currentCsPin, GPIO_PIN_RESET );

    if ( HAL_SPI_Transmit_IT( manager.hspi, pData, len ) != HAL_OK ) {
        HAL_GPIO_WritePin( manager.currentCsPort, manager.currentCsPin, GPIO_PIN_SET );
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusBusError ) );
        manager.isBusy = false;
        osSemaphoreRelease( manager.mutex );
        if ( cb ) cb( false );
    }
}

/// @brief Start an atomic asynchronous write-then-read without CS toggling between phases.
///
/// The transfer length passed to HAL is max(txLen, rxLen) so the peripheral
/// clocks the longer phase correctly in full-duplex mode.
///
/// @param[in]  csPort   GPIO port for the chip-select line.
/// @param[in]  csPin    GPIO pin for the chip-select line.
/// @param[in]  pTxData  Transmit buffer; must remain valid until @p cb fires.
/// @param[in]  txLen    Number of bytes to transmit.
/// @param[out] pRxData  Receive buffer; must remain valid until @p cb fires.
/// @param[in]  rxLen    Number of bytes to receive.
/// @param[in]  cb       Completion callback invoked from HAL ISR context.
/// @warning Do not call from ISR context.
void SPITransceiveAsync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pTxData, uint16_t txLen, uint8_t* pRxData, uint16_t rxLen, SPICallback cb ) {
    if ( pTxData == NULL || pRxData == NULL || txLen == 0 || rxLen == 0 ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusInvalidParam ) );
        if ( cb ) cb( false );
        return;
    }

    if ( osSemaphoreAcquire( manager.mutex, 0 ) != osOK ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusResourceConflict ) );
        if ( cb ) cb( false );
        return;
    }

    manager.isBusy = true;
    manager.currentCallback = cb;
    manager.currentCsPort = csPort;
    manager.currentCsPin = csPin;

    HAL_GPIO_WritePin( manager.currentCsPort, manager.currentCsPin, GPIO_PIN_RESET );

    if ( HAL_SPI_TransmitReceive_IT( manager.hspi, pTxData, pRxData, ( txLen > rxLen ) ? txLen : rxLen ) != HAL_OK ) {
        HAL_GPIO_WritePin( manager.currentCsPort, manager.currentCsPin, GPIO_PIN_SET );
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusBusError ) );
        manager.isBusy = false;
        osSemaphoreRelease( manager.mutex );
        if ( cb ) cb( false );
    }
}

/// @brief Perform a synchronous blocking SPI read.
/// @param[in]  csPort   GPIO port for the chip-select line.
/// @param[in]  csPin    GPIO pin for the chip-select line.
/// @param[out] pData    Destination buffer.
/// @param[in]  len      Number of bytes to receive.
/// @param[in]  timeout  Maximum wait in milliseconds.
/// @return true on success, false on timeout or bus error.
/// @warning Only call from task context, not ISR.
bool SPIReadSync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, uint32_t timeout ) {
    if ( pData == NULL || len == 0 ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusInvalidParam ) );
        return false;
    }

    if ( osSemaphoreAcquire( manager.mutex, timeout ) != osOK ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusTimeout ) );
        return false;
    }

    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_RESET );
    HAL_StatusTypeDef status = HAL_SPI_Receive( manager.hspi, pData, len, timeout );
    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_SET );

    if ( status != HAL_OK ) {
        osEventFlagsSet( manager.flags, ( status == HAL_TIMEOUT ) ? BIT( FlagSPIStatusTimeout ) : BIT( FlagSPIStatusBusError ) );
    }

    osSemaphoreRelease( manager.mutex );
    return ( status == HAL_OK );
}

/// @brief Perform a synchronous blocking SPI write.
/// @param[in] csPort   GPIO port for the chip-select line.
/// @param[in] csPin    GPIO pin for the chip-select line.
/// @param[in] pData    Source buffer.
/// @param[in] len      Number of bytes to transmit.
/// @param[in] timeout  Maximum wait in milliseconds.
/// @return true on success, false on timeout or bus error.
/// @warning Only call from task context, not ISR.
bool SPIWriteSync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, uint32_t timeout ) {
    if ( pData == NULL || len == 0 ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusInvalidParam ) );
        return false;
    }

    if ( osSemaphoreAcquire( manager.mutex, timeout ) != osOK ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusTimeout ) );
        return false;
    }

    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_RESET );
    HAL_StatusTypeDef status = HAL_SPI_Transmit( manager.hspi, pData, len, timeout );
    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_SET );

    if ( status != HAL_OK ) {
        osEventFlagsSet( manager.flags, ( status == HAL_TIMEOUT ) ? BIT( FlagSPIStatusTimeout ) : BIT( FlagSPIStatusBusError ) );
    }

    osSemaphoreRelease( manager.mutex );
    return ( status == HAL_OK );
}

/// @brief Perform an atomic synchronous write-then-read without CS toggling between phases.
/// @param[in]  csPort   GPIO port for the chip-select line.
/// @param[in]  csPin    GPIO pin for the chip-select line.
/// @param[in]  pTxData  Transmit buffer.
/// @param[in]  txLen    Number of bytes to transmit.
/// @param[out] pRxData  Receive buffer.
/// @param[in]  rxLen    Number of bytes to receive.
/// @param[in]  timeout  Maximum wait in milliseconds.
/// @return true on success, false on timeout or bus error.
/// @warning Only call from task context, not ISR.
bool SPITransceiveSync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pTxData, uint16_t txLen, uint8_t* pRxData, uint16_t rxLen, uint32_t timeout ) {
    if ( pTxData == NULL || pRxData == NULL || txLen == 0 || rxLen == 0 ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusInvalidParam ) );
        return false;
    }

    if ( osSemaphoreAcquire( manager.mutex, timeout ) != osOK ) {
        osEventFlagsSet( manager.flags, BIT( FlagSPIStatusTimeout ) );
        return false;
    }

    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_RESET );

    HAL_StatusTypeDef status = HAL_SPI_Transmit( manager.hspi, pTxData, txLen, timeout );
    if ( status == HAL_OK ) {
        status = HAL_SPI_Receive( manager.hspi, pRxData, rxLen, timeout );
    }

    HAL_GPIO_WritePin( csPort, csPin, GPIO_PIN_SET );

    if ( status != HAL_OK ) {
        osEventFlagsSet( manager.flags, ( status == HAL_TIMEOUT ) ? BIT( FlagSPIStatusTimeout ) : BIT( FlagSPIStatusBusError ) );
    }

    osSemaphoreRelease( manager.mutex );
    return ( status == HAL_OK );
}

/// @brief HAL callback invoked on successful transfers.
///
/// De-asserts CS, clears transient error flags, releases the semaphore, and
/// fires the caller's completion callback with success=true.
///
/// @param[in] hspi HAL handle (unused; singleton manager is used directly).
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void TransferComplete( SPI_HandleTypeDef* hspi ) {
    UNUSED( hspi );

    HAL_GPIO_WritePin( manager.currentCsPort, manager.currentCsPin, GPIO_PIN_SET );

    SPICallback cb = manager.currentCallback;
    manager.isBusy = false;

    if ( manager.flags != NULL ) {
        osEventFlagsClear( manager.flags, BIT( FlagSPIStatusBusError ) | BIT( FlagSPIStatusOverrun ) );
    }

    osSemaphoreRelease( manager.mutex );
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
/// @param[in] hspi HAL handle used to retrieve the HAL error code.
/// @warning Called from HAL ISR context. Must not call any FreeRTOS blocking API.
static void TransferError( SPI_HandleTypeDef* hspi ) {
    uint32_t err = HAL_SPI_GetError( hspi );

    HAL_GPIO_WritePin( manager.currentCsPort, manager.currentCsPin, GPIO_PIN_SET );

    if ( manager.flags != NULL ) {
        if ( err & HAL_SPI_ERROR_OVR )  osEventFlagsSet( manager.flags, BIT( FlagSPIStatusOverrun ) );
        if ( err & HAL_SPI_ERROR_CRC )  osEventFlagsSet( manager.flags, BIT( FlagSPIStatusCRCError ) );
        if ( err & HAL_SPI_ERROR_DMA )  osEventFlagsSet( manager.flags, BIT( FlagSPIStatusDMAError ) );
        if ( err & HAL_SPI_ERROR_MODF ) osEventFlagsSet( manager.flags, BIT( FlagSPIStatusBusError ) );
    }

    osEventFlagsSet( FaultFlagsHandle, BIT( FlagSPIFault ) );

    SPICallback cb = manager.currentCallback;
    manager.isBusy = false;
    osSemaphoreRelease( manager.mutex );

    if ( cb ) {
        cb( false );
    }
}
