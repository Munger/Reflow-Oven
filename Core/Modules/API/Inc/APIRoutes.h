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
/// Each route pattern is an `sscanf`-compatible prefix string. A bare `%s` in the
/// pattern marks a wildcard argument; everything up to the terminator after `%s`
/// is captured into `APIPB.rawRequest`. Patterns without `%s` treat all remaining
/// bytes as payload body.
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
    API_REQ_OVEN_STATUS,      ///< GET /oven/status
    API_REQ_OVEN_RUN,         ///< PUT /oven/run
    API_REQ_OVEN_STOP,        ///< PUT /oven/stop
    API_REQ_OVEN_ESTOP,       ///< PUT /oven/estop
    // Manual Control
    API_REQ_MANUAL_ENABLE,    ///< PUT /oven/manual/enable
    API_REQ_MANUAL_DISABLE,   ///< PUT /oven/manual/disable
    API_REQ_MANUAL_HEATER,    ///< PUT /oven/manual/heater
    API_REQ_MANUAL_FAN,       ///< PUT /oven/manual/fan
    // Sensors
    API_REQ_SENSORS_TEMP,     ///< GET /sensors/temperature
    API_REQ_SENSORS_MAINS,    ///< GET /sensors/mains
    // Profiles
    API_REQ_PROFILE_LIST,     ///< GET /profiles
    API_REQ_PROFILE_GET,      ///< GET /profiles/%s
    API_REQ_PROFILE_CREATE,   ///< POST /profiles/%s
    API_REQ_PROFILE_UPDATE,   ///< PUT /profiles/%s
    API_REQ_PROFILE_DELETE,   ///< DELETE /profiles/%s
    // Config
    API_REQ_CONFIG_GET,       ///< GET /config
    API_REQ_CONFIG_PUT,       ///< PUT /config
    // Logs
    API_REQ_LOGS_LIST,        ///< GET /logs
    API_REQ_LOG_GET,          ///< GET /logs/%s
    API_REQ_LOG_DELETE,       ///< DELETE /logs/%s
    API_REQ_LOGS_CLEAR,       ///< DELETE /logs
    // Storage
    API_REQ_STORAGE_GET,      ///< GET /storage
    API_REQ_STORAGE_FORMAT,   ///< PUT /storage/format
    // System
    API_REQ_SYSTEM_STATUS,    ///< GET /system/status
    API_REQ_CLOCK_GET,        ///< GET /system/clock
    API_REQ_CLOCK_PUT,        ///< PUT /system/clock
    API_REQ_SYSTEM_RESET,     ///< PUT /system/reset
    // Power
    API_REQ_POWER_GET,        ///< GET /power
    // UI
    API_REQ_UI_LIGHT,         ///< PUT /ui/light
    API_REQ_UI_BUZZER         ///< PUT /ui/buzzer
} APIRequestCode;

