/// @file APIRoutes.h
///
/// @brief API route table — request code enum, route struct, and route table definition.
///
/// Defines `APIRequestCode` (one entry per logical API endpoint), the `APIRoute`
/// struct (a pattern + code + handler triple), and the `apiRouteTable` array that
/// the stream codec uses for pattern-match dispatch.
///
/// The route table is defined only when `APICODEC_C` is defined (i.e. inside
/// `apicodec.c`) to avoid multiple-definition link errors. All other translation
/// units see only the type declarations.
///
/// Each route pattern is an `sscanf`-compatible prefix string. A `%` directive in
/// the pattern marks a typed argument (`%s`, `%d`, etc.). Handlers extract
/// arguments by calling `sscanf(reqString, route->pattern, ...)`. Patterns
/// without `%` treat all remaining bytes as payload body.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#ifndef APIROUTES_H
#define APIROUTES_H

#include <stdint.h>

#include "APITypes.h"
#include "APIHandlers.h"

/// @brief Logical request codes — one per unique API endpoint.
///
/// Used by the API task to identify a dispatched request independently of how it
/// arrived (REST path or CLI shorthand). The route table maps each route pattern
/// to exactly one code and one handler.
typedef enum {
    // Device list + individual device access
    APIReqDeviceList,      ///< get /devices
    APIReqDeviceGet,       ///< get /devices/%s/%s
    APIReqDeviceSet,       ///< put /devices/%s/%s
    // Reflow cycle
    APIReqReflowStatus,    ///< get /reflow/status
    APIReqReflowRun,       ///< put /reflow/run
    APIReqReflowStop,      ///< put /reflow/stop
    // Profiles
    APIReqProfileList,     ///< get /profiles
    APIReqProfileGet,      ///< get /profiles/%s
    APIReqProfileCreate,   ///< post /profiles/%s
    APIReqProfileUpdate,   ///< put /profiles/%s
    APIReqProfileDelete,   ///< delete /profiles/%s
    // Config
    APIReqConfigGet,       ///< get /config
    APIReqConfigPut,       ///< put /config
    // Logs
    APIReqLogsList,        ///< get /logs
    APIReqLogGet,          ///< get /logs/%s
    APIReqLogDelete,       ///< delete /logs/%s
    APIReqLogsClear,       ///< delete /logs
    // Storage info + format
    APIReqStorageGet,      ///< get /storage
    APIReqStorageFormat,   ///< put /storage/format
    // File ops
    APIReqFileList,        ///< get /storage/files[/%s]
    APIReqFileRead,        ///< get /storage/file/%s
    APIReqFileWrite,       ///< put /storage/file/%s
    APIReqFileDelete,      ///< delete /storage/file/%s
    // System
    APIReqSystemStatus,    ///< get /system/status
    APIReqSystemStats,     ///< get /system/stats
    APIReqSystemPower,     ///< get /system/power
    APIReqSystemStop,      ///< put /system/stop
    APIReqSystemDebug,     ///< put /system/debug
    APIReqClockGet,        ///< get /system/clock
    APIReqClockPut,        ///< put /system/clock
    APIReqSystemReset      ///< put /system/reset
} APIRequestCode;

/// @brief A single entry in the API route table.
///
/// The stream parser walks `apiRouteTable` character-by-character, pruning
/// candidates until exactly one pattern matches (or none remain). Every byte
/// is captured into `APIPB.reqString`; handlers extract arguments with
/// `sscanf(reqString, route->pattern, ...)`. A `%` in the pattern causes
/// the parser to resolve immediately and skip further candidate pruning.
typedef struct APIRoute {
    const char*    pattern;  ///< sscanf-compatible pattern used for both route matching and typed argument extraction.
    APISyntax      syntax;   ///< Serialisation format for the response.
    APIRequestCode reqCode;  ///< Logical request code for the matched route.
    APIHandler     handler;  ///< Direct handler function to call on dispatch.
} APIRoute;

