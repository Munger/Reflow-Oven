/**
 * @file    api.h
 * @brief   Reflow Oven Controller — Serial REST API
 *
 * All API operations use a parameter block (api_pb_t) passed by pointer.
 * The union shares memory across all resource types. The common header
 * must be the first member of every resource-specific struct.
 *
 * Request flow:
 *   USB RX → api_receive() → api_parse() → api_route() → handler() → api_respond()
 *
 * Push events:
 *   Any context → api_event_push()
 */

#ifndef API_H
#define API_H

#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

#define API_MAX_PATH        128
#define API_MAX_SEGMENTS    8
#define API_MAX_SEG_LEN     64
#define API_MAX_NAME        64
#define API_MAX_PATTERN     16
#define API_MAX_FAULTS      8
#define API_MAX_FAULT_DESC  64
#define API_MAX_STAGES      16
#define API_TX_BUFFER       2048
#define API_RX_BUFFER       2048

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
} api_status_t;

/* --------------------------------------------------------------------------
 * HTTP methods
 * -------------------------------------------------------------------------- */

typedef enum {
    API_METHOD_UNKNOWN  = 0,
    API_METHOD_GET,
    API_METHOD_POST,
    API_METHOD_PUT,
    API_METHOD_DELETE,
} api_method_t;

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
} oven_state_t;

/* --------------------------------------------------------------------------
 * Reflow stages
 * -------------------------------------------------------------------------- */

typedef enum {
    STAGE_NONE      = 0,
    STAGE_PREHEAT,
    STAGE_SOAK,
    STAGE_REFLOW,
    STAGE_COOLDOWN,
} reflow_stage_t;

/* --------------------------------------------------------------------------
 * Heater identifiers
 * -------------------------------------------------------------------------- */

typedef enum {
    HEATER_TOP      = 0,
    HEATER_BOTTOM,
    HEATER_REAR,
} heater_id_t;

/* --------------------------------------------------------------------------
 * USB-PD role
 * -------------------------------------------------------------------------- */

typedef enum {
    PD_ROLE_SINK    = 0,
    PD_ROLE_SOURCE,
    PD_ROLE_DUAL,
} pd_role_t;

/* --------------------------------------------------------------------------
 * Buzzer patterns
 * -------------------------------------------------------------------------- */

typedef enum {
    BUZZER_OFF      = 0,
    BUZZER_BEEP,
    BUZZER_ALARM,
    BUZZER_SUCCESS,
} buzzer_pattern_t;

/* --------------------------------------------------------------------------
 * Clock source (for fault reporting)
 * -------------------------------------------------------------------------- */

typedef enum {
    CLOCK_HSE       = 0,
    CLOCK_LSE,
    CLOCK_HSI,
} clock_source_t;

/* --------------------------------------------------------------------------
 * ESTOP source
 * -------------------------------------------------------------------------- */

typedef enum {
    ESTOP_HARDWARE  = 0,
    ESTOP_SOFTWARE,
} estop_source_t;

/* --------------------------------------------------------------------------
 * Common parameter block header — must be first member of every pb struct
 * -------------------------------------------------------------------------- */

typedef struct {
    api_status_t    status;                         /* result, set by handler          */
    api_method_t    method;                         /* GET / POST / PUT / DELETE       */
    char            path[API_MAX_PATH];             /* raw path string                 */
    char           *segments[API_MAX_SEGMENTS];     /* tokenised path segments         */
    uint8_t         segment_count;                  /* number of valid segments        */
    cJSON          *body_in;                        /* parsed request body or NULL     */
    cJSON          *body_out;                       /* response body, set by handler   */
} api_pb_common_t;

/* --------------------------------------------------------------------------
 * /oven — oven control
 * -------------------------------------------------------------------------- */

typedef struct {
    api_pb_common_t common;

    /* GET /oven/status — response fields */
    oven_state_t    state;
    char            profile_name[API_MAX_NAME];
    reflow_stage_t  stage;
    uint32_t        elapsed_s;
    uint32_t        remaining_s;

    /* PUT /oven/run — request fields */
    char            run_profile[API_MAX_NAME];

    /* PUT /oven/manual/heater — request fields */
    heater_id_t     heater;
    uint8_t         heater_power_pct;

    /* PUT /oven/manual/fan — request fields */
    uint8_t         fan_speed_pct;
} api_oven_pb_t;

/* --------------------------------------------------------------------------
 * /sensors — sensor readings
 * -------------------------------------------------------------------------- */

