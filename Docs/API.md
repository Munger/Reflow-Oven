# Reflow Oven Controller — Serial REST API

## Overview

All communication is over USB CDC (Serial). The MCU accepts plain-text
method+path requests (with optional body) and returns JSON responses.
Short CLI-style commands are also accepted.

### Wire Format (REST)

```
METHOD /path/subpath\r\n
body bytes\r\n
```

The request line is `<METHOD> <path>`.  An optional body follows — JSON
`{}`, tagged CSV, or raw bytes depending on the endpoint.  No envelope.

Responses are a single JSON object:

```json
{ "status": 200, "message": "OK", "data": null }
```

The `data` field contains the payload or `null`.  Structured payloads
(e.g. profile CSV) are embedded as a quoted JSON string in `data`.

### Wire Format (CLI)

```
command arg1 arg2
```

Line ending follows whatever the terminal sends (`\n` or `\r\n`).

Response:

```
\r\n[OK] payload\r\n>
```

Leading CRLF, trailing `>` prompt, `[OK]`/`[ERR]` prefix for terminal use.

### Status Codes

| Code | Meaning |
|------|---------|
| 200 | OK |
| 201 | Created |
| 202 | Accepted — async operation started; poll for result |
| 204 | No Content |
| 400 | Bad Request |
| 403 | Forbidden — action not permitted in current state |
| 404 | Not Found |
| 409 | Conflict (e.g. oven already running) |
| 422 | Unprocessable — semantically invalid body |
| 500 | Internal Error |
| 501 | Not Implemented — route is valid but hardware not fitted |
| 503 | Unavailable — service temporarily busy (e.g. filesystem) |

---

## 1. Devices

The `/devices` tree exposes every registered instance on this board.
Nothing is a singleton — each instance has a `<type>/<name>` path.
What exists depends on the board revision and runtime registration.

When debug mode is enabled (see System → Debug Mode), device GET
responses include a `flags` field with the raw driver status bitmask.

### Discovery

List all registered device instances.

**REST:** `GET /devices`
**CLI:**  `devices`

**Response (200):**
```json
{
  "status": 200,
  "message": "OK",
  "data": {
    "instances": [
      { "type": "heater",  "name": "top" },
      { "type": "heater",  "name": "rear" },
      { "type": "heater",  "name": "bottom" },
      { "type": "light",   "name": "oven" },
      { "type": "thermocouple", "name": "oven" },
      { "type": "thermistor",   "name": "cjt1" },
      { "type": "thermistor",   "name": "cjt2" },
      { "type": "thermistor",   "name": "oven" },
      { "type": "thermistor",   "name": "heatsink" },
      { "type": "tachometer",   "name": "ovenFan" },
      { "type": "fan",     "name": "oven" },
      { "type": "fan",     "name": "board" },
      { "type": "buzzer",  "name": "main" },
      { "type": "oven",    "name": "main" }
    ]
  }
}
```

### Heater

A resistive heating element.  Power is expressed as percent (0–100).

**REST:** `GET /devices/heater/<name>`, `PUT /devices/heater/<name>`
**CLI:**  `heater <name>`, `heater <name> <0-100>`

| Method | Body | Action |
|--------|------|--------|
| GET | — | Return current power and status |
| PUT | `{ "power": 50 }` | Set output power 0–100 |

**GET response (200):**
```json
{
  "data": {
    "power": 50,
    "on": true,
    "ready": true
  }
}
```

### Light

An on/off or dimmable AC output. Brightness is expressed as percent
(0–100); dimmable lights accept the full range, on/off lamps treat
any value > 0 as on.

**REST:** `GET /devices/light/<name>`, `PUT /devices/light/<name>`
**CLI:**  `light <name>`, `light <name> <0-100>`

| Method | Body | Action |
|--------|------|--------|
| GET | — | Return current brightness and status |
| PUT | `{ "brightness": 100 }` | Set brightness 0–100 |

**GET response (200):**
```json
{
  "data": {
    "brightness": 100,
    "on": true,
    "ready": true
  }
}
```

### Thermocouple

A thermocouple channel returning hot-junction and cold-junction
temperatures in milli-°C.

**REST:** `GET /devices/thermocouple/<name>`
**CLI:**  `temp <name>`

```json
{
  "data": {
    "temp": 23500,
    "cjt": 22100,
    "fault": null
  }
}
```

### Thermistor

