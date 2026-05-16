#include "cmsis_os.h"
#include "i2c.h"
#include "main.h"
#include "usbpowerdelivery.h"

// I2C Peripheral Addresses
#define TCPP03_ADDR       ( 0x68 << 1 )
#define STPD01_ADDR       ( 0x28 << 1 )

// STPD01 Register Map
#define STPD01_VSEL       0x00
#define STPD01_EN         0x01
#define STPD01_STATUS_REG 0x02

// TCPP03 Register Map
#define TCPP03_CONF1      0x00
#define TCPP03_OVP_SET    0x01
#define TCPP03_VSENSE_H   0x08
#define TCPP03_ISENSE_H   0x0A

// Telemetry Scaling (12-bit ADC)
#define VBUS_SCALE_FACTOR 5 // ~5mV per LSB
#define IBUS_SCALE_FACTOR 2 // ~2mA per LSB (10mOhm shunt)

// Internal 15W Source Profiles (Budgeted from RAC20 20W rail)
static const USBPDPowerProfile local_source_profiles[] = {
    { 5000, 3000, 15000 }, { 9000, 1670, 15000 }, { 12000, 1250, 15000 }, { 20000, 750, 15000 }
};

static USBPDPowerProfile partner_source_profiles[ 7 ];
static uint8_t           partner_profile_count = 0;

static struct {
    USBPDStatus       status;
    USBPDPowerRole    role;
    USBPDPowerProfile active_profile;
    osMutexId_t       i2c_mtx;
} pd_handle;

// Initialise UCPD, STPD01 buck, and TCPP03 protection.
void USBPDInitModule( void ) {
    const osMutexAttr_t mtx_attr = { "pd_i2c_mtx", osMutexRecursive, NULL, 0 };
    pd_handle.i2c_mtx = osMutexNew( &mtx_attr );

    LL_UCPD_Enable( UCPD1 );

    // Keep buck disabled until role is confirmed
    HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );

    if ( osMutexAcquire( pd_handle.i2c_mtx, osWaitForever ) == osOK ) {
        uint8_t cfg = 0x01; // Enable TCPP03
        HAL_I2C_Mem_Write( &hi2c1, TCPP03_ADDR, TCPP03_CONF1, I2C_MEMADD_SIZE_8BIT, &cfg, 1, 100 );

        uint8_t ovp = 0x24; // Default 5V OVP for safe bench boot
        HAL_I2C_Mem_Write( &hi2c1, TCPP03_ADDR, TCPP03_OVP_SET, I2C_MEMADD_SIZE_8BIT, &ovp, 1, 100 );

        osMutexRelease( pd_handle.i2c_mtx );
    }

    pd_handle.status = USBPDStatusDisconnected;
    pd_handle.role = USBPDRoleNone;
}

void USBPDHandleFLGNInterrupt( uint16_t GPIO_Pin ) {
 
    UNUSED( GPIO_Pin );

    // TCPP03 FLGN: VBUS Over-voltage or Over-current
    HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );
    pd_handle.status = USBPDStatusError;
}

void USBPDHandleSourceInterrupt( uint16_t GPIO_Pin ) {

    UNUSED( GPIO_Pin );

    // STPD01 INT: Internal Buck Status Fault
    uint8_t status_reg = 0;
    // Perform an immediate I2C autopsy on the buck
    if ( HAL_I2C_Mem_Read( &hi2c1, STPD01_ADDR, STPD01_STATUS_REG, 1, &status_reg, 1, 10 ) == HAL_OK ) {
        // Bit 0: Thermal Shutdown, Bit 1: Over-current
        if ( status_reg & 0x03 ) {
            HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );
            pd_handle.status = USBPDStatusError;
        }
    }
}

// Background task to handle role management and CC line state.
void USBPDProcess( void ) {
    // MAINS_PWR_N: High = Bench (Sink), Low = Mains (Source)
    GPIO_PinState mains_state = HAL_GPIO_ReadPin( MAINS_PWR_N_GPIO_Port, MAINS_PWR_N_Pin );

    if ( mains_state == GPIO_PIN_SET ) {
        // Bench Mode: Powered by host. High-power rails isolated via hardware interlock.
        if ( pd_handle.role != USBPDRoleSink ) {
            pd_handle.role = USBPDRoleSink;
            
            // Present Rd resistors to Host
            LL_UCPD_SetSNKRole( UCPD1 );
            
            // Explicitly disable the Source Buck
            HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_RESET );
            
            pd_handle.status = USBPDStatusContractReady;
        }
    } else {
        // Mains Mode: Powered by internal supply. Acting as Source for guest devices.
        if ( pd_handle.role != USBPDRoleSource ) {
            pd_handle.role = USBPDRoleSource;
            
            // Present Rp resistors to Guest
            LL_UCPD_SetSRCRole( UCPD1 );
            
            // Enable STPD01 Buck
            HAL_GPIO_WritePin( PD_SRC_PON_GPIO_Port, PD_SRC_PON_Pin, GPIO_PIN_SET );
            
            pd_handle.status = USBPDStatusConnecting;
        }
    }
}

