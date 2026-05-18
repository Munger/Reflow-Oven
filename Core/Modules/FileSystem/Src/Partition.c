/// @file Partition.c
///
/// @brief Partition table management implementation.
///
/// The on-disk layout in block 0 is:
///   [0..3]   magic word (PART_TABLE_MAGIC)
///   [4]      format version (kPartTableVersion)
///   [5]      entry count
///   [6..7]   reserved (zero)
///   [8..N]   FSPartEntry × count (32 bytes each)
///   [N+1..N+4] additive 32-bit checksum over all preceding bytes
///
/// The checksum is computed over the header and all entries. If the magic,
/// version, or checksum is invalid the default layout is written and used.
///
/// Default partition layout (64 MB NOR flash, 4 KB blocks, block 0 reserved):
///   "system"  blocks    1–1024  ( 4 MB)   LittleFS, uid 0, mode 0755
///   "user"    blocks 1025–16383 (~60 MB)  LittleFS, uid 0, mode 0755, USB-visible
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_FILE_SYSTEM

#include <string.h>
#include "Partition.h"

// ============================================================================
// On-disk record
// ============================================================================

typedef struct PartTableRecord {
    uint32_t   magic;
    uint8_t    version;
    uint8_t    count;
    uint16_t   reserved;
    FSPartEntry entries[ kPartMaxCount ];
    uint32_t   checksum;
} PartTableRecord, *PartTableRecordPtr;

// ============================================================================
// Private instance type
// ============================================================================

typedef struct FSPartition {
    FSPartEntry entry;
    BDRef       device;
} FSPartition, *FSPartitionPtr;

// ============================================================================
// Module state
// ============================================================================

static FSPartition table[ kPartMaxCount ];
static uint8_t     partCount = 0;
static BDRef       partDevice = NULL;

// ============================================================================
// Default partition layout
// ============================================================================

static const FSPartEntry kDefaultEntries[] = {
    {
        .name       = "system",
        .startBlock = 1,
        .blockCount = 1024,
        .type       = (uint8_t)FSPartLittleFS,
        .flags      = 0,
        .uid        = 0,
        .reserved   = 0,
        .mode       = FSModeDirDefault,
        .pad        = { 0, 0 },
    },
    {
        .name       = "user",
        .startBlock = 1025,
        .blockCount = 15359,
        .type       = (uint8_t)FSPartLittleFS,
        .flags      = (uint8_t)FSPartFlagUsbVisible,
        .uid        = 0,
        .reserved   = 0,
        .mode       = FSModeDirDefault,
        .pad        = { 0, 0 },
    },
};

enum { kDefaultEntryCount = sizeof( kDefaultEntries ) / sizeof( kDefaultEntries[ 0 ] ) };

// ============================================================================
// Checksum
// ============================================================================

static uint32_t ComputeChecksum( const PartTableRecordPtr rec ) {
    const uint8_t* p   = (const uint8_t*)rec;
    size_t         len = sizeof( PartTableRecord ) - sizeof( uint32_t );
    uint32_t       sum = 0;
    for ( size_t i = 0; i < len; i++ ) sum += p[ i ];
    return sum;
}

// ============================================================================
// Private helpers
// ============================================================================

static void LoadDefaults( void ) {
    partCount = (uint8_t)kDefaultEntryCount;
    for ( uint8_t i = 0; i < partCount; i++ ) {
        table[ i ].entry  = kDefaultEntries[ i ];
        table[ i ].device = partDevice;
    }
}

static FSResult WriteTable( void ) {
    PartTableRecord rec;
    memset( &rec, 0xFF, sizeof( rec ) );
    rec.magic   = PART_TABLE_MAGIC;
    rec.version = kPartTableVersion;
    rec.count   = partCount;
    rec.reserved = 0;
    for ( uint8_t i = 0; i < partCount; i++ ) {
        rec.entries[ i ] = table[ i ].entry;
    }
    rec.checksum = ComputeChecksum( &rec );

    FSResult r = BDErase( partDevice, 0 );
    if ( r != FSResultOk ) return r;

    FSGeometry geo = BDGetGeometry( partDevice );
    uint32_t   progSize = geo.progSize;

    const uint8_t* src  = (const uint8_t*)&rec;
    uint32_t       rem  = sizeof( rec );
    uint32_t       off  = 0;

    while ( rem > 0 ) {
        uint32_t chunk = ( rem < progSize ) ? rem : progSize;
        r = BDProg( partDevice, 0, off, src + off, chunk );
        if ( r != FSResultOk ) return r;
        off += chunk;
        rem -= chunk;
    }
    return FSResultOk;
}

// ============================================================================
// Public API
// ============================================================================

FSResult PartInitModule( BDRef bd ) {
    if ( bd == NULL ) return FSResultInvalid;
    partDevice = bd;
    partCount  = 0;

    PartTableRecord rec;
    FSResult r = BDRead( bd, 0, 0, &rec, sizeof( rec ) );
    if ( r != FSResultOk ) return r;

    bool valid = ( rec.magic   == PART_TABLE_MAGIC    ) &&
                 ( rec.version == kPartTableVersion    ) &&
                 ( rec.count   <= kPartMaxCount        ) &&
                 ( ComputeChecksum( &rec ) == rec.checksum );

    if ( valid ) {
        partCount = rec.count;
        for ( uint8_t i = 0; i < partCount; i++ ) {
            table[ i ].entry  = rec.entries[ i ];
            table[ i ].device = bd;
        }
    } else {
        LoadDefaults();
        r = WriteTable();
        if ( r != FSResultOk ) return r;
    }
    return FSResultOk;
}

uint8_t PartGetCount( void ) {
    return partCount;
}

PartRef PartGetByIndex( uint8_t index ) {
    return ( index < partCount ) ? &table[ index ] : NULL;
}

PartRef PartGetByName( const char* name ) {
    if ( name == NULL ) return NULL;
    for ( uint8_t i = 0; i < partCount; i++ ) {
        if ( strncmp( table[ i ].entry.name, name, kPartNameLen ) == 0 ) {
            return &table[ i ];
        }
    }
    return NULL;
}

FSResult PartGetEntry( PartRef part, FSPartEntryPtr out ) {
    if ( part == NULL || out == NULL ) return FSResultInvalid;
    *out = part->entry;
    return FSResultOk;
}

BDRef PartGetDevice( PartRef part ) {
    return part ? part->device : NULL;
}

FSResult PartCommit( void ) {
    if ( partDevice == NULL ) return FSResultNotReady;
    return WriteTable();
}

#endif // FEATURE_FILE_SYSTEM
