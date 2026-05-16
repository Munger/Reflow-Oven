/// @file Thermocouple.c
///
/// @brief MAX31856 dual thermocouple driver — SPI polling implementation.
///
/// Manages two MAX31856 channels. Each instance has its own private osEventFlagsId_t
/// stored in the Thermocouple struct (tc->statusHandle). TCInitModule() creates these
/// via osEventFlagsNew() — they are not exposed as extern handles.
///
/// Key design invariants:
///   - All SPI access (register write/read) occurs exclusively in TCProcess().
///   - TCHandleDRDYInterrupt() only sets FlagTCStatusDataReady via osEventFlagsSet().
///   - TCHandleFaultInterrupt() only sets a volatile bool flag; the fault register
///     is read by TCProcess() to avoid SPI from ISR context.
///   - Getters return cached values from the Thermocouple struct — safe from any task.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "adc.h"
#include "main.h"
#include "SPIManager.h"
#include "TaskUtils.h"
#include "Thermistor.h"
#include "Thermocouple.h"

/// @brief MAX31856 register address map.
typedef enum {
    RegCr0     = 0x00,  ///< Configuration Register 0 (conversion mode, fault mask)
    RegCr1     = 0x01,  ///< Configuration Register 1 (thermocouple type)
    RegMask    = 0x02,  ///< Fault mask register
    RegCjhf    = 0x03,  ///< Cold junction high fault threshold
    RegCjlf    = 0x04,  ///< Cold junction low fault threshold
    RegLthfMsb = 0x05,  ///< Linearised temperature high fault threshold MSB
    RegCjto    = 0x09,  ///< Cold junction temperature offset
    RegCjth    = 0x0A,  ///< Cold junction temperature register (MSB)
    RegCjtl    = 0x0B,  ///< Cold junction temperature register (LSB)
    RegLtcb2   = 0x0C,  ///< Linearised thermocouple temperature byte 2 (MSB)
    RegLtcb1   = 0x0D,  ///< Linearised thermocouple temperature byte 1
    RegLtcb0   = 0x0E,  ///< Linearised thermocouple temperature byte 0 (LSB)
    RegSr      = 0x0F   ///< Status register (fault bits)
} MaxRegister;

/// @brief CR0 and CR1 configuration bit values used during initialisation.
typedef enum {
    Cr0ConvModeAuto = 0x80,  ///< Enable automatic conversion mode
    Cr0OneShot      = 0x40,  ///< One-shot conversion trigger (not used in auto mode)
    Cr0OcFault100Ms = 0x10,  ///< Enable open-circuit fault detection (100 ms filter)
    Cr0CjDisable    = 0x08,  ///< Disable internal cold-junction sensor (we inject externally)
    Cr1TypeK        = 0x03   ///< Configure for Type-K thermocouple
} MaxConfig;

/// @brief Internal per-channel thermocouple state.
typedef struct Thermocouple {
    ThermocoupleID     id;            ///< Channel identifier
    SPIRef             spi;           ///< SPI bus handle acquired at TCOpen()
    osEventFlagsId_t   statusHandle;  ///< Private event flag group for this instance
    GPIO_TypeDef*      csPort;        ///< SPI chip-select GPIO port
    uint16_t           csPin;         ///< SPI chip-select GPIO pin
    uint16_t           drdyPin;       ///< Data-ready GPIO pin (matched in DRDY ISR)
    uint16_t           faultPin;      ///< Fault GPIO pin (matched in FAULT ISR)
    ThermistorID       externalCjtId; ///< NTC thermistor ID used for CJT injection
    ThermistorRef      externalCjt;   ///< Resolved handle to the CJT thermistor
    Temperature        lastTemp;      ///< Most recently decoded thermocouple temperature
    uint8_t            rxBuf[ 4 ];    ///< Scratch buffer for SPI register reads
} Thermocouple, *ThermocouplePtr;

/// @brief Two thermocouple instances, indexed by ThermocoupleID.
static Thermocouple instances[ 2 ];

static void TCWriteReg( ThermocoupleRef tc, MaxRegister reg, uint8_t val );
static void TCReadRegs( ThermocoupleRef tc, MaxRegister reg, uint8_t* buffer, uint8_t len );

