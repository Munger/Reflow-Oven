/**
 * @file    api.h
 * @brief   Reflow Oven Controller — Serial REST API
 *
 * All API operations use a parameter block (ApiPB) passed by pointer.
 * The header fields (status, method, path, segments, body_in, body_out) are
 * directly accessible. Resource-specific data is overlaid in a union within
 * the parameter block (pb->resource.oven, pb->resource.sensors, etc.).
 *
 * Request flow:
 *   USB RX → ApiReceive() → ApiParse() → ApiRoute() → handler() → ApiRespond()
 *
 * Push events:
 *   Any context → ApiEventPush()
 */

#ifndef API_H
#define API_H

#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

#define API_MAX_PATH_LEN    128
#define API_MAX_SEGMENTS    8
#define API_MAX_SEG_LEN     64
#define API_MAX_NAME        64
#define API_MAX_PATTERN     16
#define API_MAX_FAULTS      8
#define API_MAX_FAULT_DESC  64
#define API_MAX_STAGES      16
#define API_TX_BUFFER_LEN   APP_TX_DATA_SIZE
#define API_RX_BUFFER_LEN   APP_RX_DATA_SIZE

/* --------------------------------------------------------------------------
 * Status codes
 * -------------------------------------------------------------------------- */

typedef enum {
    API_STATUS_OK                   = 200,
    API_STATUS_CREATED              = 201,
    API_STATUS_NO_CONTENT           = 204,
    API_STATUS_BAD_REQUEST          = 400,
    API_STATUS_NOT_FOUND            = 404,
    API_STATUS_CONFLICT             = 409,
    API_STATUS_INTERNAL_ERROR       = 500,
} APIStatus;

/* --------------------------------------------------------------------------
 * HTTP methods
 * -------------------------------------------------------------------------- */

typedef enum {
    API_METHOD_UNKNOWN  = 0,
    API_METHOD_GET,
    API_METHOD_POST,
    API_METHOD_PUT,
    API_METHOD_DELETE,
} APIMethod;

/* --------------------------------------------------------------------------
 * Oven states
 * -------------------------------------------------------------------------- */

typedef enum {
    OVEN_STATE_IDLE     = 0,
    OVEN_STATE_RUNNING,
    OVEN_STATE_MANUAL,
    OVEN_STATE_COOLING,
    OVEN_STATE_FAULT,
    OVEN_STATE_ESTOP,
} OvenState;

/* --------------------------------------------------------------------------
 * Reflow stages
 * -------------------------------------------------------------------------- */

typedef enum {
    STAGE_NONE      = 0,
    STAGE_PREHEAT,
    STAGE_SOAK,
    STAGE_REFLOW,
    STAGE_COOLDOWN,
} ReflowStage;

/* --------------------------------------------------------------------------
 * Heater identifiers
 * -------------------------------------------------------------------------- */

typedef enum {
    HEATER_TOP      = 0,
    HEATER_BOTTOM,
    HEATER_REAR,
} HeaterID;

/* --------------------------------------------------------------------------
 * USB-PD role
 * -------------------------------------------------------------------------- */

typedef enum {
    PD_ROLE_SINK    = 0,
    PD_ROLE_SOURCE,
    PD_ROLE_DUAL,
} PDRole;

/* --------------------------------------------------------------------------
 * Buzzer patterns
 * -------------------------------------------------------------------------- */

typedef enum {
    BUZZER_OFF      = 0,
    BUZZER_BEEP,
    BUZZER_ALARM,
    BUZZER_SUCCESS,
} BuzzerPattern;

/* --------------------------------------------------------------------------
 * Clock source (for fault reporting)
 * -------------------------------------------------------------------------- */

typedef enum {
    CLOCK_HSE       = 0,
    CLOCK_LSE,
    CLOCK_HSI,
} ClockSource;

/* --------------------------------------------------------------------------
 * ESTOP source
 * -------------------------------------------------------------------------- */

