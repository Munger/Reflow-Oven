#include <string.h>

#include "adc.h"
#include "main.h"
#include "SPIManager.h"
#include "taskutils.h"
#include "thermistor.h"
#include "thermocouple.h"

typedef enum {
    RegCr0 = 0x00,
    RegCr1 = 0x01,
    RegMask = 0x02,
    RegCjhf = 0x03,
    RegCjlf = 0x04,
    RegLthfMsb = 0x05,
    RegCjto = 0x09,
    RegCjth = 0x0A,
    RegCjtl = 0x0B,
    RegLtcb2 = 0x0C,
    RegLtcb1 = 0x0D,
    RegLtcb0 = 0x0E,
    RegSr = 0x0F
} MaxRegister;

typedef enum {
    Cr0ConvModeAuto = 0x80,
    Cr0OneShot = 0x40,
    Cr0OcFault100Ms = 0x10,
    Cr0CjDisable = 0x08, // Disable internal cold junction sensor
    Cr1TypeK = 0x03
} MaxConfig;

typedef struct Thermocouple {
    ThermocoupleID     id;
    osEventFlagsId_t   statusHandle;
    GPIO_TypeDef*      csPort;
    uint16_t           csPin;
    uint16_t           drdyPin;
    uint16_t           faultPin;
    ThermistorID       externalCjtId;
    ThermistorRef      externalCjt;
    Temperature        lastTemp;
    uint8_t            rxBuf[ 4 ]; // Buffer for async SPI reads
} Thermocouple, *ThermocouplePtr;

static Thermocouple instances[ 2 ];

static void TCWriteReg( ThermocoupleRef tc, MaxRegister reg, uint8_t val );
static void TCReadRegs( ThermocoupleRef tc, MaxRegister reg, uint8_t* buffer, uint8_t len );

// Initialises the thermocouple module, maps hardware pins, and configures the MAX31856 devices
void TCInitModule( void ) {
    memset( instances, 0, sizeof( instances ) );

    instances[ Thermocouple1 ].id = Thermocouple1;
    instances[ Thermocouple1 ].statusHandle = Thermocouple1StatusFlagsHandle;
    instances[ Thermocouple1 ].csPort = THERM1_CS_GPIO_Port;
    instances[ Thermocouple1 ].csPin = THERM1_CS_Pin;
    instances[ Thermocouple1 ].drdyPin = THERM1_DRDY_Pin;
    instances[ Thermocouple1 ].faultPin = THERM1_FAULT_Pin;
    instances[ Thermocouple1 ].externalCjtId = ThermistorCJT1;

    instances[ Thermocouple2 ].id = Thermocouple2;
    instances[ Thermocouple2 ].statusHandle = Thermocouple2StatusFlagsHandle;
    instances[ Thermocouple2 ].csPort = THERM2_CS_GPIO_Port;
    instances[ Thermocouple2 ].csPin = THERM2_CS_Pin;
    instances[ Thermocouple2 ].drdyPin = THERM2_DRDY_Pin;
    instances[ Thermocouple2 ].faultPin = THERM2_FAULT_Pin;
    instances[ Thermocouple2 ].externalCjtId = ThermistorCJT2;

    for ( uint8_t i = 0; i < 2; i++ ) {
        TCWriteReg( &instances[ i ], RegCr1, Cr1TypeK );
        TCWriteReg( &instances[ i ], RegCr0, (uint8_t)Cr0ConvModeAuto | (uint8_t)Cr0OcFault100Ms | (uint8_t)Cr0CjDisable );
        
        osEventFlagsSet( DeviceStatusFlagsHandle, ( instances[ i ].id == Thermocouple1 ) ? 
                         BIT( FlagThermocouple1Ready ) : BIT( FlagThermocouple2Ready ) );
    }
}

// Opens a handle to a specific thermocouple instance and ensures the associated cold-junction thermistor is ready
ThermocoupleRef TCOpen( ThermocoupleID thermocoupleID ) {
    ThermocoupleRef tc = &instances[ (uint8_t)thermocoupleID ];
    if ( tc->externalCjt == NULL ) {
        tc->externalCjt = TMOpen( tc->externalCjtId );
    }
    return tc;
}

// Injects current cold-junction temperature into the MAX31856 to override its internal sensor
void TCRequestSample( ThermocoupleRef tc ) {
    if ( tc == NULL || tc->externalCjt == NULL ) {
        return;
    }

    osEventFlagsClear( tc->statusHandle, 0x00FFFFFF );

    Temperature cjt = TMGetTemperature( tc->externalCjt );
    int16_t     cj_bits = (int16_t)( ( cjt * 64 ) / 1000 );

    uint8_t tx_h[ 2 ] = { (uint8_t)( (uint8_t)RegCjth | 0x80 ), (uint8_t)( ( cj_bits >> 8 ) & 0xFF ) };
    uint8_t tx_l[ 2 ] = { (uint8_t)( (uint8_t)RegCjtl | 0x80 ), (uint8_t)( cj_bits & 0xFF ) };

    // Sync writes for CJT injection
    if ( !SPIWriteSync( tc->csPort, tc->csPin, tx_h, 2, 5 ) ||
         !SPIWriteSync( tc->csPort, tc->csPin, tx_l, 2, 5 ) ) {
        osEventFlagsSet( tc->statusHandle, BIT( FlagTCStatusHardwareFault ) );
    }
}