/// @brief Allocate per-instance resources and assign static GPIO/pin mappings.
///
/// Creates per-instance private event flag groups (osEventFlagsNew) and records
/// the GPIO/pin configuration from CubeMX-generated defines. Does NOT access SPI
/// hardware — that happens in TCOpen() once a bus reference is available. Safe to
/// call before SPIInitModule() runs.
void TCInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );

    instances[ Thermocouple1 ].id            = Thermocouple1;
    instances[ Thermocouple1 ].statusHandle  = osEventFlagsNew( NULL );
    instances[ Thermocouple1 ].csPort        = THERM1_CS_GPIO_Port;
    instances[ Thermocouple1 ].csPin         = THERM1_CS_Pin;
    instances[ Thermocouple1 ].drdyPin       = THERM1_DRDY_Pin;
    instances[ Thermocouple1 ].faultPin      = THERM1_FAULT_Pin;
    instances[ Thermocouple1 ].externalCjtId = ThermistorCJT1;

    instances[ Thermocouple2 ].id            = Thermocouple2;
    instances[ Thermocouple2 ].statusHandle  = osEventFlagsNew( NULL );
    instances[ Thermocouple2 ].csPort        = THERM2_CS_GPIO_Port;
    instances[ Thermocouple2 ].csPin         = THERM2_CS_Pin;
    instances[ Thermocouple2 ].drdyPin       = THERM2_DRDY_Pin;
    instances[ Thermocouple2 ].faultPin      = THERM2_FAULT_Pin;
    instances[ Thermocouple2 ].externalCjtId = ThermistorCJT2;
}

/// @brief Open a handle to a thermocouple instance and configure the MAX31856 hardware.
///
/// On first call for a given ID: stores @p spi, writes CR0/CR1 to the MAX31856,
/// resolves the CJT thermistor reference, and signals DeviceStatusFlagsHandle.
/// Subsequent calls with the same ID return the existing instance without re-configuring.
///
/// @param[in] thermocoupleID Channel identifier.
/// @param[in] spi            SPI bus handle returned by SPIOpen().
/// @return Handle to the thermocouple instance.
ThermocoupleRef TCOpen( ThermocoupleID thermocoupleID, SPIRef spi ) {
    ThermocoupleRef tc = &instances[ (uint8_t)thermocoupleID ];

    if ( tc->spi == NULL ) {
        tc->spi = spi;
        TCWriteReg( tc, RegCr1, Cr1TypeK );
        TCWriteReg( tc, RegCr0, (uint8_t)Cr0ConvModeAuto | (uint8_t)Cr0OcFault100Ms | (uint8_t)Cr0CjDisable );
        osEventFlagsSet( DeviceStatusFlagsHandle, ( tc->id == Thermocouple1 ) ?
                         BIT( FlagThermocouple1Ready ) : BIT( FlagThermocouple2Ready ) );
    }

    if ( tc->externalCjt == NULL ) {
        tc->externalCjt = TMOpen( tc->externalCjtId );
    }

    return tc;
}

/// @brief Signal that a new sample cycle should begin; hardware I/O happens in TCProcess().
/// @param[in] tc Handle returned by TCOpen().
void TCRequestSample( ThermocoupleRef tc ) {
    if ( tc == NULL || tc->externalCjt == NULL ) return;
    osEventFlagsSet( tc->statusHandle, BIT( FlagTCStatusSamplePending ) );
}

/// @brief Return true if a new sample has been decoded since the last check.
/// @param[in] tc Handle returned by TCOpen().
/// @return true if FlagTCStatusDataReady is set in the instance event flags.
/// @note Safe to call from any task context without blocking.
bool TCIsReady( ThermocoupleRef tc ) {
    if ( tc == NULL ) return false;
    return ( osEventFlagsGet( tc->statusHandle ) & BIT( FlagTCStatusDataReady ) );
}

/// @brief Return the most recently decoded thermocouple temperature.
/// @param[in] tc Handle returned by TCOpen().
/// @return Cached temperature in milli-degrees Celsius; 0 if @p tc is NULL.
/// @note Safe to call from any task context without blocking.
Temperature TCGetTemperature( ThermocoupleRef tc ) {
    return tc ? tc->lastTemp : 0;
}

