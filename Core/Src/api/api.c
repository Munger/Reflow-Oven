#include "api.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static uint8_t apiRXBuffer[API_RX_BUFFER_LEN];
static uint32_t apiRXLength;

/* --------------------------------------------------------------------------
 * API Tables
 * -------------------------------------------------------------------------- */

typedef void (*APIHandler)(APIPBPtr pb);

typedef struct {
    APIMethod method;
    const char *pattern;
    const char *plainTextCmd;
    APIHandler handler;
} APIRouteEntry, *APIRouteEntryPtr;

typedef bool (*APIEventFormatter)(const char *eventName,
                                  const APIEventPtr event,
                                  char *buffer,
                                  size_t bufferSize,
                                  size_t *outLen);

typedef struct {
    APIEventType type;
    const char *name;
    APIEventFormatter formatter;
} APIEventEntry, *APIEventEntryPtr;

static bool ApiEventFormatGeneric(const char *eventName,
                                     const APIEventPtr event,
                                     char *buffer,
                                     size_t bufferSize,
                                     size_t *outLen);

static const APIRouteEntry apiRouteTable[] = {
    { API_METHOD_GET,    "/config",                NULL,                   ApiConfigGet },
    { API_METHOD_GET,    "/logs",                  NULL,                   ApiLogsGetList },
    { API_METHOD_DELETE, "/logs",                  NULL,                   ApiLogsDeleteAll },
    { API_METHOD_GET,    "/logs/:name",            NULL,                   ApiLogsGet },
    { API_METHOD_DELETE, "/logs/:name",            NULL,                   ApiLogsDelete },
    { API_METHOD_GET,    "/oven/manual",           NULL,                   NULL },
    { API_METHOD_PUT,    "/oven/manual/disable",   NULL,                   ApiOvenPutManualDisable },
    { API_METHOD_PUT,    "/oven/manual/enable",    "MANUAL",               ApiOvenPutManualEnable },
    { API_METHOD_PUT,    "/oven/manual/fan",       NULL,                   ApiOvenPutManualFan },
    { API_METHOD_PUT,    "/oven/manual/heater",    NULL,                   ApiOvenPutManualHeater },
    { API_METHOD_PUT,    "/oven/estop",            "ESTOP",                ApiOvenPutEstop },
    { API_METHOD_PUT,    "/oven/run",              NULL,                   ApiOvenPutRun },
    { API_METHOD_PUT,    "/oven/stop",             "STOP",                 ApiOvenPutStop },
    { API_METHOD_GET,    "/oven/status",           "STATUS",               ApiOvenGetStatus },
    { API_METHOD_GET,    "/power",                 NULL,                   ApiPowerGet },
    { API_METHOD_GET,    "/profiles",              "PROFILES",             ApiProfilesGetList },
    { API_METHOD_POST,   "/profiles",              NULL,                   ApiProfilesPost },
    { API_METHOD_GET,    "/profiles/:name",        NULL,                   ApiProfilesGet },
    { API_METHOD_DELETE, "/profiles/:name",        NULL,                   ApiProfilesDelete },
    { API_METHOD_GET,    "/sensors/mains",         "MAINS",                ApiSensorsGetMains },
    { API_METHOD_GET,    "/sensors/temperature",   "TEMP",                 ApiSensorsGetTemperature },
    { API_METHOD_GET,    "/storage",               NULL,                   ApiStorageGet },
    { API_METHOD_PUT,    "/storage/format",        NULL,                   ApiStoragePutFormat },
    { API_METHOD_GET,    "/system/clock",          NULL,                   ApiSystemGetClock },
    { API_METHOD_PUT,    "/system/clock",          NULL,                   ApiSystemPutClock },
    { API_METHOD_GET,    "/system/status",         NULL,                   ApiSystemGetStatus },
    { API_METHOD_PUT,    "/system/reset",          NULL,                   ApiSystemPutReset },
    { API_METHOD_PUT,    "/ui/buzzer",             NULL,                   ApiUIPutBuzzer },
    { API_METHOD_PUT,    "/ui/light",              NULL,                   ApiUIPutLight },
};

static const size_t APIRouteTableCount = sizeof(apiRouteTable) / sizeof(apiRouteTable[0]);

static const APIEventEntry apiEventTable[] = {
    { API_EVENT_TEMPERATURE,       "temperature",        ApiEventFormatGeneric },
    { API_EVENT_PROFILE_STAGE,     "profile_stage",      ApiEventFormatGeneric },
    { API_EVENT_PROFILE_COMPLETE,  "profile_complete",   ApiEventFormatGeneric },
    { API_EVENT_FAULT,             "fault",              ApiEventFormatGeneric },
    { API_EVENT_ESTOP,             "estop",              ApiEventFormatGeneric },
    { API_EVENT_CLOCK_FAULT,       "clock_fault",        ApiEventFormatGeneric },
    { API_EVENT_THERMOCOUPLE_FAULT, "thermocouple_fault", ApiEventFormatGeneric },
    { API_EVENT_POWER_CHANGE,      "power_change",       ApiEventFormatGeneric },
    { API_EVENT_STORAGE_LOW,       "storage_low",        ApiEventFormatGeneric },
};

