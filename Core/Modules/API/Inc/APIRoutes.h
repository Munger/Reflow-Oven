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
    // Oven
    APIReqOvenStatus,      ///< get /oven/status
    APIReqOvenRun,         ///< put /oven/run
    APIReqOvenStop,        ///< put /oven/stop
    APIReqOvenEstop,       ///< put /oven/estop
    // Manual Control
    APIReqManualEnable,    ///< put /oven/manual/enable
    APIReqManualDisable,   ///< put /oven/manual/disable
    APIReqManualHeater,    ///< put /oven/manual/heater
    APIReqManualFan,       ///< put /oven/manual/fan
    // Sensors
    APIReqSensorsTemp,     ///< get /sensors/temperature
    APIReqSensorsMains,    ///< get /sensors/mains
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
    // Storage
    APIReqStorageGet,      ///< get /storage
    APIReqStorageFormat,   ///< put /storage/format
    // System
    APIReqSystemStatus,    ///< get /system/status
    APIReqClockGet,        ///< get /system/clock
    APIReqClockPut,        ///< put /system/clock
    APIReqSystemReset,     ///< put /system/reset
    // Power
    APIReqPowerGet,        ///< get /power
    // UI
    APIReqUiLight,         ///< put /ui/light
    APIReqUiBuzzer         ///< put /ui/buzzer
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
} APIRoute, *APIRoutePtr;