#ifdef APICODEC_C
// clang-format off
/// @brief Master route table mapping every supported pattern to its handler.
///
/// Each logical endpoint has two entries: one for the REST path form and one for
/// the CLI shorthand. The parser resolves whichever prefix matches first.
/// This array is only instantiated inside apicodec.c (when APICODEC_C is defined).
const APIRoute apiRouteTable[] = {
    // Devices — REST
    { "get /devices",               APISyntaxREST,    APIReqDeviceList, HandlerDeviceList },
    { "get /devices/%s/%s",         APISyntaxREST,    APIReqDeviceGet,  HandlerDeviceGet  },
    { "put /devices/%s/%s",         APISyntaxREST,    APIReqDeviceSet,  HandlerDeviceSet  },
    // Devices — CLI
    { "devices",                    APISyntaxCLI,     APIReqDeviceList, HandlerDeviceList },
    { "device %s %s",              APISyntaxCLI,     APIReqDeviceGet,  HandlerDeviceGet  },
    { "set device %s %s %d",       APISyntaxCLI,     APIReqDeviceSet,  HandlerDeviceSet  },
    // Reflow — REST
    { "get /reflow/status",         APISyntaxREST,    APIReqReflowStatus, HandlerReflowStatus },
    { "put /reflow/run",            APISyntaxREST,    APIReqReflowRun,    HandlerReflowRun    },
    { "put /reflow/stop",           APISyntaxREST,    APIReqReflowStop,   HandlerReflowStop   },
    // Reflow — CLI
    { "status",                     APISyntaxCLI,     APIReqReflowStatus, HandlerReflowStatus },
    { "run profile %s",             APISyntaxCLI,     APIReqReflowRun,    HandlerReflowRun    },
    { "stop",                       APISyntaxCLI,     APIReqReflowStop,   HandlerReflowStop   },
    // Profiles — REST
    { "get /profiles",              APISyntaxREST,    APIReqProfileList,   HandlerProfileList  },
    { "get /profiles/%s",           APISyntaxREST,    APIReqProfileGet,    HandlerProfileGet   },
    { "post /profiles/%s",          APISyntaxREST,    APIReqProfileCreate, HandlerProfileCreate },
    { "put /profiles/%s",           APISyntaxREST,    APIReqProfileUpdate, HandlerProfileUpdate },
    { "delete /profiles/%s",        APISyntaxREST,    APIReqProfileDelete, HandlerProfileDelete },
    // Profiles — CLI
    { "list profiles",              APISyntaxCLI,     APIReqProfileList,   HandlerProfileList  },
    { "show profile %s",            APISyntaxCLI,     APIReqProfileGet,    HandlerProfileGet   },
    { "create profile %s",          APISyntaxCLI,     APIReqProfileCreate, HandlerProfileCreate },
    { "update profile %s",          APISyntaxCLI,     APIReqProfileUpdate, HandlerProfileUpdate },
    { "delete profile %s",          APISyntaxCLI,     APIReqProfileDelete, HandlerProfileDelete },
    // Config — REST
    { "get /config",                APISyntaxREST,    APIReqConfigGet, HandlerConfigGet },
    { "put /config",                APISyntaxREST,    APIReqConfigPut, HandlerConfigPut },
    // Config — CLI
    { "config",                     APISyntaxCLI,     APIReqConfigGet, HandlerConfigGet },
    // Logs — REST
    { "get /logs",                  APISyntaxREST,    APIReqLogsList,  HandlerLogsList  },
    { "get /logs/%s",               APISyntaxREST,    APIReqLogGet,    HandlerLogGet    },
    { "delete /logs/%s",            APISyntaxREST,    APIReqLogDelete, HandlerLogDelete },
    { "delete /logs",               APISyntaxREST,    APIReqLogsClear, HandlerLogsClear },
    // Logs — CLI
    { "list logs",                  APISyntaxCLI,     APIReqLogsList,  HandlerLogsList  },
    { "show log %s",                APISyntaxCLI,     APIReqLogGet,    HandlerLogGet    },
    { "delete log %s",              APISyntaxCLI,     APIReqLogDelete, HandlerLogDelete },
    { "clear logs",                 APISyntaxCLI,     APIReqLogsClear, HandlerLogsClear },
    // Storage — REST
    { "get /storage",               APISyntaxREST,    APIReqStorageGet,    HandlerStorageGet    },
    { "put /storage/format",        APISyntaxREST,    APIReqStorageFormat, HandlerStorageFormat },
    // Storage — CLI
    { "storage",                    APISyntaxCLI,     APIReqStorageGet,    HandlerStorageGet    },
    { "format storage",             APISyntaxCLI,     APIReqStorageFormat, HandlerStorageFormat },
    // Files — REST
    { "get /storage/files",         APISyntaxREST,    APIReqFileList,  HandlerFileList  },
    { "get /storage/files/%s",      APISyntaxREST,    APIReqFileList,  HandlerFileList  },
    { "get /storage/file/%s",       APISyntaxREST,    APIReqFileRead,  HandlerFileRead  },
    { "put /storage/file/%s",       APISyntaxREST,    APIReqFileWrite, HandlerFileWrite },
    { "delete /storage/file/%s",    APISyntaxREST,    APIReqFileDelete,HandlerFileDelete },
    // Files — CLI
    { "ls",                         APISyntaxCLI,     APIReqFileList,  HandlerFileList  },
    { "ls %s",                      APISyntaxCLI,     APIReqFileList,  HandlerFileList  },
    { "cat %s",                     APISyntaxCLI,     APIReqFileRead,  HandlerFileRead  },
    { "write %s",                   APISyntaxCLI,     APIReqFileWrite, HandlerFileWrite },
    { "rm %s",                      APISyntaxCLI,     APIReqFileDelete,HandlerFileDelete },
    // System — REST
    { "get /system/status",         APISyntaxREST,    APIReqSystemStatus, HandlerSystemStatus },
    { "get /system/stats",          APISyntaxREST,    APIReqSystemStats,  HandlerSystemStats  },
    { "get /system/power",          APISyntaxREST,    APIReqSystemPower,  HandlerSystemPower  },
    { "put /system/debug",          APISyntaxREST,    APIReqSystemDebug,  HandlerSystemDebug  },
    { "put /system/stop",           APISyntaxREST,    APIReqSystemStop,   HandlerSystemStop   },
    { "get /system/clock",          APISyntaxREST,    APIReqClockGet,     HandlerClockGet     },
    { "put /system/clock",          APISyntaxREST,    APIReqClockPut,     HandlerClockPut     },
    { "put /system/reset",          APISyntaxREST,    APIReqSystemReset,  HandlerSystemReset  },
    // System — CLI
    { "pool",                       APISyntaxCLI,     APIReqSystemStats,  HandlerSystemStats  },
    { "sysstat",                    APISyntaxCLI,     APIReqSystemStatus, HandlerSystemStatus },
    { "power",                      APISyntaxCLI,     APIReqSystemPower,  HandlerSystemPower  },
    { "debug",                      APISyntaxCLI,     APIReqSystemDebug,  HandlerSystemDebug  },
    { "sysstop",                    APISyntaxCLI,     APIReqSystemStop,   HandlerSystemStop   },
    { "clock",                      APISyntaxCLI,     APIReqClockGet,     HandlerClockGet     },
    { "set clock %s",               APISyntaxCLI,     APIReqClockPut,     HandlerClockPut     },
    { "reset",                      APISyntaxCLI,     APIReqSystemReset,  HandlerSystemReset  }
};

/// @brief Number of entries in `apiRouteTable`.
#define API_ROUTE_TABLE_SIZE ( sizeof( apiRouteTable ) / sizeof( APIRoute ) )
// clang-format on
#endif // APICODEC_C


#endif // APIROUTES_H