// Non-blocking check to see if the hardware has asserted the DRDY flag
bool TCIsReady( ThermocoupleRef tc ) {
    if ( tc == NULL ) return false;
    return ( osEventFlagsGet( tc->statusHandle ) & BIT( FlagTCStatusDataReady ) );
}

// Blocking call that waits for a conversion to complete, then reads and decodes the 19-bit temperature data
Temperature TCGetTemperature( ThermocoupleRef tc ) {
    if ( tc == NULL ) return 0;

    uint32_t flags = osEventFlagsWait( tc->statusHandle, 
                                     BIT( FlagTCStatusDataReady ) | BIT( FlagTCStatusHardwareFault ), 
                                     osFlagsWaitAny, osWaitForever );

    if ( flags & BIT( FlagTCStatusHardwareFault ) ) {
        return tc->lastTemp;
    }

    uint8_t regs[ 3 ];
    TCReadRegs( tc, RegLtcb2, regs, 3 );

    int32_t val = ( (int32_t)regs[ 0 ] << 16 ) | ( (int32_t)regs[ 1 ] << 8 ) | regs[ 2 ];
    if ( val & 0x800000 )
        val |= 0xFF000000;

    tc->lastTemp = ( val >> 5 ) * 1000 / 128;
    osEventFlagsClear( tc->statusHandle, BIT( FlagTCStatusDataReady ) );
    
    return tc->lastTemp;
}

// Returns the last known cold-junction temperature for this thermocouple channel
Temperature TCGetCJT( ThermocoupleRef tc ) {
    return ( tc != NULL && tc->externalCjt != NULL ) ? TMGetTemperature( tc->externalCjt ) : 0;
}

// Returns the full status flag bitmask, including hardware fault details from the MAX31856 SR register
uint32_t TCGetStatus( ThermocoupleRef tc ) {
    if ( tc == NULL ) return BIT( FlagTCStatusHardwareFault );
    return osEventFlagsGet( tc->statusHandle ) & OS_USER_FLAGS_MASK;
}

// Hook for periodic background processing
void TCProcess( void ) {
    // Periodic processing or status monitoring
}

// ISR handler for the DRDY pin; notifies the waiting RTOS task that a conversion is complete
void TCHandleDRDYInterrupt( uint16_t GPIO_Pin ) {
    for ( uint8_t i = 0; i < 2; i++ ) {
        if ( GPIO_Pin == instances[ i ].drdyPin ) {
            osEventFlagsSet( instances[ i ].statusHandle, BIT( FlagTCStatusDataReady ) );
        }
    }
}

// ISR handler for the FAULT pin; reads the Status Register and propagates faults to the system
void TCHandleFaultInterrupt( uint16_t GPIO_Pin ) {
    for ( uint8_t i = 0; i < 2; i++ ) {
        struct Thermocouple* tc = &instances[ i ];
        if ( GPIO_Pin == tc->faultPin ) {
            uint8_t sr;
            TCReadRegs( tc, RegSr, &sr, 1 );
            // Shifting SR bits to align with TCStatusFlagsBit enum starting at index 2
            osEventFlagsSet( tc->statusHandle, ( (uint32_t)sr << 2 ) | BIT( FlagTCStatusHardwareFault ) );
            osEventFlagsSet( FaultFlagsHandle, ( tc->id == Thermocouple1 ) ? 
                             BIT( FlagThermocouple1Fault ) : BIT( FlagThermocouple2Fault ) );
        }
    }
}

// --- Internal Helpers ---

// Performs a synchronous 2-byte write to a MAX31856 register using SPIManager
static void TCWriteReg( ThermocoupleRef tc, MaxRegister reg, uint8_t val ) {
    uint8_t tx[ 2 ] = { (uint8_t)( (uint8_t)reg | 0x80 ), val };
    if ( !SPIWriteSync( tc->csPort, tc->csPin, tx, 2, 10 ) ) {
        osEventFlagsSet( tc->statusHandle, BIT( FlagTCStatusHardwareFault ) );
    }
}

// Performs a synchronous burst read of MAX31856 registers using the SPIManager abstraction
static void TCReadRegs( ThermocoupleRef tc, MaxRegister reg, uint8_t* buffer, uint8_t len ) {
    uint8_t addr = (uint8_t)( (uint8_t)reg & 0x7F );
    
    // Now returns bool; if false, ensure instance status reflects the failure
    if ( !SPITransceiveSync( tc->csPort, tc->csPin, &addr, 1, buffer, len, 10 ) ) {
        osEventFlagsSet( tc->statusHandle, BIT( FlagTCStatusHardwareFault ) );
    }
}