#ifdef APICODEC_C
// clang-format off
/// @brief Master route table mapping every supported pattern to its handler.
///
/// Each logical endpoint has two entries: one for the REST path form and one for
/// the CLI shorthand. The parser resolves whichever prefix matches first.
/// This array is only instantiated inside apicodec.c (when APICODEC_C is defined).
const APIRoute apiRouteTable[] = {
    // Oven — REST
    { "get /oven/status",           APISyntaxREST,    APIReqOvenStatus,    HandlerOvenStatus },
    { "put /oven/run",              APISyntaxREST,    APIReqOvenRun,       HandlerOvenRun    },
    { "put /oven/stop",             APISyntaxREST,    APIReqOvenStop,      HandlerOvenStop   },
    { "put /oven/estop",            APISyntaxREST,    APIReqOvenEstop,     HandlerOvenEstop  },
    // Oven — CLI
    { "status",                     APISyntaxCLI,     APIReqOvenStatus,    HandlerOvenStatus },
    { "run profile %s",             APISyntaxCLI,     APIReqOvenRun,       HandlerOvenRun    },
    { "stop",                       APISyntaxCLI,     APIReqOvenStop,      HandlerOvenStop   },
    { "estop",                      APISyntaxCLI,     APIReqOvenEstop,     HandlerOvenEstop  },
    // Manual Control — REST
    { "put /oven/manual/enable",    APISyntaxREST,    APIReqManualEnable,  HandlerManualEnable  },
    { "put /oven/manual/disable",   APISyntaxREST,    APIReqManualDisable, HandlerManualDisable },
    { "put /oven/manual/heater",    APISyntaxREST,    APIReqManualHeater,  HandlerManualHeater  },
    { "put /oven/manual/fan",       APISyntaxREST,    APIReqManualFan,     HandlerManualFan     },
    // Manual Control — CLI
    { "set manual on",              APISyntaxCLI,     APIReqManualEnable,  HandlerManualEnable  },
    { "set manual off",             APISyntaxCLI,     APIReqManualDisable, HandlerManualDisable },
    { "set heater %s %d",           APISyntaxCLI,     APIReqManualHeater,  HandlerManualHeater  },
    { "set fan %d",                 APISyntaxCLI,     APIReqManualFan,     HandlerManualFan     },
    // Sensors — REST
    { "get /sensors/temperature",   APISyntaxREST,    APIReqSensorsTemp,   HandlerSensorsTemp  },
    { "get /sensors/mains",         APISyntaxREST,    APIReqSensorsMains,  HandlerSensorsMains },
    // Sensors — CLI
    { "temp",                       APISyntaxCLI,     APIReqSensorsTemp,   HandlerSensorsTemp  },
    { "mains",                      APISyntaxCLI,     APIReqSensorsMains,  HandlerSensorsMains },
    // Profiles — REST
    { "get /profiles",              APISyntaxREST,    APIReqProfileList,   HandlerProfilesList  },
    { "get /profiles/%s",           APISyntaxREST,    APIReqProfileGet,    HandlerProfileGet    },
    { "post /profiles/%s",          APISyntaxREST,    APIReqProfileCreate, HandlerProfileCreate },
    { "put /profiles/%s",           APISyntaxREST,    APIReqProfileUpdate, HandlerProfileUpdate },
    { "delete /profiles/%s",        APISyntaxREST,    APIReqProfileDelete, HandlerProfileDelete },
    // Profiles — CLI
    { "list profiles",              APISyntaxCLI,     APIReqProfileList,   HandlerProfilesList  },
    { "show profile %s",            APISyntaxCLI,     APIReqProfileGet,    HandlerProfileGet    },
    { "create profile %s",          APISyntaxCLI,     APIReqProfileCreate, HandlerProfileCreate },
    { "update profile %s",          APISyntaxCLI,     APIReqProfileUpdate, HandlerProfileUpdate },
    { "delete profile %s",          APISyntaxCLI,     APIReqProfileDelete, HandlerProfileDelete },
    // Config — REST
    { "get /config",                APISyntaxREST,    APIReqConfigGet,     HandlerConfigGet },
    { "put /config",                APISyntaxREST,    APIReqConfigPut,     HandlerConfigPut },
    // Config — CLI
    { "config",                     APISyntaxCLI,     APIReqConfigGet,     HandlerConfigGet },
    // Logs — REST
    { "get /logs",                  APISyntaxREST,    APIReqLogsList,      HandlerLogsList  },
    { "get /logs/%s",               APISyntaxREST,    APIReqLogGet,        HandlerLogGet    },
    { "delete /logs/%s",            APISyntaxREST,    APIReqLogDelete,     HandlerLogDelete },
    { "delete /logs",               APISyntaxREST,    APIReqLogsClear,     HandlerLogsClear },
    // Logs — CLI
    { "list logs",                  APISyntaxCLI,     APIReqLogsList,      HandlerLogsList  },
    { "show log %s",                APISyntaxCLI,     APIReqLogGet,        HandlerLogGet    },
    { "delete log %s",              APISyntaxCLI,     APIReqLogDelete,     HandlerLogDelete },
    { "clear logs",                 APISyntaxCLI,     APIReqLogsClear,     HandlerLogsClear },
    // Storage — REST
    { "get /storage",               APISyntaxREST,    APIReqStorageGet,    HandlerStorageGet    },
    { "put /storage/format",        APISyntaxREST,    APIReqStorageFormat, HandlerStorageFormat },
    // Storage — CLI
    { "storage",                    APISyntaxCLI,     APIReqStorageGet,    HandlerStorageGet    },
    { "format storage",             APISyntaxCLI,     APIReqStorageFormat, HandlerStorageFormat },
    // System — REST
    { "get /system/status",         APISyntaxREST,    APIReqSystemStatus,  HandlerSystemStatus },
    { "get /system/clock",          APISyntaxREST,    APIReqClockGet,      HandlerClockGet     },
    { "put /system/clock",          APISyntaxREST,    APIReqClockPut,      HandlerClockPut     },
    { "put /system/reset",          APISyntaxREST,    APIReqSystemReset,   HandlerSystemReset  },
    // System — CLI
    { "sysstat",                    APISyntaxCLI,     APIReqSystemStatus,  HandlerSystemStatus },
    { "clock",                      APISyntaxCLI,     APIReqClockGet,      HandlerClockGet     },
    { "set clock %s",               APISyntaxCLI,     APIReqClockPut,      HandlerClockPut     },
    { "reset",                      APISyntaxCLI,     APIReqSystemReset,   HandlerSystemReset  },
    // Power — REST
    { "get /power",                 APISyntaxREST,    APIReqPowerGet,      HandlerPowerGet },
    // Power — CLI
    { "power",                      APISyntaxCLI,     APIReqPowerGet,      HandlerPowerGet },
    // UI — REST
    { "put /ui/light",              APISyntaxREST,    APIReqUiLight,       HandlerUiLight  },
    { "put /ui/buzzer",             APISyntaxREST,    APIReqUiBuzzer,      HandlerUiBuzzer },
    // UI — CLI
    { "set light %s",               APISyntaxCLI,     APIReqUiLight,       HandlerUiLight  },
    { "buzz %s",                    APISyntaxCLI,     APIReqUiBuzzer,      HandlerUiBuzzer }
};

/// @brief Number of entries in `apiRouteTable`.
#define API_ROUTE_TABLE_SIZE ( sizeof( apiRouteTable ) / sizeof( APIRoute ) )
// clang-format on
#endif // APICODEC_C


#endif // APIROUTES_H