typedef enum {
    ESTOP_HARDWARE  = 0,
    ESTOP_SOFTWARE,
} EstopSource;

/* --------------------------------------------------------------------------
 * /oven — oven control (resource-specific fields)
 * -------------------------------------------------------------------------- */

typedef struct {
    /* GET /oven/status — response fields */
    OvenState   state;
    char        profileName[API_MAX_NAME];
    ReflowStage stage;
    uint32_t    elapsed;
    uint32_t    remaining;

    /* PUT /oven/run — request fields */
    char            runProfile[API_MAX_NAME];

    /* PUT /oven/manual/heater — request fields */
    HeaterID     heater;
    uint8_t         heaterPowerPct;

    /* PUT /oven/manual/fan — request fields */
    uint8_t         fanSpeedPct;
} OvenAPIParam, *OvenAPIParamPtr;

/* --------------------------------------------------------------------------
 * /sensors — sensor readings (resource-specific fields)
 * -------------------------------------------------------------------------- */

typedef struct {
    /* GET /sensors/temperature — response fields */
    float           tempOven;
    float           tempCJT1;
    float           tempCJT2;
    char            tempUnit;          /* 'C' or 'F'   */

    /* GET /sensors/mains — response fields */
    float           mainsFreqHZ;
    bool            mainsPresent;
} SensorsAPIParam, *SensorsAPIParamPtr;

/* --------------------------------------------------------------------------
 * /profiles — profile management (resource-specific fields)
 * -------------------------------------------------------------------------- */

typedef struct {
    char            name[API_MAX_NAME];
    float           targetC;
    uint32_t        duration;
    uint8_t         fanPct;
} APIProfileStage;

typedef struct {
    char                name[API_MAX_NAME];
    APIProfileStage stages[API_MAX_STAGES];
    uint8_t             stageCount;
    uint32_t            sizeBytes;
} APIProfile;

typedef struct {
    /* GET /profiles — response fields */
    APIProfile  *list;               /* caller-allocated array   */
    uint8_t         listCount;

    /* GET|POST|DELETE /profiles/{name} */
    APIProfile   profile;
} ProfilesAPIParam, *ProfilesAPIParamPtr;

/* --------------------------------------------------------------------------
 * /config — system configuration (resource-specific fields)
 * -------------------------------------------------------------------------- */

typedef struct {
    float           kp;
    float           ki;
    float           kd;
} APIPIDConfig;

typedef struct {
    APIPIDConfig        pid;
    char                tempUnit;          /* 'C' or 'F'       */
    uint8_t             logLevel;          /* 0=error 3=debug  */
    bool                buzzerEnabled;
    bool                lightOnRun;
} ConfigAPIParam, *ConfigAPIParamPtr;

/* --------------------------------------------------------------------------
 * /logs — log file management (resource-specific fields)
 * -------------------------------------------------------------------------- */

typedef struct {
    char            name[API_MAX_NAME];
    uint32_t        sizeBytes;
} APILogEntry;

typedef struct {
    /* GET /logs — response fields */
    APILogEntry *list;              /* caller-allocated array   */
    uint8_t          listCount;

    /* GET|DELETE /logs/{name} */
    char             logName[API_MAX_NAME];
    char            *logContent;       /* caller-allocated buffer  */
    uint32_t         logContentLen;
} LogsAPIParam, *LogsAPIParamPtr;

/* --------------------------------------------------------------------------
 * /storage — flash storage management (resource-specific fields)
 * -------------------------------------------------------------------------- */

typedef struct {
    uint32_t        totalBytes;
    uint32_t        usedBytes;
    uint32_t        freeBytes;
    bool            confirmFormat;     /* must be true for PUT /storage/format */
} StorageAPIParam, *StorageAPIParamPtr;

/* --------------------------------------------------------------------------
 * /system — system status and control (resource-specific fields)
 * -------------------------------------------------------------------------- */

typedef struct {
    char            description[API_MAX_FAULT_DESC];
    uint32_t        code;
} APIFault;