/// @brief A single entry in the API route table.
///
/// The stream parser walks `apiRouteTable` character-by-character, pruning
/// candidates until exactly one pattern matches (or none remain). A `%` in
/// the pattern causes the parser to resolve immediately and start capturing
/// subsequent bytes into `APIPB.rawRequest`.
typedef struct APIRoute {
    const char*    pattern;  ///< sscanf-compatible pattern, e.g. `"GET /profiles/%s"`.
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
    // Oven
    { "GET /oven/status",           API_REQ_OVEN_STATUS,    HandlerOvenStatus    },
    { "status",                     API_REQ_OVEN_STATUS,    HandlerOvenStatus    },
    { "PUT /oven/run",              API_REQ_OVEN_RUN,       HandlerOvenRun       },
    { "run profile %s",             API_REQ_OVEN_RUN,       HandlerOvenRun       },
    { "PUT /oven/stop",             API_REQ_OVEN_STOP,      HandlerOvenStop      },
    { "stop",                       API_REQ_OVEN_STOP,      HandlerOvenStop      },
    { "PUT /oven/estop",            API_REQ_OVEN_ESTOP,     HandlerOvenEstop     },
    { "estop",                      API_REQ_OVEN_ESTOP,     HandlerOvenEstop     },
    // Manual Control
    { "PUT /oven/manual/enable",    API_REQ_MANUAL_ENABLE,  HandlerManualEnable  },
    { "set manual on",              API_REQ_MANUAL_ENABLE,  HandlerManualEnable  },
    { "PUT /oven/manual/disable",   API_REQ_MANUAL_DISABLE, HandlerManualDisable },
    { "set manual off",             API_REQ_MANUAL_DISABLE, HandlerManualDisable },
    { "PUT /oven/manual/heater",    API_REQ_MANUAL_HEATER,  HandlerManualHeater  },
    { "set heater %s %d",           API_REQ_MANUAL_HEATER,  HandlerManualHeater  },
    { "PUT /oven/manual/fan",       API_REQ_MANUAL_FAN,     HandlerManualFan     },
    { "set fan %d",                 API_REQ_MANUAL_FAN,     HandlerManualFan     },
    // Sensors
    { "GET /sensors/temperature",   API_REQ_SENSORS_TEMP,   HandlerSensorsTemp   },
    { "temp",                       API_REQ_SENSORS_TEMP,   HandlerSensorsTemp   },
    { "GET /sensors/mains",         API_REQ_SENSORS_MAINS,  HandlerSensorsMains  },
    { "mains",                      API_REQ_SENSORS_MAINS,  HandlerSensorsMains  },
    // Profiles
    { "GET /profiles",              API_REQ_PROFILE_LIST,   HandlerProfilesList  },
    { "list profiles",              API_REQ_PROFILE_LIST,   HandlerProfilesList  },
    { "GET /profiles/%s",           API_REQ_PROFILE_GET,    HandlerProfileGet    },
    { "show profile %s",            API_REQ_PROFILE_GET,    HandlerProfileGet    },
    { "POST /profiles/%s",          API_REQ_PROFILE_CREATE, HandlerProfileCreate },
    { "create profile %s",          API_REQ_PROFILE_CREATE, HandlerProfileCreate },
    { "PUT /profiles/%s",           API_REQ_PROFILE_UPDATE, HandlerProfileUpdate },
    { "update profile %s",          API_REQ_PROFILE_UPDATE, HandlerProfileUpdate },
    { "DELETE /profiles/%s",        API_REQ_PROFILE_DELETE, HandlerProfileDelete },
    { "delete profile %s",          API_REQ_PROFILE_DELETE, HandlerProfileDelete },
    // Config
    { "GET /config",                API_REQ_CONFIG_GET,     HandlerConfigGet     },
    { "config",                     API_REQ_CONFIG_GET,     HandlerConfigGet     },
    { "PUT /config",                API_REQ_CONFIG_PUT,     HandlerConfigPut     },
    // Logs
    { "GET /logs",                  API_REQ_LOGS_LIST,      HandlerLogsList      },
    { "list logs",                  API_REQ_LOGS_LIST,      HandlerLogsList      },
    { "GET /logs/%s",               API_REQ_LOG_GET,        HandlerLogGet        },
    { "show log %s",                API_REQ_LOG_GET,        HandlerLogGet        },
    { "DELETE /logs/%s",            API_REQ_LOG_DELETE,     HandlerLogDelete     },
    { "delete log %s",              API_REQ_LOG_DELETE,     HandlerLogDelete     },
    { "DELETE /logs",               API_REQ_LOGS_CLEAR,     HandlerLogsClear     },
    { "clear logs",                 API_REQ_LOGS_CLEAR,     HandlerLogsClear     },
    // Storage
    { "GET /storage",               API_REQ_STORAGE_GET,    HandlerStorageGet    },
    { "storage",                    API_REQ_STORAGE_GET,    HandlerStorageGet    },
    { "PUT /storage/format",        API_REQ_STORAGE_FORMAT, HandlerStorageFormat },
    { "format storage",             API_REQ_STORAGE_FORMAT, HandlerStorageFormat },
    // System
    { "GET /system/status",         API_REQ_SYSTEM_STATUS,  HandlerSystemStatus  },
    { "sysstat",                    API_REQ_SYSTEM_STATUS,  HandlerSystemStatus  },
    { "GET /system/clock",          API_REQ_CLOCK_GET,      HandlerClockGet      },
    { "clock",                      API_REQ_CLOCK_GET,      HandlerClockGet      },
    { "PUT /system/clock",          API_REQ_CLOCK_PUT,      HandlerClockPut      },
    { "set clock %s",               API_REQ_CLOCK_PUT,      HandlerClockPut      },
    { "PUT /system/reset",          API_REQ_SYSTEM_RESET,   HandlerSystemReset   },
    { "reset",                      API_REQ_SYSTEM_RESET,   HandlerSystemReset   },
    // Power
    { "GET /power",                 API_REQ_POWER_GET,      HandlerPowerGet      },
    { "power",                      API_REQ_POWER_GET,      HandlerPowerGet      },
    // UI
    { "PUT /ui/light",              API_REQ_UI_LIGHT,       HandlerUiLight       },
    { "set light %s",               API_REQ_UI_LIGHT,       HandlerUiLight       },
    { "PUT /ui/buzzer",             API_REQ_UI_BUZZER,      HandlerUiBuzzer      },
    { "buzz %s",                    API_REQ_UI_BUZZER,      HandlerUiBuzzer      }
};

/// @brief Number of entries in `apiRouteTable`.
#define API_ROUTE_TABLE_SIZE ( sizeof( apiRouteTable ) / sizeof( APIRoute ) )
// clang-format on
#endif // APICODEC_C


#endif // APIROUTES_H