A thermistor temperature reading in milli-°C.

**REST:** `GET /devices/thermistor/<name>`
**CLI:**  `ntc <name>`

```json
{
  "data": {
    "temp": 24200
  }
}
```

### Tachometer

A rotational-speed sensor reading RPM.

**REST:** `GET /devices/tachometer/<name>`
**CLI:**  `rpm <name>`

### Fan

An AC or DC fan.  AC fans without an encoder operate in on/off mode only
(power is either 0 or 100).  DC fans accept 0–100 percent.

**REST:** `GET /devices/fan/<name>`, `PUT /devices/fan/<name>`
**CLI:**  `fan <name>`, `fan <name> <0-100>`

| Method | Body | Action |
|--------|------|--------|
| GET | — | Return current power and status |
| PUT | `{ "power": 80 }` | Set fan power 0–100 |

### Buzzer

**REST:** `PUT /devices/buzzer/<name>`
**CLI:**  `buzz <name> <pattern>`

Body: `{ "pattern": "beep|alarm|success|off", "duration": 500 }` (milliseconds)

### Oven Controller

A closed-loop temperature controller managing a set of heaters,
thermocouples, and a fan to maintain a target temperature.  The
reflow engine commands one or more oven instances but the controller
itself is also exposed for direct use.

Temperatures in milli-°C.

**REST:** `GET /devices/oven/<name>`, `PUT /devices/oven/<name>`

| Method | Body | Action |
|--------|------|--------|
| GET | — | Return current temp, target, pid state |
| PUT | `{ "target": 150000 }` | Set target temperature (milli-°C) |
| PUT | `{ "mode": "idle" }` | Stop control |

---

### Cycle Status

**REST:** `GET /reflow/status`
**CLI:**  `status`

```json
{
  "data": {
    "state": "idle|running|cooling|fault|estop",
    "profile": "sac305",
    "stage": "preheat|soak|reflow|cooldown|null",
    "elapsed": 123,
    "remaining": 45
  }
}
```

### Run Profile

**REST:** `PUT /reflow/run`  (body: `{ "profile": "sac305" }`)
**CLI:**  `run profile <name>`

**Response:** `200 OK` or `409 Conflict`.

### Stop

**REST:** `PUT /reflow/stop`
**CLI:**  `stop`

---

## 2. Profiles

### List Profiles

**REST:** `GET /profiles`
**CLI:**  `list profiles`

```json
{
  "data": {
    "profiles": [
      { "name": "sac305", "size": 512 }
    ]
  }
}
```

### Get Profile

**REST:** `GET /profiles/<name>`
**CLI:**  `show profile <name>`

`data` is a quoted string of tagged CSV stages separated by `;`:

```json
{
  "data": "tyPreheat,tc150000,rr2000,...;tySoak,tc183000,..."
}
```

**CSV tag reference (per stage):**

| Tag | Type | Description |
|-----|------|-------------|
| `ty` | string | Stage label, no spaces |
| `tc` | int | Target temp in milli-°C; 0 = no target (start stage immediately) |
| `rr` | int | Ramp rate in milli-°C/s, signed |
| `to` | uint | Max ms to wait for target; 0 = no timeout |
| `hv` | uint | ms to hold once target is reached |
| `fs` | ushort | Fan speed 0–1000 (permille) |
| `fa` | uint | ms to accelerate fan; 0 = instant |
| `h0` | bool | Heater top, 0 or 1 |
| `h1` | bool | Heater rear, 0 or 1 |
| `h2` | bool | Heater bottom, 0 or 1 |
| `fn` | int | Stage function: 0 = none, 1 = Fan Calibration, 2 = Thermal Calibration |

Stages are separated by `;` when multiple appear in one body.

### Create / Overwrite Profile

Body is raw tagged CSV.  Stages `;`-delimited.

**REST:**
```
POST /profiles/sac305\r\n
tyPreheat,tc150000,...;tySoak,tc183000,...\r\n
```
**CLI (one-shot):**
```
create profile sac305 tyPreheat,tc150000,...;tySoak,tc183000,...
```
**CLI (two-step):**
```
create profile sac305 stages:Preheat,Soak,Reflow,Cooldown
update profile sac305 stage=Preheat tc=150000,rr=2000,hv=60000,fs=200
```

**Response:** `201 Created`

### Update Profile

**REST:** `PUT /profiles/<name>`  (body: same CSV format)
**CLI:**  `update profile <name> ...`