typedef struct {
    /* GET /system/status — response fields */
    char            firmwareVersion[16];
    uint32_t        uptime;
    APIFault        faults[API_MAX_FAULTS];
    uint8_t         faultCount;
    ClockSource     clockSource;
    bool            watchdogActive;

    /* GET|PUT /system/clock */
    char            datetime[32];       /* ISO8601: 2025-01-01T12:00:00 */
} SystemAPIParam, *SystemAPIParamPtr;

/* --------------------------------------------------------------------------
 * /power — USB-PD and supply status (resource-specific fields)
 * -------------------------------------------------------------------------- */

typedef struct {
    bool            usbPDConnected;
    float           usbPDVoltage;
    float           usbPDCurrent;
    float           usbPDPower;
    PDRole          usbPDRole;
    float           inputVoltage;
    bool            batteryPresent;
    uint8_t         batteryLevelPct;
} PowerAPIParam, *PowerAPIParamPtr;

/* --------------------------------------------------------------------------
 * /ui — light and buzzer (resource-specific fields)
 * -------------------------------------------------------------------------- */

typedef struct {
    /* PUT /ui/light */
    bool            lightOn;

    /* PUT /ui/buzzer */
    BuzzerPattern   buzzerPattern;
    uint32_t        buzzerDurationMS;
} UIAPIParam, *UIAPIParamPtr;

/* --------------------------------------------------------------------------
 * Master parameter block — header fields + resource union overlay
 * -------------------------------------------------------------------------- */

typedef struct {
    /* Header — always accessible directly */
    APIStatus    status;                         /* result, set by handler          */
    APIMethod    method;                         /* GET / POST / PUT / DELETE       */
    char            path[API_MAX_PATH_LEN];             /* raw path string                 */
    char           *segments[API_MAX_SEGMENTS];     /* tokenised path segments         */
    uint8_t         segmentCount;                  /* number of valid segments        */
    cJSON          *bodyIn;                        /* parsed request body or NULL     */
    cJSON          *bodyOut;                       /* response body, set by handler   */
    bool            isJSON;                        /* true if request was JSON format */

    /* Resource-specific data — union overlay */
    union {
        OvenAPIParam        oven;
        SensorsAPIParam     sensors;
        ProfilesAPIParam    profiles;
        ConfigAPIParam      config;
        LogsAPIParam        logs;
        StorageAPIParam     storage;
        SystemAPIParam      system;
        PowerAPIParam       power;
        UIAPIParam          ui;
    } resource;
} APIPB, *APIPBPtr;

/* --------------------------------------------------------------------------
 * Push event types
 * -------------------------------------------------------------------------- */

typedef enum {
    API_EVENT_TEMPERATURE       = 0,
    API_EVENT_PROFILE_STAGE,
    API_EVENT_PROFILE_COMPLETE,
    API_EVENT_FAULT,
    API_EVENT_ESTOP,
    API_EVENT_CLOCK_FAULT,
    API_EVENT_THERMOCOUPLE_FAULT,
    API_EVENT_POWER_CHANGE,
    API_EVENT_STORAGE_LOW,
} APIEventType;

typedef struct {
    APIEventType    type;
    cJSON           *data;
} APIEvent, *APIEventPtr;

/* --------------------------------------------------------------------------
 * Transport layer
 * -------------------------------------------------------------------------- */

/**
 * @brief Initialise the API layer. Call once after USB CDC is ready.
 */
void ApiInit(void);

/**
 * @brief Process incoming USB data. Call from CDC receive callback.
 * @param buf   Pointer to received data
 * @param len   Length of received data
 */
void ApiReceive(const uint8_t *buf, uint32_t len);

/**
 * @brief Serialise and transmit a response. Called internally by handlers.
 * @param pb    Completed parameter block
 */
void ApiRespond(APIPBPtr pb);

/**
 * @brief Push an unsolicited event to the host.
 * @param event Pointer to event descriptor
 */
void ApiEventPush(const APIEventPtr event);