// Returns the current status of the connection.
USBPDStatus USBPDGetStatus( void ) {
    return pd_handle.status;
}

// Returns whether we are currently a Sink or a Source.
USBPDPowerRole USBPDGetPowerRole( void ) {
    return pd_handle.role;
}

// Returns the number of power profiles available.
uint8_t USBPDGetProfileCount( void ) {
    return ( pd_handle.role == USBPDRoleSource ) ? 4 : partner_profile_count;
}

// Retrieves a specific profile by index.
USBPDPowerProfile USBPDGetProfile( uint8_t index ) {
    if ( pd_handle.role == USBPDRoleSource && index < 4 ) {
        return local_source_profiles[ index ];
    }
    if ( pd_handle.role == USBPDRoleSink && index < partner_profile_count ) {
        return partner_source_profiles[ index ];
    }
    return (USBPDPowerProfile){ 0, 0, 0 };
}

// Request a voltage (as Sink) or set an output limit (as Source).
void USBPDRequestVoltage( Voltage target ) {
    if ( pd_handle.role == USBPDRoleSource ) {
        if ( osMutexAcquire( pd_handle.i2c_mtx, 100 ) == osOK ) {
            // Adjust STPD01 Output
            uint8_t vsel = (uint8_t)( ( target - 3000 ) / 20 );
            HAL_I2C_Mem_Write( &hi2c1, STPD01_ADDR, STPD01_VSEL, I2C_MEMADD_SIZE_8BIT, &vsel, 1, 50 );

            // Sync TCPP03 OVP (120% of target)
            uint8_t ovp_val = (uint8_t)( ( target * 12 ) / 10000 );
            HAL_I2C_Mem_Write( &hi2c1, TCPP03_ADDR, TCPP03_OVP_SET, I2C_MEMADD_SIZE_8BIT, &ovp_val, 1, 50 );

            pd_handle.active_profile.voltage = target;
            osMutexRelease( pd_handle.i2c_mtx );
        }
    } else if ( pd_handle.role == USBPDRoleSink ) {
        // Placeholder for Sink-side amperage negotiation if required by stack
        for ( uint8_t i = 0; i < partner_profile_count; i++ ) {
            if ( partner_source_profiles[ i ].voltage == 5000 ) {
                pd_handle.status = USBPDStatusNegotiating;
                break;
            }
        }
    }
}

// Get the actual live voltage on VBUS in mV.
Voltage USBPDGetLiveVoltage( void ) {
    uint8_t  buf[ 2 ];
    uint16_t raw = 0;
    if ( osMutexAcquire( pd_handle.i2c_mtx, 20 ) == osOK ) {
        if ( HAL_I2C_Mem_Read( &hi2c1, TCPP03_ADDR, TCPP03_VSENSE_H, I2C_MEMADD_SIZE_8BIT, buf, 2, 50 ) == HAL_OK ) {
            raw = (uint16_t)( ( buf[ 0 ] << 4 ) | ( buf[ 1 ] >> 4 ) );
        }
        osMutexRelease( pd_handle.i2c_mtx );
    }
    return (Voltage)( raw * VBUS_SCALE_FACTOR );
}

// Get the actual live current on VBUS in mA.
Current USBPDGetLiveCurrent( void ) {
    uint8_t  buf[ 2 ];
    uint16_t raw = 0;
    if ( osMutexAcquire( pd_handle.i2c_mtx, 20 ) == osOK ) {
        if ( HAL_I2C_Mem_Read( &hi2c1, TCPP03_ADDR, TCPP03_ISENSE_H, I2C_MEMADD_SIZE_8BIT, buf, 2, 50 ) == HAL_OK ) {
            raw = (uint16_t)( ( buf[ 0 ] << 4 ) | ( buf[ 1 ] >> 4 ) );
        }
        osMutexRelease( pd_handle.i2c_mtx );
    }
    return (Current)( raw * IBUS_SCALE_FACTOR );
}