/// @brief Return the cold-junction temperature for this instance.
/// @param[in] tc Handle returned by TCOpen().
/// @return CJT in milli-degrees Celsius; 0 if @p tc or its CJT ref is NULL.
/// @note Delegates to TMGetTemperature() — safe from any task context.
Temperature TCGetCJT( ThermocoupleRef tc ) {
    return ( tc != NULL && tc->externalCjt != NULL ) ? TMGetTemperature( tc->externalCjt ) : 0;
}

/// @brief Return the full status bitmask for this thermocouple instance.
/// @param[in] tc Handle returned by TCOpen().
/// @return Bitmask of ThermocoupleStatusBit flags, masked to OS_USER_FLAGS_MASK.
///         Returns FlagTCStatusHardwareFault if @p tc is NULL.
/// @note Safe to call from any task context without blocking.
uint32_t TCGetStatus( ThermocoupleRef tc ) {
    if ( tc == NULL ) return BIT( FlagTCStatusHardwareFault );
    return osEventFlagsGet( tc->statusHandle ) & OS_USER_FLAGS_MASK;
}

/// @brief Service both thermocouple instances: inject CJT, decode temperature, read fault register.
///
/// For each instance, in order:
///   1. If pendingSample: writes the current NTC CJT temperature to the MAX31856 CJT registers.
///   2. If FlagTCStatusDataReady: reads the three linearised temperature bytes and decodes them.
///   3. If faultPending: reads the fault status register and maps bits to ThermocoupleStatusBit.
///   4. Propagates FlagTCStatusHardwareFault to the global FaultFlagsHandle.
///
/// @warning All SPI hardware access occurs here. Do not call from ISR context.
void TCProcess( void ) {
    for ( uint8_t i = 0; i < 2; i++ ) {
        ThermocouplePtr tc = &instances[ i ];
        uint32_t faultBit  = ( tc->id == Thermocouple1 ) ? BIT( FlagThermocouple1Fault ) : BIT( FlagThermocouple2Fault );

        // Inject cold-junction temperature if a sample was requested.
        if ( osEventFlagsGet( tc->statusHandle ) & BIT( FlagTCStatusSamplePending ) ) {
            osEventFlagsClear( tc->statusHandle, OS_USER_FLAGS_MASK );

            Temperature cjt     = TMGetTemperature( tc->externalCjt );
            // MAX31856 CJT register format: signed 14-bit in units of 1/64 degree C
            int16_t     cj_bits = (int16_t)( ( cjt * 64 ) / 1000 );
            uint8_t     tx_h[ 2 ] = { (uint8_t)( (uint8_t)RegCjth | 0x80 ), (uint8_t)( ( cj_bits >> 8 ) & 0xFF ) };
            uint8_t     tx_l[ 2 ] = { (uint8_t)( (uint8_t)RegCjtl | 0x80 ), (uint8_t)( cj_bits & 0xFF ) };

            if ( !SPIWriteSync( tc->spi, tc->csPort, tc->csPin, tx_h, 2, 5 ) ||
                 !SPIWriteSync( tc->spi, tc->csPort, tc->csPin, tx_l, 2, 5 ) ) {
                osEventFlagsSet( tc->statusHandle, BIT( FlagTCStatusHardwareFault ) );
            }
        }

        // Decode temperature register if DRDY was asserted by ISR.
        if ( osEventFlagsGet( tc->statusHandle ) & BIT( FlagTCStatusDataReady ) ) {
            uint8_t regs[ 3 ];
            TCReadRegs( tc, RegLtcb2, regs, 3 );

            // 19-bit two's complement in bits 23:5; LSB = 1/128 degree C; result in millidegrees
            int32_t val = ( (int32_t)regs[ 0 ] << 16 ) | ( (int32_t)regs[ 1 ] << 8 ) | regs[ 2 ];
            if ( val & 0x800000 ) val |= 0xFF000000;
            tc->lastTemp = ( val >> 5 ) * 1000 / 128;

            osEventFlagsClear( tc->statusHandle, BIT( FlagTCStatusDataReady ) );
        }

        // Read fault status register if FAULT pin was asserted by ISR.
        // SR is read here (not in the ISR) to avoid SPI transactions from interrupt context.
        if ( osEventFlagsGet( tc->statusHandle ) & BIT( FlagTCStatusFaultPending ) ) {
            osEventFlagsClear( tc->statusHandle, BIT( FlagTCStatusFaultPending ) );
            uint8_t sr;
            TCReadRegs( tc, RegSr, &sr, 1 );
            // SR bits 7:2 map to ThermocoupleStatusBit bits 9:4 (offset by 2)
            osEventFlagsSet( tc->statusHandle, ( (uint32_t)sr << 2 ) | BIT( FlagTCStatusHardwareFault ) );
        }

        // Propagate hardware fault to global flags.
        if ( osEventFlagsGet( tc->statusHandle ) & BIT( FlagTCStatusHardwareFault ) ) {
            osEventFlagsSet( FaultFlagsHandle, faultBit );
        } else {
            osEventFlagsClear( FaultFlagsHandle, faultBit );
        }
    }
}