/* --------------------------------------------------------------------------
 * Parser and router
 * -------------------------------------------------------------------------- */

/**
 * @brief Parse a received line into a parameter block.
 * @param line  NULL-terminated input line
 * @param pb    Output parameter block
 * @return      true if parsing succeeded
 */
bool ApiParse(const char *line, APIPBPtr pb);

/**
 * @brief Route a parsed parameter block to the correct handler.
 * @param pb    Parsed parameter block
 */
void ApiRoute(APIPBPtr pb);

/* --------------------------------------------------------------------------
 * /oven handlers
 * -------------------------------------------------------------------------- */

void ApiOvenGetStatus       (APIPBPtr pb);
void ApiOvenPutRun          (APIPBPtr pb);
void ApiOvenPutStop         (APIPBPtr pb);
void ApiOvenPutEstop        (APIPBPtr pb);
void ApiOvenPutManualEnable (APIPBPtr pb);
void ApiOvenPutManualDisable(APIPBPtr pb);
void ApiOvenPutManualHeater (APIPBPtr pb);
void ApiOvenPutManualFan    (APIPBPtr pb);

/* --------------------------------------------------------------------------
 * /sensors handlers
 * -------------------------------------------------------------------------- */

void ApiSensorsGetTemperature(APIPBPtr pb);
void ApiSensorsGetMains      (APIPBPtr pb);

/* --------------------------------------------------------------------------
 * /profiles handlers
 * -------------------------------------------------------------------------- */

void ApiProfilesGetList  (APIPBPtr pb);
void ApiProfilesGet       (APIPBPtr pb);
void ApiProfilesPost      (APIPBPtr pb);
void ApiProfilesDelete    (APIPBPtr pb);

/* --------------------------------------------------------------------------
 * /config handlers
 * -------------------------------------------------------------------------- */

void ApiConfigGet (APIPBPtr pb);
void ApiConfigPut (APIPBPtr pb);

/* --------------------------------------------------------------------------
 * /logs handlers
 * -------------------------------------------------------------------------- */

void ApiLogsGetList  (APIPBPtr pb);
void ApiLogsGet       (APIPBPtr pb);
void ApiLogsDelete    (APIPBPtr pb);
void ApiLogsDeleteAll(APIPBPtr pb);

/* --------------------------------------------------------------------------
 * /storage handlers
 * -------------------------------------------------------------------------- */

void ApiStorageGet        (APIPBPtr pb);
void ApiStoragePutFormat (APIPBPtr pb);

/* --------------------------------------------------------------------------
 * /system handlers
 * -------------------------------------------------------------------------- */

void ApiSystemGetStatus  (APIPBPtr pb);
void ApiSystemGetClock   (APIPBPtr pb);
void ApiSystemPutClock   (APIPBPtr pb);
void ApiSystemPutReset   (APIPBPtr pb);

/* --------------------------------------------------------------------------
 * /power handlers
 * -------------------------------------------------------------------------- */

void ApiPowerGet(APIPBPtr pb);

/* --------------------------------------------------------------------------
 * /ui handlers
 * -------------------------------------------------------------------------- */

void ApiUIPutLight  (APIPBPtr pb);
void ApiUIPutBuzzer (APIPBPtr pb);

/* --------------------------------------------------------------------------
 * Push event helpers — call from anywhere in firmware
 * -------------------------------------------------------------------------- */

void ApiEventTemperature      (float oven, float cjt1, float cjt2);
void ApiEventProfileStage    (ReflowStage stage, float targetC, uint32_t elapsed);
void ApiEventProfileComplete (const char *name, uint32_t duration);
void ApiEventFault            (uint32_t code, const char *description);
void ApiEventEstop            (EstopSource source);
void ApiEventClockFault      (ClockSource clock);
void ApiEventThermocoupleFault(uint8_t sensor, const char *faultType);
void ApiEventPowerChange     (float voltage, float current, float power);
void ApiEventStorageLow      (uint32_t freeBytes);

#endif /* API_H */