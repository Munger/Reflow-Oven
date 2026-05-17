/// @file Flash.c
///
/// @brief External NOR flash driver — MX25L51245GZ2I-10G over SPI.
///
/// All operations use the synchronous SPIManager API (SPIWriteSync /
/// SPITransceiveSync) via the SPIRef stored at FlashOpen() time. The chip is
/// placed into 4-byte address mode at Open() and kept there for the session —
/// all command opcodes are the 4BA variants. WIP polling after each program or
/// erase yields the FreeRTOS scheduler between polls so that other tasks (and
/// other SPI users) are not starved.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include <string.h>

#include "Flash.h"
#include "SystemStatusFlags.h"
#include "main.h"
#include "cmsis_os2.h"

// ============================================================================
// MX25L51245G command opcodes
// ============================================================================

static const uint8_t kCmdReadId         = 0x9FU; ///< Read JEDEC identification (3 bytes).
static const uint8_t kCmdWren           = 0x06U; ///< Write Enable — must precede any program or erase.
static const uint8_t kCmdReadSr1        = 0x05U; ///< Read Status Register 1 (contains WIP and WEL).
static const uint8_t kCmdEnter4Ba       = 0xB7U; ///< Enter 4-Byte Address Mode.
static const uint8_t kCmdRead4Ba        = 0x13U; ///< Read Data with 4-byte address.
static const uint8_t kCmdPageProg4Ba    = 0x12U; ///< Page Program with 4-byte address (up to 256 bytes).
static const uint8_t kCmdSectorErase4Ba = 0x21U; ///< Sector Erase 4 KB with 4-byte address.
static const uint8_t kCmdChipErase      = 0xC7U; ///< Full chip erase (~200 s typical).

// ============================================================================
// Status Register 1 bit masks
// ============================================================================

static const uint8_t kSr1Wip = 0x01U; ///< Write In Progress — set while program/erase is running.
static const uint8_t kSr1Wel = 0x02U; ///< Write Enable Latch — set after a successful WREN.

// ============================================================================
// Expected JEDEC ID bytes for MX25L51245GZ2I-10G
// ============================================================================

static const uint8_t kJedecManufacturer = 0xC2U; ///< Macronix manufacturer ID.
static const uint8_t kJedecMemoryType   = 0x20U; ///< MX25L series memory type.
static const uint8_t kJedecCapacity     = 0x1AU; ///< 512 Mbit (64 MB) capacity code.

// ============================================================================
// Timing — worst-case datasheet values plus margin
// ============================================================================

static const uint32_t kTimeoutSpiMs         =     50U; ///< SPI transfer timeout.
static const uint32_t kTimeoutPageProgMs    =     20U; ///< Page program WIP poll timeout (datasheet max 5 ms).
static const uint32_t kTimeoutSectorEraseMs =    500U; ///< Sector erase WIP poll timeout (datasheet max 400 ms).
static const uint32_t kTimeoutChipEraseMs   = 500000U; ///< Chip erase WIP poll timeout (datasheet max ~400 s).

static const uint32_t kPollProgMs  =    2U; ///< WIP poll interval after page program.
static const uint32_t kPollEraseMs =   10U; ///< WIP poll interval after sector erase.
static const uint32_t kPollChipMs  = 1000U; ///< WIP poll interval during chip erase.

// ============================================================================
// Chip geometry — MX25L51245GZ2I-10G
// ============================================================================

static const uint32_t kPageSize   = 256U;
static const uint32_t kSectorSize = 4096U;
static const uint32_t kTotalSize  = 64UL * 1024UL * 1024UL;

// ============================================================================
// Command + address buffer — reused for page program (5 header + 256 data bytes)
// ============================================================================

enum { kProgBufSize = 5 + 256 };

// ============================================================================
// Private instance type
// ============================================================================

/// @brief Full internal state of one flash device instance.
typedef struct FlashInstance {
    FlashID          id;            ///< Device identifier
    SPIRef           spi;           ///< SPI bus handle acquired at FlashOpen()
    osEventFlagsId_t statusHandle;  ///< Per-instance diagnostic event flags
    uint8_t          progBuf[ kProgBufSize ]; ///< Scratch buffer for page program commands
} FlashInstance, *FlashInstancePtr;

/// @brief All flash instances — indexed by FlashID.
static FlashInstance instances[ FlashCount ];

// ============================================================================
// Private helpers
// ============================================================================

/// @brief Write a 4-byte big-endian address into @p buf[0..3].
static void PackAddr4( uint8_t* buf, uint32_t addr ) {
    buf[ 0 ] = (uint8_t)( addr >> 24 );
    buf[ 1 ] = (uint8_t)( addr >> 16 );
    buf[ 2 ] = (uint8_t)( addr >>  8 );
    buf[ 3 ] = (uint8_t)( addr        );
}

