#include "SPIManager.h"
#include "spi.h"

extern osSemaphoreId_t SPIBusSemHandle;

typedef struct {
    SPI_HandleTypeDef* hspi;
    osSemaphoreId_t    mutex;
    osEventFlagsId_t   flags;
    SPICallback        currentCallback;
    GPIO_TypeDef* currentCsPort;
    uint16_t           currentCsPin;
    volatile bool      isBusy;
} SPIManager;

static SPIManager manager;

static void TransferComplete( SPI_HandleTypeDef* hspi );
static void TransferError( SPI_HandleTypeDef* hspi );

// Initialises the SPI manager, binds hardware handles, and registers HAL callbacks
void SPIInitModule( void ) {
    manager.hspi = &hspi1;
    manager.mutex = SPIBusSemHandle;
    manager.flags = SPIStatusFlags;
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

// Starts an asynchronous interrupt-driven read; manages CS internally and executes callback on completion
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

// Starts an asynchronous interrupt-driven write; ideal for offloading large flash page programs
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

// Atomic asynchronous write-then-read; prevents CS toggling and offloads large data transfers
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

// Performs a synchronous blocking read; manages CS and releases bus upon completion or timeout
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

// Performs a synchronous blocking write; manages CS and releases bus upon completion or timeout
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

// Performs an atomic synchronous write-then-read
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

// Internal HAL callback for successful transfers; handles CS de-assertion and semaphore release
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

// Internal HAL callback for failed SPI transfers; updates system fault flags and cleans up hardware state
static void TransferError( SPI_HandleTypeDef* hspi ) {
    uint32_t err = HAL_SPI_GetError( hspi );

    HAL_GPIO_WritePin( manager.currentCsPort, manager.currentCsPin, GPIO_PIN_SET );

    if ( manager.flags != NULL ) {
        if ( err & HAL_SPI_ERROR_OVR ) osEventFlagsSet( manager.flags, BIT( FlagSPIStatusOverrun ) );
        if ( err & HAL_SPI_ERROR_CRC ) osEventFlagsSet( manager.flags, BIT( FlagSPIStatusCRCError ) );
        if ( err & HAL_SPI_ERROR_DMA ) osEventFlagsSet( manager.flags, BIT( FlagSPIStatusDMAError ) );
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