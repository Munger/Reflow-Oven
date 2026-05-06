# Reflow Oven Controller — Serial REST API

## Overview

All communication is over USB CDC (Serial). The MCU accepts JSON requests and returns JSON responses. Plain text shorthand commands are also accepted and mapped to the equivalent JSON operations.

### Request Envelope
```json
{
  "method": "GET|POST|PUT|DELETE",
  "path": "/resource/subresource",
  "body": {}
}
```

### Response Envelope
```json
{
  "status": 200,
  "path": "/resource/subresource",
  "body": {}
}
```

### Status Codes
| Code | Meaning |
|------|---------|
| 200 | OK |
| 201 | Created |
| 204 | No Content |
| 400 | Bad Request |
| 404 | Not Found |
| 409 | Conflict (e.g. oven already running) |
| 500 | Internal Error |

### Unsolicited Push Events
The MCU may push events at any time without a prior request:
```json
{
  "event": "event_name",
  "data": {}
}
```

---

## /oven

### GET /oven/status
Returns current oven state.

**Response:**
```json
{
  "status": 200,
  "path": "/oven/status",
  "body": {
    "state": "idle|running|manual|cooling|fault|estop",
    "profile": "profile_name_or_null",
    "stage": "preheat|soak|reflow|cooldown|null",
    "elapsed_s": 123,
    "remaining_s": 45
  }
}
```

**Text shorthand:** `STATUS`

---

### PUT /oven/run
Start a stored profile.

**Request:**
```json
{
  "method": "PUT",
  "path": "/oven/run",
  "body": {
    "profile": "profile_name"
  }
}
```

**Response:** `200 OK` or `409 Conflict` if already running.

**Text shorthand:** `RUN <profile_name>`

---

### PUT /oven/stop
Stop the current profile and begin cooldown.

**Response:** `200 OK`

**Text shorthand:** `STOP`

---

### PUT /oven/estop
Software emergency stop. Immediately disables hot side power.

**Response:** `200 OK`

**Text shorthand:** `ESTOP`

---

### /oven/manual
Manual control mode for testing and commissioning. Must be explicitly enabled before use. Oven must be idle.

#### PUT /oven/manual/enable
Enable manual control mode.

**Response:** `200 OK` or `409 Conflict` if oven is running.

**Text shorthand:** `MANUAL ON`

---

#### PUT /oven/manual/disable
Disable manual control mode. All outputs set to off.

**Response:** `200 OK`

**Text shorthand:** `MANUAL OFF`

---

#### PUT /oven/manual/heater
Set individual heater power. Requires manual mode enabled.

**Request:**
```json
{
  "method": "PUT",
  "path": "/oven/manual/heater",
  "body": {
    "heater": "top|bottom|rear",
    "power_pct": 50
  }
}
```

**Response:** `200 OK`

**Text shorthand:** `HEATER <top|bottom|rear> <0-100>`

---

#### PUT /oven/manual/fan
Set fan speed. Requires manual mode enabled.

**Request:**
```json
{
  "method": "PUT",
  "path": "/oven/manual/fan",
  "body": {
    "speed_pct": 75
  }
}
```

**Response:** `200 OK`

**Text shorthand:** `FAN <0-100>`

---

## /sensors

### GET /sensors/temperature
Returns all thermocouple readings.

**Response:**
```json
{
  "status": 200,
  "path": "/sensors/temperature",
  "body": {
    "oven": 23.5,
    "cjt1": 22.1,
    "cjt2": 22.3,
    "unit": "C",
    "timestamp": "2025-01-01T12:00:00"
  }
}
```

**Text shorthand:** `TEMP`

---

### GET /sensors/mains
Returns mains frequency derived from ZCD.

**Response:**
```json
{
  "status": 200,
  "path": "/sensors/mains",
  "body": {
    "frequency_hz": 50.01,
    "present": true
  }
}
```

**Text shorthand:** `MAINS`

---

## /profiles

### GET /profiles
List all stored profiles.

**Response:**
```json
{
  "status": 200,
  "path": "/profiles",
  "body": {
    "profiles": [
      { "name": "leaded_standard", "size_bytes": 512 },
      { "name": "leadfree_rma", "size_bytes": 640 }
    ]
  }
}
```

**Text shorthand:** `PROFILES`

---

### GET /profiles/{name}
Retrieve a specific profile.

**Response:**
```json
{
  "status": 200,
  "path": "/profiles/leaded_standard",
  "body": {
    "name": "leaded_standard",
    "stages": [
      { "name": "preheat",  "target_c": 150, "duration_s": 90,  "fan_pct": 0  },
      { "name": "soak",     "target_c": 180, "duration_s": 60,  "fan_pct": 0  },
      { "name": "reflow",   "target_c": 220, "duration_s": 45,  "fan_pct": 0  },
      { "name": "cooldown", "target_c": 50,  "duration_s": 120, "fan_pct": 100 }
    ]
  }
}
```

**Text shorthand:** `PROFILE <name>`

---

### POST /profiles/{name}
Create or overwrite a profile.

**Request body:** Same structure as GET response body.

**Response:** `201 Created`

**Text shorthand:** Not available — use JSON.

---