/// @brief Read Status Register 1 into @p sr1.
/// @return true on success, false on SPI error.
static bool ReadSR1( FlashInstancePtr flash, uint8_t* sr1 ) {
    uint8_t cmd = kCmdReadSr1;
    return SPITransceiveSync( flash->spi, FLASH_CS_GPIO_Port, FLASH_CS_Pin, &cmd, 1, sr1, 1, kTimeoutSpiMs );
}

/// @brief Poll WIP until clear or timeout expires.
///
/// Yields to the FreeRTOS scheduler between polls, allowing other SPI users
/// to access the bus while the flash completes its internal write cycle.
///
/// @return true if the device becomes idle within the timeout, false otherwise.
static bool WaitReady( FlashInstancePtr flash, uint32_t pollMs, uint32_t timeoutMs ) {
    uint32_t elapsed = 0;
    osEventFlagsSet( flash->statusHandle, BIT( FlagFlashStatusBusy ) );
    while ( elapsed < timeoutMs ) {
        uint8_t sr1;
        if ( !ReadSR1( flash, &sr1 ) ) {
            osEventFlagsClear( flash->statusHandle, BIT( FlagFlashStatusBusy ) );
            return false;
        }
        if ( !( sr1 & kSr1Wip ) ) {
            osEventFlagsClear( flash->statusHandle, BIT( FlagFlashStatusBusy ) );
            return true;
        }
        osDelay( pollMs );
        elapsed += pollMs;
    }
    osEventFlagsClear( flash->statusHandle, BIT( FlagFlashStatusBusy ) );
    osEventFlagsSet( flash->statusHandle, BIT( FlagFlashStatusTimeout ) );
    osEventFlagsSet( FaultFlagsHandle, BIT( FlagFlashFault ) );
    return false;
}

/// @brief Send a Write Enable command and verify the WEL bit is set.
/// @return true if WEL is confirmed set, false on SPI error or WEL not set.
static bool WriteEnable( FlashInstancePtr flash ) {
    uint8_t cmd = kCmdWren;
    if ( !SPIWriteSync( flash->spi, FLASH_CS_GPIO_Port, FLASH_CS_Pin, &cmd, 1, kTimeoutSpiMs ) ) return false;
    uint8_t sr1;
    if ( !ReadSR1( flash, &sr1 ) ) return false;
    return ( sr1 & kSr1Wel ) != 0;
}

/// @brief Read and verify the 3-byte JEDEC ID against the expected MX25L51245G values.
/// @return true if ID matches, false on SPI error or mismatch.
static bool VerifyJEDECID( FlashInstancePtr flash ) {
    uint8_t cmd = kCmdReadId;
    uint8_t id[ 3 ] = { 0 };
    if ( !SPITransceiveSync( flash->spi, FLASH_CS_GPIO_Port, FLASH_CS_Pin, &cmd, 1, id, 3, kTimeoutSpiMs ) ) return false;
    return ( id[ 0 ] == kJedecManufacturer &&
             id[ 1 ] == kJedecMemoryType   &&
             id[ 2 ] == kJedecCapacity );
}

/// @brief Issue the Enter 4-Byte Address Mode command (0xB7).
/// @return true on success, false on SPI error.
static bool Enter4ByteMode( FlashInstancePtr flash ) {
    uint8_t cmd = kCmdEnter4Ba;
    return SPIWriteSync( flash->spi, FLASH_CS_GPIO_Port, FLASH_CS_Pin, &cmd, 1, kTimeoutSpiMs );
}

// ============================================================================
// Public API
// ============================================================================

/// @brief Open a handle to a flash device and perform one-time hardware initialisation.
///
/// On first call for a given ID: verifies JEDEC ID, enters 4-byte address mode,
/// and signals DeviceStatusFlagsHandle. Subsequent calls with the same ID return
/// the existing instance without re-initialising.
FlashRef FlashOpen( FlashID id, SPIRef spi ) {
    if ( id >= FlashCount ) return NULL;
    FlashInstancePtr flash = &instances[ id ];

    if ( flash->statusHandle != NULL ) return flash;  // already initialised

    flash->id           = id;
    flash->spi          = spi;
    flash->statusHandle = osEventFlagsNew( NULL );

    if ( !VerifyJEDECID( flash ) ) {
        osEventFlagsSet( flash->statusHandle, BIT( FlagFlashStatusJEDECError ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagFlashFault ) );
        return flash;
    }

    if ( !Enter4ByteMode( flash ) ) {
        osEventFlagsSet( flash->statusHandle, BIT( FlagFlashStatusSPIError ) );
        osEventFlagsSet( FaultFlagsHandle, BIT( FlagFlashFault ) );
        return flash;
    }

    osEventFlagsSet( flash->statusHandle, BIT( FlagFlashStatusReady ) );
    osEventFlagsSet( DeviceStatusFlagsHandle, BIT( FlagFlashReady ) );
    return flash;
}

