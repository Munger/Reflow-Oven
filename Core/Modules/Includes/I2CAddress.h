/// @file I2CAddress.h
///
/// @brief I2C device address registry.
///
/// All 7-bit I2C addresses used on this board in one place. Addresses are the
/// canonical 7-bit form as quoted in each device's datasheet — drivers shift
/// left by 1 before passing to I2CReadSync / I2CWriteSync per the HAL convention.
///
/// Confirmed against the board netlist:
///   STPD01  ADD  → GND  (address variant 0)
///   TCPP03  I2C_ADD → GND  (address variant 0)
///   MCP3221 factory-coded as A2 variant  → 0x4A
///   EMC2101 fixed address (no pin)       → 0x4C
///   AS5600  fixed address (no pin)       → 0x36
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef I2C_ADDRESS_H
#define I2C_ADDRESS_H

/// @brief 7-bit I2C addresses for every device on the shared I2C bus (PA9/PA10).
///
/// Pass as `(uint16_t)I2CAddrXxx << 1` to I2CReadSync / I2CWriteSync.
typedef enum {
    I2CAddrSTPD01  = 0x28, ///< STPD01PUR  — USB-PD power supply controller  (ADD=GND)
    I2CAddrAS5600  = 0x36, ///< AS5600      — magnetic encoder / fan tachometer (CN4, fixed)
    I2CAddrMCP3221 = 0x4A, ///< MCP3221A2T  — 12-bit I2C ADC for NTC thermistor (A2 variant)
    I2CAddrEMC2101 = 0x4C, ///< EMC2101      — DC fan controller / inlet-temp sensor (fixed)
    I2CAddrTCPP03  = 0x68, ///< TCPP03-M20  — USB-C port protection controller  (I2C_ADD=GND)
} I2CAddress;

#endif // I2C_ADDRESS_H