static const size_t apiEventTableCount =
    sizeof(apiEventTable) / sizeof(apiEventTable[0]);

/* --------------------------------------------------------------------------
 * Helper: tokenise path into segments
 * -------------------------------------------------------------------------- */
static void ApiTokenisePath(APIPBPtr pb)
{
    if (!pb || !pb->path[0]) {
        pb->segmentCount = 0;
        return;
    }

    /* Copy path to a mutable buffer for tokenisation */
    char pathCopy[API_MAX_PATH_LEN];
    strncpy(pathCopy, pb->path, sizeof(pathCopy) - 1);
    pathCopy[sizeof(pathCopy) - 1] = '\0';

    uint8_t count = 0;
    char *pos = pathCopy;

    /* Skip leading slash */
    if (*pos == '/') {
        pos++;
    }

    while (*pos && count < API_MAX_SEGMENTS) {
        pb->segments[count] = pos;
        
        /* Find next slash */
        char *slash = strchr(pos, '/');
        if (slash) {
            *slash = '\0';
            pos = slash + 1;
        } else {
            pos = strchr(pos, '\0');
        }
        
        count++;
    }

    pb->segmentCount = count;
}

/* --------------------------------------------------------------------------
 * JSON Parser
 * -------------------------------------------------------------------------- */
static bool ApiParseJson(const char *line, APIPBPtr pb)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        pb->status = API_STATUS_BAD_REQUEST;
        return false;
    }

    /* Extract method */
    cJSON *methodObj = cJSON_GetObjectItem(root, "method");
    if (!methodObj || !methodObj->valuestring) {
        cJSON_Delete(root);
        pb->status = API_STATUS_BAD_REQUEST;
        return false;
    }

    if (strcmp(methodObj->valuestring, "GET") == 0) {
        pb->method = API_METHOD_GET;
    } else if (strcmp(methodObj->valuestring, "POST") == 0) {
        pb->method = API_METHOD_POST;
    } else if (strcmp(methodObj->valuestring, "PUT") == 0) {
        pb->method = API_METHOD_PUT;
    } else if (strcmp(methodObj->valuestring, "DELETE") == 0) {
        pb->method = API_METHOD_DELETE;
    } else {
        cJSON_Delete(root);
        pb->status = API_STATUS_BAD_REQUEST;
        return false;
    }

    /* Extract path */
    cJSON *path_obj = cJSON_GetObjectItem(root, "path");
    if (!path_obj || !path_obj->valuestring) {
        cJSON_Delete(root);
        pb->status = API_STATUS_BAD_REQUEST;
        return false;
    }

    strncpy(pb->path, path_obj->valuestring, sizeof(pb->path) - 1);
    pb->path[sizeof(pb->path) - 1] = '\0';

    /* Extract body if present */
    pb->bodyIn = cJSON_GetObjectItem(root, "body");
    if (pb->bodyIn) {
        pb->bodyIn = cJSON_Duplicate(pb->bodyIn, 1);
    }

    cJSON_Delete(root);
    pb->status = API_STATUS_OK;
    ApiTokenisePath(pb);
    return true;
}

/* --------------------------------------------------------------------------
 * Plain text command parser
 * -------------------------------------------------------------------------- */
static bool ApiParsePlainText(const char *line, APIPBPtr pb)
{
    char cmd[API_MAX_NAME];
    int n = sscanf(line, "%63s", cmd);
    
    if (n != 1) {
        pb->status = API_STATUS_BAD_REQUEST;
        return false;
    }

    /* Convert to uppercase */
    for (int i = 0; cmd[i]; i++) {
        cmd[i] = (char)toupper((unsigned char)cmd[i]);
    }

    /* Map plain text commands to API endpoints using the route table */
    for (size_t i = 0; i < APIRouteTableCount; ++i) {
        const APIRouteEntryPtr entry = &apiRouteTable[i];
        if (entry->plainTextCmd && strcmp(cmd, entry->plainTextCmd) == 0) {
            pb->method = entry->method;
            strcpy(pb->path, entry->pattern);
            pb->status = API_STATUS_OK;
            ApiTokenisePath(pb);
            return true;
        }
    }

    pb->status = API_STATUS_NOT_FOUND;
    return false;
}

/* --------------------------------------------------------------------------
 * Transport layer
 * -------------------------------------------------------------------------- */

void ApiInit(void)
{
    apiRXLength = 0;
    memset(apiRXBuffer, 0, sizeof(apiRXBuffer));
}