**Response:** `200 OK`

### Add / Delete Stage

**CLI:**
```
add profile sac305 stage=Cooling tc=50000,rr=-3000,hv=30000,fs=800
delete profile sac305 stage=Cooling
```

### Delete Profile

**REST:** `DELETE /profiles/<name>`
**CLI:**  `delete profile <name>`

**Response:** `204 No Content`

---

## 3. Configuration

### Get Configuration

**REST:** `GET /config`
**CLI:**  `config`

### Update Configuration

**REST:** `PUT /config`  (body: any subset of config keys)
**CLI:**  not available — use REST

**Response:** `200 OK`

---

## 4. Logs

### List Logs

**REST:** `GET /logs`
**CLI:**  `list logs`

### Get Log

**REST:** `GET /logs/<name>`
**CLI:**  `show log <name>`

Response payload is raw log content (plain text).

### Delete Log

**REST:** `DELETE /logs/<name>`
**CLI:**  `delete log <name>`

### Clear Logs

**REST:** `DELETE /logs`
**CLI:**  `clear logs`

---

## 5. Storage / File System

### Storage Status

**REST:** `GET /storage`
**CLI:**  `storage`

### Format

**REST:** `PUT /storage/format`  (body: `{ "confirm": true }`)
**CLI:**  `format storage`

### List Directory

**REST:** `GET /storage/files`  or  `GET /storage/files/<path>`
**CLI:**  `ls`  or  `ls /profiles`

### Read File

**REST:** `GET /storage/file/<path>`
**CLI:**  `cat <path>`

### Write File

**REST:** `PUT /storage/file/<path>`  (body: raw file bytes)
**CLI:**  not available — use REST

### Delete File

**REST:** `DELETE /storage/file/<path>`
**CLI:**  `rm <path>`

---

## 6. System

### System Status

**REST:** `GET /system/status`
**CLI:**  `sysstat`

### Pool Statistics

Returns static-pool utilisation for the API engine — current free
counts, peak usage, and per-pool byte consumption. Useful for tuning
the pool sizing constants in APITypes.h.

**REST:** `GET /system/stats`
**CLI:**  `pool`

```json
{
  "data": {
    "pb": { "free": 8, "peak": 4, "count": 10, "bytes": 640 },
    "payload": { "free": 52, "peak": 12, "count": 64, "bytes": 34816 },
    "buffer": { "free": 6, "peak": 3, "count": 8, "bytes": 4160 }
  }
}
```

### Get / Set Clock

**REST:** `GET /system/clock`, `PUT /system/clock`
**CLI:**  `clock`, `set clock <ISO8601>`

### Debug Mode

When enabled, device GET responses include the raw driver status
bitmask (`flags` field) for diagnostics.  Not persisted — resets on
power cycle.

**REST:** `PUT /system/debug`  (body: `{ "on": true }`)
**CLI:**  `debug on`, `debug off`

**Response:** `200 OK`

### System Stop

Gracefully stops any active reflow cycle, de-energises all hot-side
drivers (heaters, fan, light), waits for TRIACs to commutate off, then
isolates hot-side power via the relay. Non-destructive — no MCU reset.

**REST:** `PUT /system/stop`
**CLI:**  `sysstop`

### Reset

**REST:** `PUT /system/reset`
**CLI:**  `reset`

### Power Status

**REST:** `GET /system/power`
**CLI:**  `power`

USB PD contract, input voltage, battery status.

---

## 7. Push Events

Unsolicited events sent at any time:

```json
{ "event": "profileStage", "data": {} }
```

| Event | Trigger | Data |
|-------|---------|------|
| `temperature` | Periodic during run | `{ oven, cjt1, cjt2 }` |
| `profileStage` | Stage transition | `{ stage, target, elapsed }` |
| `profileComplete` | Profile finished | `{ name, duration }` |
| `fault` | Any fault condition | `{ code, description }` |
| `estop` | Hardware or software ESTOP | `{ source }` |
| `clockFault` | CSS detects clock failure | `{ clock }` |
| `thermocoupleFault` | Thermocouple error | `{ sensor, faultType }` |
| `powerChange` | USB-PD negotiation change | `{ voltage, current, power }` |
| `storageLow` | Flash nearly full | `{ free }` |

---

## 8. Future Endpoints

- **Firmware Management** — upload firmware images, select active image