typedef struct {
    api_pb_common_t common;

    /* GET /sensors/temperature — response fields */
    float           temp_oven;
    float           temp_cjt1;
    float           temp_cjt2;
    char            temp_unit;          /* 'C' or 'F'   */

    /* GET /sensors/mains — response fields */
    float           mains_freq_hz;
    bool            mains_present;
} api_sensors_pb_t;

/* --------------------------------------------------------------------------
 * /profiles — profile management
 * -------------------------------------------------------------------------- */

typedef struct {
    char            name[API_MAX_NAME];
    float           target_c;
    uint32_t        duration_s;
    uint8_t         fan_pct;
} api_profile_stage_t;

typedef struct {
    char                name[API_MAX_NAME];
    api_profile_stage_t stages[API_MAX_STAGES];
    uint8_t             stage_count;
    uint32_t            size_bytes;
} api_profile_t;

typedef struct {
    api_pb_common_t common;

    /* GET /profiles — response fields */
    api_profile_t  *list;               /* caller-allocated array   */
    uint8_t         list_count;

    /* GET|POST|DELETE /profiles/{name} */
    api_profile_t   profile;
} api_profiles_pb_t;

/* --------------------------------------------------------------------------
 * /config — system configuration
 * -------------------------------------------------------------------------- */

typedef struct {
    float           kp;
    float           ki;
    float           kd;
} api_pid_config_t;

typedef struct {
    api_pb_common_t common;

    api_pid_config_t    pid;
    char                temp_unit;          /* 'C' or 'F'       */
    uint8_t             log_level;          /* 0=error 3=debug  */
    bool                buzzer_enabled;
    bool                light_on_run;
} api_config_pb_t;

/* --------------------------------------------------------------------------
 * /logs — log file management
 * -------------------------------------------------------------------------- */

typedef struct {
    char            name[API_MAX_NAME];
    uint32_t        size_bytes;
} api_log_entry_t;

typedef struct {
    api_pb_common_t common;

    /* GET /logs — response fields */
    api_log_entry_t *list;              /* caller-allocated array   */
    uint8_t          list_count;

    /* GET|DELETE /logs/{name} */
    char             log_name[API_MAX_NAME];
    char            *log_content;       /* caller-allocated buffer  */
    uint32_t         log_content_len;
} api_logs_pb_t;

/* --------------------------------------------------------------------------
 * /storage — flash storage management
 * -------------------------------------------------------------------------- */

typedef struct {
    api_pb_common_t common;

    uint32_t        total_bytes;
    uint32_t        used_bytes;
    uint32_t        free_bytes;
    bool            confirm_format;     /* must be true for PUT /storage/format */
} api_storage_pb_t;

/* --------------------------------------------------------------------------
 * /system — system status and control
 * -------------------------------------------------------------------------- */

typedef struct {
    char            description[API_MAX_FAULT_DESC];
    uint32_t        code;
} api_fault_t;

typedef struct {
    api_pb_common_t common;

    /* GET /system/status — response fields */
    char            firmware_version[16];
    uint32_t        uptime_s;
    api_fault_t     faults[API_MAX_FAULTS];
    uint8_t         fault_count;
    clock_source_t  clock_source;
    bool            watchdog_active;

    /* GET|PUT /system/clock */
    char            datetime[32];       /* ISO8601: 2025-01-01T12:00:00 */
} api_system_pb_t;

/* --------------------------------------------------------------------------
 * /power — USB-PD and supply status
 * -------------------------------------------------------------------------- */

typedef struct {
    api_pb_common_t common;

    bool            usb_pd_connected;
    float           usb_pd_voltage_v;
    float           usb_pd_current_a;
    float           usb_pd_power_w;
    pd_role_t       usb_pd_role;
    float           input_voltage_v;
    bool            battery_present;
    uint8_t         battery_level_pct;
} api_power_pb_t;

/* --------------------------------------------------------------------------
 * /ui — light and buzzer
 * -------------------------------------------------------------------------- */

typedef struct {
    api_pb_common_t common;

    /* PUT /ui/light */
    bool            light_on;

    /* PUT /ui/buzzer */
    buzzer_pattern_t buzzer_pattern;
    uint32_t         buzzer_duration_ms;
} api_ui_pb_t;

/* --------------------------------------------------------------------------
 * Master parameter block union
 * -------------------------------------------------------------------------- */

typedef union {
    api_pb_common_t     common;
    api_oven_pb_t       oven;
    api_sensors_pb_t    sensors;
    api_profiles_pb_t   profiles;
    api_config_pb_t     config;
    api_logs_pb_t       logs;
    api_storage_pb_t    storage;
    api_system_pb_t     system;
    api_power_pb_t      power;
    api_ui_pb_t         ui;
} api_pb_t;

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
} api_event_type_t;