void ApiReceive(const uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0)) {
        return;
    }

    for (uint32_t i = 0; i < len; ++i) {
        if (apiRXLength < sizeof(apiRXBuffer) - 1) {
            apiRXBuffer[apiRXLength++] = buf[i];
        }

        if (buf[i] == '\n') {
            apiRXBuffer[apiRXLength] = '\0';
            APIPB pb;
            bool parsed = ApiParse((const char *)apiRXBuffer, &pb);
            if (parsed) {
                ApiRoute(&pb);
            }
            ApiRespond(&pb);
            if (pb.bodyIn) {
                cJSON_Delete(pb.bodyIn);
            }
            if (pb.bodyOut) {
                cJSON_Delete(pb.bodyOut);
            }
            apiRXLength = 0;
        }
    }
}

bool ApiParse(const char *line, APIPBPtr pb)
{
    if ((line == NULL) || (pb == NULL)) {
        return false;
    }

    memset(pb, 0, sizeof(*pb));
    pb->status = API_STATUS_BAD_REQUEST;

    /* Skip leading whitespace */
    while (isspace((unsigned char)*line)) {
        line++;
    }

    if (!*line) {
        return false;
    }

    /* Auto-detect JSON vs plain text */
    if (*line == '{') {
        pb->isJSON = true;
        return ApiParseJson(line, pb);
    } else {
        pb->isJSON = false;
        return ApiParsePlainText(line, pb);
    }
}

static bool ApiMatchPattern(const char *pattern, APIPBPtr pb)
{
    const char *p = pattern;
    uint8_t index = 0;

    while (*p && (index < pb->segmentCount)) {
        if (*p != '/') {
            return false;
        }

        p++;
        const char *segmentStart = p;
        while (*p && (*p != '/')) {
            p++;
        }

        size_t pattern_len = (size_t)(p - segmentStart);
        if (pattern_len == 0) {
            return false;
        }

        if (segmentStart[0] != ':') {
            if (!pb->segments[index]
                || strlen(pb->segments[index]) != pattern_len
                || strncmp(pb->segments[index], segmentStart, pattern_len) != 0) {
                return false;
            }
        }

        index++;
    }

    return (*p == '\0') && (index == pb->segmentCount);
}

void ApiRoute(APIPBPtr pb)
{
    if (!pb || pb->segmentCount == 0) {
        if (pb) {
            pb->status = API_STATUS_NOT_FOUND;
        }
        return;
    }

    for (size_t i = 0; i < APIRouteTableCount; ++i) {
        const APIRouteEntryPtr entry = &apiRouteTable[i];

        if (entry->method != pb->method) {
            continue;
        }

        if (entry->handler == NULL) {
            continue;
        }

        if (ApiMatchPattern(entry->pattern, pb)) {
            entry->handler(pb);
            return;
        }
    }

    pb->status = API_STATUS_NOT_FOUND;
}

void ApiRespond(APIPBPtr pb)
{
    if (!pb) {
        return;
    }

    char txBuffer[API_TX_BUFFER_LEN];
    size_t length = 0;

    if (pb->isJSON) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "status", pb->status);
        cJSON_AddStringToObject(root, "path", pb->path);
        if (pb->bodyOut) {
            cJSON_AddItemToObject(root, "body", cJSON_Duplicate(pb->bodyOut, 1));
        } else {
            cJSON_AddItemToObject(root, "body", cJSON_CreateObject());
        }

        char *payload = cJSON_PrintUnformatted(root);
        if (payload) {
            length = strlen(payload);
            if (length > sizeof(txBuffer) - 2) {
                length = sizeof(txBuffer) - 2;
            }
            memcpy(txBuffer, payload, length);
            txBuffer[length++] = '\n';
            cJSON_free(payload);
        }
        cJSON_Delete(root);
    } else {
        int written = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "STATUS: %d\r\nPATH: %s\r\n",
                               pb->status,
                               pb->path);
        if (written < 0) {
            return;
        }
        length = (size_t)written;
        if (length >= sizeof(txBuffer)) {
            length = sizeof(txBuffer) - 1;
        }

        if (pb->bodyOut) {
            char *bodyText = cJSON_PrintUnformatted(pb->bodyOut);
            if (bodyText) {
                int bodyWritten = snprintf(txBuffer + length,
                                            sizeof(txBuffer) - length,
                                            "BODY: %s\r\n",
                                            bodyText);
                if (bodyWritten > 0) {
                    length += (size_t)bodyWritten;
                    if (length >= sizeof(txBuffer)) {
                        length = sizeof(txBuffer) - 1;
                    }
                }
                cJSON_free(bodyText);
            }
        }
    }

    if (length > 0) {
        if (length > sizeof(txBuffer)) {
            length = sizeof(txBuffer);
        }
        CDC_Transmit_FS((uint8_t *)txBuffer, (uint16_t)length);
    }
}