/// @brief Return a handle to a previously opened flash device without re-initialising.
/// @param[in] id Flash device identifier.
/// @return Handle, or NULL if @p id has not been opened yet.
FlashRef FlashGetRef( FlashID id ) {
    if ( id >= FlashCount ) return NULL;
    return ( instances[ id ].statusHandle != NULL ) ? &instances[ id ] : NULL;
}

/// @brief Read @p len bytes from byte address @p addr.
bool FlashRead( FlashRef flash, uint32_t addr, void* buf, uint32_t len ) {
    if ( flash == NULL ) return false;
    uint8_t cmd[ 5 ];
    cmd[ 0 ] = kCmdRead4Ba;
    PackAddr4( &cmd[ 1 ], addr );
    return SPITransceiveSync( flash->spi, FLASH_CS_GPIO_Port, FLASH_CS_Pin, cmd, 5, buf, (uint16_t)len, kTimeoutSpiMs );
}

/// @brief Program up to kPageSize bytes at page-aligned address @p addr.
///
/// Sends Write Enable, transmits the page program command with address and data
/// in a single CS-asserted burst, then polls WIP until the device is idle.
bool FlashProgram( FlashRef flash, uint32_t addr, const void* buf, uint32_t len ) {
    if ( flash == NULL ) return false;
    if ( len == 0 || len > kPageSize || ( addr % kPageSize ) != 0 ) return false;

    if ( !WriteEnable( flash ) ) return false;

    flash->progBuf[ 0 ] = kCmdPageProg4Ba;
    PackAddr4( &flash->progBuf[ 1 ], addr );
    memcpy( &flash->progBuf[ 5 ], buf, len );

    if ( !SPIWriteSync( flash->spi, FLASH_CS_GPIO_Port, FLASH_CS_Pin, flash->progBuf, (uint16_t)( 5U + (uint16_t)len ), kTimeoutSpiMs ) ) return false;

    return WaitReady( flash, kPollProgMs, kTimeoutPageProgMs );
}

/// @brief Erase the 4 KB sector containing @p addr (must be sector-aligned).
bool FlashEraseSector( FlashRef flash, uint32_t addr ) {
    if ( flash == NULL ) return false;
    if ( ( addr % kSectorSize ) != 0 ) return false;

    if ( !WriteEnable( flash ) ) return false;

    uint8_t cmd[ 5 ];
    cmd[ 0 ] = kCmdSectorErase4Ba;
    PackAddr4( &cmd[ 1 ], addr );

    if ( !SPIWriteSync( flash->spi, FLASH_CS_GPIO_Port, FLASH_CS_Pin, cmd, 5, kTimeoutSpiMs ) ) return false;

    return WaitReady( flash, kPollEraseMs, kTimeoutSectorEraseMs );
}

/// @brief Erase the entire chip.
///
/// @warning Blocks for up to ~400 seconds. Only call from a partition format operation.
bool FlashEraseChip( FlashRef flash ) {
    if ( flash == NULL ) return false;
    if ( !WriteEnable( flash ) ) return false;

    uint8_t cmd = kCmdChipErase;
    if ( !SPIWriteSync( flash->spi, FLASH_CS_GPIO_Port, FLASH_CS_Pin, &cmd, 1, kTimeoutSpiMs ) ) return false;

    return WaitReady( flash, kPollChipMs, kTimeoutChipEraseMs );
}

/// @brief Verify the device is not busy — used as the LittleFS sync callback.
bool FlashSync( FlashRef flash ) {
    if ( flash == NULL ) return false;
    uint8_t sr1;
    if ( !ReadSR1( flash, &sr1 ) ) {
        osEventFlagsSet( flash->statusHandle, BIT( FlagFlashStatusSPIError ) );
        return false;
    }
    return ( sr1 & kSr1Wip ) == 0;
}

/// @brief Return the page size in bytes for this flash device.
uint32_t FlashGetPageSize( FlashRef flash ) {
    return flash ? kPageSize : 0;
}

/// @brief Return the sector size in bytes for this flash device.
uint32_t FlashGetSectorSize( FlashRef flash ) {
    return flash ? kSectorSize : 0;
}

/// @brief Return the total capacity in bytes for this flash device.
uint32_t FlashGetTotalSize( FlashRef flash ) {
    return flash ? kTotalSize : 0;
}

/// @brief Return the number of erasable sectors for this flash device.
uint32_t FlashGetSectorCount( FlashRef flash ) {
    return flash ? ( kTotalSize / kSectorSize ) : 0;
}

/// @brief Return the raw flash status event flag bits for diagnostics and system snapshots.
/// @param[in] flash Handle returned by FlashOpen().
/// @return Bitmask of FlashStatusBit values; 0 if @p flash is NULL.
uint32_t FlashGetStatus( FlashRef flash ) {
    return ( flash != NULL ) ? osEventFlagsGet( flash->statusHandle ) : 0;
}