### DELETE /profiles/{name}
Delete a profile.

**Response:** `204 No Content`

**Text shorthand:** `DELPROFILE <name>`

---

## /config

### GET /config
Retrieve all system configuration.

**Response:**
```json
{
  "status": 200,
  "path": "/config",
  "body": {
    "pid": {
      "kp": 1.0,
      "ki": 0.1,
      "kd": 0.05
    },
    "temp_unit": "C",
    "log_level": "info|debug|warn|error",
    "buzzer_enabled": true,
    "light_on_run": true
  }
}
```

**Text shorthand:** `CONFIG`

---

### PUT /config
Update configuration. Partial updates accepted.

**Request body:** Any subset of the config structure.

**Response:** `200 OK`

---

## /logs

### GET /logs
List all log files.

**Response:**
```json
{
  "status": 200,
  "path": "/logs",
  "body": {
    "logs": [
      { "name": "2025-01-01.log", "size_bytes": 4096 },
      { "name": "2025-01-02.log", "size_bytes": 2048 }
    ]
  }
}
```

**Text shorthand:** `LOGS`

---

### GET /logs/{name}
Download a log file. Response body is plain text log content.

**Text shorthand:** `LOG <name>`

---

### DELETE /logs/{name}
Delete a log file.

**Response:** `204 No Content`

**Text shorthand:** `DELLOG <name>`

---

### DELETE /logs
Delete all log files.

**Response:** `204 No Content`

**Text shorthand:** `CLEARLOGS`

---

## /storage

### GET /storage
Flash storage status.

**Response:**
```json
{
  "status": 200,
  "path": "/storage",
  "body": {
    "total_bytes": 67108864,
    "used_bytes": 1048576,
    "free_bytes": 66060288
  }
}
```

**Text shorthand:** `STORAGE`

---

### PUT /storage/format
Format the flash filesystem. Destructive — deletes all profiles and logs.

**Request:**
```json
{
  "method": "PUT",
  "path": "/storage/format",
  "body": { "confirm": true }
}
```

**Response:** `200 OK`

---

## /system

### GET /system/status
System health and fault status.

**Response:**
```json
{
  "status": 200,
  "path": "/system/status",
  "body": {
    "firmware_version": "1.0.0",
    "uptime_s": 3600,
    "faults": [],
    "clock_source": "HSE|HSI",
    "watchdog": true
  }
}
```

**Text shorthand:** `SYSSTAT`

---

### GET /system/clock
Get current RTC date and time.

**Response:**
```json
{
  "status": 200,
  "path": "/system/clock",
  "body": {
    "datetime": "2025-01-01T12:00:00"
  }
}
```

**Text shorthand:** `CLOCK`

---

### PUT /system/clock
Set RTC date and time.

**Request:**
```json
{
  "method": "PUT",
  "path": "/system/clock",
  "body": {
    "datetime": "2025-01-01T12:00:00"
  }
}
```

**Response:** `200 OK`

**Text shorthand:** `SETCLOCK <ISO8601>`

---

### PUT /system/reset
Soft reset the MCU.

**Response:** `200 OK` (may not be received if reset is immediate)

**Text shorthand:** `RESET`

---

## /power

### GET /power
Power delivery and supply status.

**Response:**
```json
{
  "status": 200,
  "path": "/power",
  "body": {
    "usb_pd": {
      "connected": true,
      "voltage_v": 20.0,
      "current_a": 3.0,
      "power_w": 60.0,
      "role": "sink|source|dual"
    },
    "input_voltage_v": 24.1,
    "battery_present": false,
    "battery_level_pct": null
  }
}
```

**Text shorthand:** `POWER`

---

## /ui

### PUT /ui/light
Control the oven light.

**Request:**
```json
{
  "method": "PUT",
  "path": "/ui/light",
  "body": {
    "on": true
  }
}
```

**Response:** `200 OK`

**Text shorthand:** `LIGHT <on|off>`

---

### PUT /ui/buzzer
Trigger the buzzer.

**Request:**
```json
{
  "method": "PUT",
  "path": "/ui/buzzer",
  "body": {
    "pattern": "beep|alarm|success|off",
    "duration_ms": 500
  }
}
```

**Response:** `200 OK`

**Text shorthand:** `BUZZ <pattern>`

---

## Push Events

The MCU sends unsolicited events during operation. All events use the same envelope:

```json
{ "event": "event_name", "data": {} }
```

| Event | Trigger | Data |
|-------|---------|------|
| `temperature` | Periodic during run | `{ oven, cjt1, cjt2, unit }` |
| `profile_stage` | Stage transition | `{ stage, target_c, elapsed_s }` |
| `profile_complete` | Profile finished | `{ name, duration_s }` |
| `fault` | Any fault condition | `{ code, description }` |
| `estop` | Hardware or software ESTOP | `{ source: "hardware|software" }` |
| `clock_fault` | CSS detects clock failure | `{ clock: "HSE|LSE" }` |
| `thermocouple_fault` | Thermocouple error | `{ sensor, fault_type }` |
| `power_change` | USB-PD negotiation change | `{ voltage_v, current_a, power_w }` |
| `storage_low` | Flash nearly full | `{ free_bytes }` |