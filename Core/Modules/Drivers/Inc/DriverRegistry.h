/// @file DriverRegistry.h
///
/// @brief Central driver lifecycle registry — single `const` table for all modules.
///
/// Every driver exposes `void XxxInitModule(void)` and optionally `void XxxProcess(void)`.
/// This table collects all of them in one place with per-entry feature guards, so
/// `ManagerTaskInit()` and `DeviceTaskLoop()` become simple loops instead of
/// `#if`-block call sites duplicated across files.
///
/// Task-owner filtering (`TaskOwnerDevice`, `TaskOwnerUSBPD`) lets each task loop call
/// only its own drivers' `process` functions while `ManagerTaskInit` ignores the
/// field and calls every entry's `init` in table order (bus managers first).
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef DRIVER_REGISTRY_H
#define DRIVER_REGISTRY_H

#include <stdint.h>
#include <stddef.h>

/// @brief Identifies which task loop owns a driver's Process() call.
typedef enum {
    TaskOwnerManager,   ///< Process() called from ManagerTaskLoop
    TaskOwnerDevice,    ///< Process() called from DeviceTaskLoop
    TaskOwnerUSBPD,     ///< Process() called from USBPDTaskLoop
    TaskOwnerReflow,    ///< Process() called from ReflowTaskLoop
    TaskOwnerAPI,       ///< Process() called from APITaskLoop
    TaskOwnerLogging,   ///< Process() called from LoggingTaskLoop
} TaskOwner;

/// @brief Sentinel for DriverFindInstance "not found".
#define DRIVER_INSTANCE_NONE  UINT16_MAX

/// @brief A single named hardware instance within a driver module.
typedef struct {
    const char* name;   ///< User-facing instance name (e.g. "top", "cjt1").
    uint16_t    id;     ///< Driver-specific enum ID (e.g. TriacHeaterTop).
} InstanceEntry;

/// @brief A single driver entry — name, task owner, init, process, and instance table.
typedef struct DriverEntry {
    const char*          name;          ///< Short label for diagnostics.
    TaskOwner            task;          ///< Task that calls Process(). Ignored by ManagerTaskInit.
    void                 (*init)(void);    ///< Alloc-only InitModule (NULL if none).
    void                 (*process)(void); ///< Periodic Process() call (NULL if none).
    const InstanceEntry* instances;        ///< Named instance table (NULL if none).
    uint8_t              instanceCount;    ///< Number of entries in instances table.
} DriverEntry;

/// @brief Const pointer into the static driver table (table is read-only).
typedef const DriverEntry* DriverEntryPtr;

/// @brief Return the driver registry table.
/// @param[out] count Set to the number of entries in the table.
/// @return Pointer to the static const DriverEntry array.
DriverEntryPtr DriverTable( int* count );

/// @brief Find a named instance within a specific driver module.
/// @param driverName   Module name (matches DriverEntry.name).
/// @param instanceName User-facing instance name to look up.
/// @return The instance ID, or DRIVER_INSTANCE_NONE if not found.
uint16_t DriverFindInstance( const char* driverName, const char* instanceName );

#endif // DRIVER_REGISTRY_H