/// @brief DRDY rising-edge ISR handler — sets FlagTCStatusDataReady on the matching instance.
///
/// @param[in] GPIO_Pin HAL pin mask; compared against each instance's drdyPin.
/// @warning ISR context. Sets event flags only — no SPI access, no FreeRTOS blocking API.
void TCHandleDRDYInterrupt( uint16_t GPIO_Pin ) {
    for ( uint8_t i = 0; i < 2; i++ ) {
        if ( GPIO_Pin == instances[ i ].drdyPin ) {
            osEventFlagsSet( instances[ i ].statusHandle, BIT( FlagTCStatusDataReady ) );
        }
    }
}

/// @brief FAULT rising-edge ISR handler — sets FlagTCStatusFaultPending on the matching instance.
///
/// Does not read the SPI fault register (to avoid SPI from ISR context). TCProcess()
/// reads the status register on the next tick.
///
/// @param[in] GPIO_Pin HAL pin mask; compared against each instance's faultPin.
/// @warning ISR context. osEventFlagsSet() only — no SPI access.
void TCHandleFaultInterrupt( uint16_t GPIO_Pin ) {
    for ( uint8_t i = 0; i < 2; i++ ) {
        if ( GPIO_Pin == instances[ i ].faultPin ) {
            osEventFlagsSet( instances[ i ].statusHandle, BIT( FlagTCStatusFaultPending ) );
        }
    }
}

/// @brief Write a single byte to a MAX31856 register.
///
/// Sets bit 7 of the address to indicate a write operation (MAX31856 SPI convention).
/// On SPI failure, sets FlagTCStatusHardwareFault in the instance status flags.
///
/// @param[in] tc  Thermocouple instance.
/// @param[in] reg Target register address.
/// @param[in] val Byte to write.
static void TCWriteReg( ThermocoupleRef tc, MaxRegister reg, uint8_t val ) {
    uint8_t tx[ 2 ] = { (uint8_t)( (uint8_t)reg | 0x80 ), val };
    if ( !SPIWriteSync( tc->spi, tc->csPort, tc->csPin, tx, 2, 10 ) ) {
        osEventFlagsSet( tc->statusHandle, BIT( FlagTCStatusHardwareFault ) );
    }
}

/// @brief Read one or more consecutive registers from a MAX31856.
///
/// Clears bit 7 of the address to indicate a read operation. Uses SPITransceiveSync
/// to send the address byte and clock in @p len reply bytes simultaneously.
/// On SPI failure, sets FlagTCStatusHardwareFault in the instance status flags.
///
/// @param[in]  tc     Thermocouple instance.
/// @param[in]  reg    Starting register address.
/// @param[out] buffer Destination buffer for the received bytes.
/// @param[in]  len    Number of bytes to read.
static void TCReadRegs( ThermocoupleRef tc, MaxRegister reg, uint8_t* buffer, uint8_t len ) {
    uint8_t addr = (uint8_t)( (uint8_t)reg & 0x7F );
    if ( !SPITransceiveSync( tc->spi, tc->csPort, tc->csPin, &addr, 1, buffer, len, 10 ) ) {
        osEventFlagsSet( tc->statusHandle, BIT( FlagTCStatusHardwareFault ) );
    }
}
