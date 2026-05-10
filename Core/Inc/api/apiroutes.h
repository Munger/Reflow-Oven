#ifndef APIROUTES_H
#define APIROUTES_H

#include <stdint.h>

#include "apitypes.h"
#include "apihandlers.h"

typedef enum {
    // Oven
    API_REQ_OVEN_STATUS,
    API_REQ_OVEN_RUN,
    API_REQ_OVEN_STOP,
    API_REQ_OVEN_ESTOP,
    // Manual Control
    API_REQ_MANUAL_ENABLE,
    API_REQ_MANUAL_DISABLE,
    API_REQ_MANUAL_HEATER,
    API_REQ_MANUAL_FAN,
    // Sensors
    API_REQ_SENSORS_TEMP,
    API_REQ_SENSORS_MAINS,
    // Profiles
    API_REQ_PROFILE_LIST,
    API_REQ_PROFILE_GET,
    API_REQ_PROFILE_CREATE,
    API_REQ_PROFILE_UPDATE,
    API_REQ_PROFILE_DELETE,
    // Config
    API_REQ_CONFIG_GET,
    API_REQ_CONFIG_PUT,
    // Logs
    API_REQ_LOGS_LIST,
    API_REQ_LOG_GET,
    API_REQ_LOG_DELETE,
    API_REQ_LOGS_CLEAR,
    // Storage
    API_REQ_STORAGE_GET,
    API_REQ_STORAGE_FORMAT,
    // System
    API_REQ_SYSTEM_STATUS,
    API_REQ_CLOCK_GET,
    API_REQ_CLOCK_PUT,
    API_REQ_SYSTEM_RESET,
    // Power
    API_REQ_POWER_GET,
    // UI
    API_REQ_UI_LIGHT,
    API_REQ_UI_BUZZER
} APIRequestCode;

// A single route table entry
typedef struct APIRoute {
    const char*    pattern;   // sscanf-compatible pattern e.g. "GET /profiles/%s"
    APIRequestCode reqCode;   // Request code for handler dispatch
    APIHandler     handler;   // Direct handler dispatch
} APIRoute, *APIRoutePtr;

#ifdef APICODEC_C
// clang-format off
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
#define API_ROUTE_TABLE_SIZE ( sizeof( apiRouteTable ) / sizeof( APIRoute ) )
// clang-format on
#endif // APICODEC_C


#endif // APIROUTES_H