typedef struct {
    api_event_type_t    type;
    cJSON              *data;
} api_event_t;

/* --------------------------------------------------------------------------
 * Transport layer
 * -------------------------------------------------------------------------- */

/**
 * @brief Initialise the API layer. Call once after USB CDC is ready.
 */
void api_init(void);

/**
 * @brief Process incoming USB data. Call from CDC receive callback.
 * @param buf   Pointer to received data
 * @param len   Length of received data
 */
void api_receive(const uint8_t *buf, uint32_t len);

/**
 * @brief Serialise and transmit a response. Called internally by handlers.
 * @param pb    Completed parameter block
 */
void api_respond(const api_pb_t *pb);

/**
 * @brief Push an unsolicited event to the host.
 * @param event Pointer to event descriptor
 */
void api_event_push(const api_event_t *event);

/* --------------------------------------------------------------------------
 * Parser and router
 * -------------------------------------------------------------------------- */

/**
 * @brief Parse a received line into a parameter block.
 * @param line  NULL-terminated input line
 * @param pb    Output parameter block
 * @return      true if parsing succeeded
 */
bool api_parse(const char *line, api_pb_t *pb);

/**
 * @brief Route a parsed parameter block to the correct handler.
 * @param pb    Parsed parameter block
 */
void api_route(api_pb_t *pb);

/* --------------------------------------------------------------------------
 * /oven handlers
 * -------------------------------------------------------------------------- */

void api_oven_get_status       (api_pb_t *pb);
void api_oven_put_run          (api_pb_t *pb);
void api_oven_put_stop         (api_pb_t *pb);
void api_oven_put_estop        (api_pb_t *pb);
void api_oven_put_manual_enable(api_pb_t *pb);
void api_oven_put_manual_disable(api_pb_t *pb);
void api_oven_put_manual_heater(api_pb_t *pb);
void api_oven_put_manual_fan   (api_pb_t *pb);

/* --------------------------------------------------------------------------
 * /sensors handlers
 * -------------------------------------------------------------------------- */

void api_sensors_get_temperature(api_pb_t *pb);
void api_sensors_get_mains      (api_pb_t *pb);

/* --------------------------------------------------------------------------
 * /profiles handlers
 * -------------------------------------------------------------------------- */

void api_profiles_get_list  (api_pb_t *pb);
void api_profiles_get       (api_pb_t *pb);
void api_profiles_post      (api_pb_t *pb);
void api_profiles_delete    (api_pb_t *pb);

/* --------------------------------------------------------------------------
 * /config handlers
 * -------------------------------------------------------------------------- */

void api_config_get (api_pb_t *pb);
void api_config_put (api_pb_t *pb);

/* --------------------------------------------------------------------------
 * /logs handlers
 * -------------------------------------------------------------------------- */

void api_logs_get_list  (api_pb_t *pb);
void api_logs_get       (api_pb_t *pb);
void api_logs_delete    (api_pb_t *pb);
void api_logs_delete_all(api_pb_t *pb);

/* --------------------------------------------------------------------------
 * /storage handlers
 * -------------------------------------------------------------------------- */

void api_storage_get        (api_pb_t *pb);
void api_storage_put_format (api_pb_t *pb);

/* --------------------------------------------------------------------------
 * /system handlers
 * -------------------------------------------------------------------------- */

void api_system_get_status  (api_pb_t *pb);
void api_system_get_clock   (api_pb_t *pb);
void api_system_put_clock   (api_pb_t *pb);
void api_system_put_reset   (api_pb_t *pb);

/* --------------------------------------------------------------------------
 * /power handlers
 * -------------------------------------------------------------------------- */

void api_power_get(api_pb_t *pb);

/* --------------------------------------------------------------------------
 * /ui handlers
 * -------------------------------------------------------------------------- */

void api_ui_put_light  (api_pb_t *pb);
void api_ui_put_buzzer (api_pb_t *pb);

/* --------------------------------------------------------------------------
 * Push event helpers — call from anywhere in firmware
 * -------------------------------------------------------------------------- */

void api_event_temperature      (float oven, float cjt1, float cjt2);
void api_event_profile_stage    (reflow_stage_t stage, float target_c, uint32_t elapsed_s);
void api_event_profile_complete (const char *name, uint32_t duration_s);
void api_event_fault            (uint32_t code, const char *description);
void api_event_estop            (estop_source_t source);
void api_event_clock_fault      (clock_source_t clock);
void api_event_thermocouple_fault(uint8_t sensor, const char *fault_type);
void api_event_power_change     (float voltage_v, float current_a, float power_w);
void api_event_storage_low      (uint32_t free_bytes);

#endif /* API_H */