static bool ApiEventFormatGeneric(const char *eventName,
                                     const APIEventPtr event,
                                     char *buffer,
                                     size_t bufferSize,
                                     size_t *outLen)
{
    if ((buffer == NULL) || (outLen == NULL) || (bufferSize == 0)) {
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }

    cJSON_AddStringToObject(root, "event", eventName ? eventName : "unknown");
    if (event && event->data) {
        cJSON_AddItemToObject(root, "data", cJSON_Duplicate(event->data, 1));
    } else {
        cJSON_AddItemToObject(root, "data", cJSON_CreateObject());
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return false;
    }

    size_t length = strlen(payload);
    if (length > bufferSize) {
        length = bufferSize;
    }
    memcpy(buffer, payload, length);
    *outLen = length;
    cJSON_free(payload);
    return true;
}

void ApiEventPush(const APIEventPtr event)
{
    if (!event || (event->type < 0)) {
        return;
    }

    const size_t index = (size_t)event->type;
    if (index >= apiEventTableCount) {
        return;
    }

    const APIEventEntryPtr entry = &apiEventTable[index];
    if (entry->type != event->type) {
        return;
    }

    char txBuffer[API_TX_BUFFER_LEN];
    size_t length = 0;
    if (!entry->formatter(entry->name, event, txBuffer, sizeof(txBuffer) - 1, &length)) {
        return;
    }

    if (length >= sizeof(txBuffer)) {
        length = sizeof(txBuffer) - 1;
    }
    txBuffer[length++] = '\n';
    CDC_Transmit_FS((uint8_t *)txBuffer, (uint16_t)length);
}

static void ApiEventSendJson(APIEventType type, cJSON *data)
{
    APIEvent event = { .type = type, .data = data };
    ApiEventPush(&event);
}

void ApiEventTemperature(float oven, float cjt1, float cjt2)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return;
    }

    cJSON_AddNumberToObject(data, "oven", oven);
    cJSON_AddNumberToObject(data, "cjt1", cjt1);
    cJSON_AddNumberToObject(data, "cjt2", cjt2);
    ApiEventSendJson(API_EVENT_TEMPERATURE, data);
    cJSON_Delete(data);
}

void ApiEventProfileStage(ReflowStage stage, float targetC, uint32_t elapsed)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return;
    }

    cJSON_AddNumberToObject(data, "stage", stage);
    cJSON_AddNumberToObject(data, "target_c", targetC);
    cJSON_AddNumberToObject(data, "elapsed_s", elapsed);
    ApiEventSendJson(API_EVENT_PROFILE_STAGE, data);
    cJSON_Delete(data);
}

void ApiEventProfileComplete(const char *name, uint32_t duration)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return;
    }

    cJSON_AddStringToObject(data, "name", name ? name : "");
    cJSON_AddNumberToObject(data, "duration_s", duration);
    ApiEventSendJson(API_EVENT_PROFILE_COMPLETE, data);
    cJSON_Delete(data);
}

void ApiEventFault(uint32_t code, const char *description)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return;
    }

    cJSON_AddNumberToObject(data, "code", code);
    cJSON_AddStringToObject(data, "description", description ? description : "");
    ApiEventSendJson(API_EVENT_FAULT, data);
    cJSON_Delete(data);
}

void ApiEventEstop(EstopSource source)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return;
    }

    cJSON_AddNumberToObject(data, "source", source);
    ApiEventSendJson(API_EVENT_ESTOP, data);
    cJSON_Delete(data);
}

void ApiEventClockFault(ClockSource clock)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return;
    }

    cJSON_AddNumberToObject(data, "clock", clock);
    ApiEventSendJson(API_EVENT_CLOCK_FAULT, data);
    cJSON_Delete(data);
}

void ApiEventThermocoupleFault(uint8_t sensor, const char *faultType)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return;
    }

    cJSON_AddNumberToObject(data, "sensor", sensor);
    cJSON_AddStringToObject(data, "fault_type", faultType ? faultType : "");
    ApiEventSendJson(API_EVENT_THERMOCOUPLE_FAULT, data);
    cJSON_Delete(data);
}

void ApiEventPowerChange(float voltage, float current, float power)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return;
    }

    cJSON_AddNumberToObject(data, "voltage_v", voltage);
    cJSON_AddNumberToObject(data, "current_a", current);
    cJSON_AddNumberToObject(data, "power_w", power);
    ApiEventSendJson(API_EVENT_POWER_CHANGE, data);
    cJSON_Delete(data);
}

void ApiEventStorageLow(uint32_t freeBytes)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return;
    }

    cJSON_AddNumberToObject(data, "free_bytes", freeBytes);
    ApiEventSendJson(API_EVENT_STORAGE_LOW, data);
    cJSON_Delete(data);
}
