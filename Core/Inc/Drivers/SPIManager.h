/// @file SPIManager.h
///
/// @brief SPI bus manager with asynchronous and synchronous transfer APIs.
///
/// Wraps HAL SPI interrupt-driven and blocking transfers behind a single
/// semaphore-guarded interface. CS line management is performed internally.
/// Callers supply a callback for async completion notification.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef SPIMANAGER_H
#define SPIMANAGER_H

#include "SystemStatusFlags.h"
#include "Types.h"
#include "main.h"

/// @brief Status and diagnostic flag bit positions for the SPI bus.
/// These map 1:1 to the bits in the private spiStatus event flag group.
typedef enum {
    FlagSPIStatusReady = 0,           ///< Bus initialised and ready for transfers
    FlagSPIStatusBusError,            ///< MODF or general HAL bus error
    FlagSPIStatusOverrun,             ///< RX overrun (OVR)
    FlagSPIStatusTimeout,             ///< Semaphore or transfer timeout
    FlagSPIStatusCRCError,            ///< CRC mismatch on received data
    FlagSPIStatusDMAError,            ///< DMA transfer error
    FlagSPIStatusResourceConflict,    ///< High-priority diagnostic for RTOS task collisions
    FlagSPIStatusInvalidParam,        ///< NULL pointer or zero-length buffer passed to API
    FlagSPIStatusConfigError,         ///< Peripheral register corruption detected at init

    SPIFlagsCount
} SPIStatusBit;

_Static_assert( SPIFlagsCount <= 24, "SPIStatusFlags out of bounds" );

/// @brief Completion callback type for asynchronous SPI operations.
/// @param[in] success true if the transfer completed without error, false otherwise.
/// @warning Called from HAL interrupt context. Must not call any FreeRTOS blocking API.
typedef void ( *SPICallback )( bool success );

/// @brief Initialise the SPI manager, bind the hardware handle, and register HAL callbacks.
void SPIInitModule( void );

/// @brief Start an asynchronous interrupt-driven read from a device.
/// @param[in]  csPort  GPIO port for the chip-select line.
/// @param[in]  csPin   GPIO pin for the chip-select line.
/// @param[out] pData   Destination buffer; must remain valid until @p cb fires.
/// @param[in]  len     Number of bytes to receive.
/// @param[in]  cb      Completion callback invoked from HAL ISR context.
void SPIReadAsync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, SPICallback cb );

/// @brief Start an asynchronous interrupt-driven write to a device.
/// @param[in] csPort GPIO port for the chip-select line.
/// @param[in] csPin  GPIO pin for the chip-select line.
/// @param[in] pData  Source buffer; must remain valid until @p cb fires.
/// @param[in] len    Number of bytes to transmit.
/// @param[in] cb     Completion callback invoked from HAL ISR context.
void SPIWriteAsync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, SPICallback cb );

/// @brief Start an atomic asynchronous write-then-read without toggling CS between phases.
/// @param[in]  csPort   GPIO port for the chip-select line.
/// @param[in]  csPin    GPIO pin for the chip-select line.
/// @param[in]  pTxData  Transmit buffer; must remain valid until @p cb fires.
/// @param[in]  txLen    Number of bytes to transmit.
/// @param[out] pRxData  Receive buffer; must remain valid until @p cb fires.
/// @param[in]  rxLen    Number of bytes to receive.
/// @param[in]  cb       Completion callback invoked from HAL ISR context.
void SPITransceiveAsync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pTxData, uint16_t txLen, uint8_t* pRxData, uint16_t rxLen, SPICallback cb );

/// @brief Perform a synchronous blocking write.
/// @param[in] csPort   GPIO port for the chip-select line.
/// @param[in] csPin    GPIO pin for the chip-select line.
/// @param[in] pData    Source buffer.
/// @param[in] len      Number of bytes to transmit.
/// @param[in] timeout  Maximum wait in milliseconds.
/// @return true on success, false on timeout or bus error.
/// @warning Only call from task context, not ISR.
bool SPIWriteSync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, uint32_t timeout );

/// @brief Perform a synchronous blocking read.
/// @param[in]  csPort   GPIO port for the chip-select line.
/// @param[in]  csPin    GPIO pin for the chip-select line.
/// @param[out] pData    Destination buffer.
/// @param[in]  len      Number of bytes to receive.
/// @param[in]  timeout  Maximum wait in milliseconds.
/// @return true on success, false on timeout or bus error.
/// @warning Only call from task context, not ISR.
bool SPIReadSync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pData, uint16_t len, uint32_t timeout );

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
bool SPITransceiveSync( GPIO_TypeDef* csPort, uint16_t csPin, uint8_t* pTxData, uint16_t txLen, uint8_t* pRxData, uint16_t rxLen, uint32_t timeout );

#endif // SPIMANAGER_H
