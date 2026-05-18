# Function Reference

---

## ACFan

*AC oven circulation fan driver.*

---

## ACFanTuning

*AC fan calibration and runtime drive engine — public API.*

---

## ACLight

*Oven interior light driver.*

---

## APICodec

*USB stream parser and response serialiser — public interface.*

### Functions

#### `APIStreamInit`

```c
void APIStreamInit( void )
```

Initialise (or reset) the incremental stream parser. Call once at startup.

Initialise (or reset) the incremental stream parser. Call once at startup.

#### `ProcessStream`

```c
void ProcessStream( const uint8_t * data, uint32_t len )
```

Consume a raw CDC receive buffer and advance the parser state machine.

May be called from ISR context (CDC_Receive_FS). Assembles complete APIPB requests and enqueues them on the input queue, notifying the API task.

| Parameter | Description |
|-----------|-------------|
| `data` | Received bytes. |
| `len` | Number of bytes in data. |

#### `GetNextRequest`

```c
APIPBPtr GetNextRequest( void )
```

Dequeue the next complete parsed request, or NULL if none are available.

The caller takes ownership and must return the PB with ReleasePB() when done.

The caller takes ownership and must call ReleasePB() when done.

**Returns:** Pointer to the oldest queued APIPB, or NULL.

#### `APIQueueForSend`

```c
void APIQueueForSend( APIPBPtr pb )
```

Serialise a completed APIPB response and enqueue it for USB transmission.

Dispatches to JSON or CLI format based on pb->origin. Releases the payload chain; the caller is responsible for the PB itself.

| Parameter | Description |
|-----------|-------------|
| `pb` | Completed APIPB with status, origin, terminator, and optional payload set. |

---

## APICore

*API engine public interface — pool allocators, queue accessors, and diagnostics.*

### Types

#### `APICoreStats`

Live diagnostics snapshot for the API memory engine.

| Type | Field | Description |
|------|-------|-------------|
| `uint32_t` | `pbFree` | APIPB nodes currently in the free pool. |
| `uint32_t` | `payloadFree` | Payload nodes currently in the free pool. |
| `uint32_t` | `bufferFree` | APIBuffer nodes currently in the free pool. |
| `uint32_t` | `inputQueued` | Requests currently waiting in the input queue. |
| `uint32_t` | `outputQueued` | Response buffers currently waiting in the output queue. |
| `uint32_t` | `pbPeak` | Peak simultaneous APIPB nodes in use (high-water mark). |
| `uint32_t` | `payloadPeak` | Peak simultaneous Payload nodes in use (high-water mark). |
| `uint32_t` | `bufferPeak` | Peak simultaneous APIBuffer nodes in use (high-water mark). |
| `uint32_t` | `pbCount` | Total APIPB pool capacity. |
| `uint32_t` | `payloadCount` | Total Payload pool capacity. |
| `uint32_t` | `bufferCount` | Total APIBuffer pool capacity. |
| `size_t` | `pbSize` | Size in bytes of one APIPB. |
| `size_t` | `payloadSize` | Size in bytes of one Payload. |
| `size_t` | `bufferSize` | Size in bytes of one APIBuffer. |
| `size_t` | `pbMemUsed` | Total bytes currently in use for APIPBs. |
| `size_t` | `payloadMemUsed` | Total bytes currently in use for Payloads. |
| `size_t` | `bufferMemUsed` | Total bytes currently in use for APIBuffers. |

### Functions

#### `APICoreInit`

```c
void APICoreInit( void )
```

Initialise pools and queues. Call once at startup before any other APICore function.

Initialise pools and queues. Call once at startup before any other APICore function.

Must be called once before any other APICore function. APITaskInit() calls this after waiting for FlagSystemInitialised. All previously acquired objects are invalidated; do not call after startup.

#### `GetInputQueue`

```c
APIPBQueueRef GetInputQueue( void )
```

Return the input queue — received requests awaiting dispatch.

Return the input queue — received requests awaiting dispatch.

**Returns:** Queue reference for use with EnqueuePB() / DequeuePB().

#### `GetOutputQueue`

```c
APIBufferQueueRef GetOutputQueue( void )
```

Return the output queue — serialised responses awaiting transmission.

Return the output queue — serialised responses awaiting transmission.

**Returns:** Queue reference for use with EnqueueBuffer() / DequeueBuffer().

#### `AcquirePB`

```c
APIPBPtr AcquirePB( void )
```

Acquire a zeroed APIPB from the pool.

**Returns:** Pointer to a clean APIPB, or NULL if the pool is exhausted.

#### `ReleasePB`

```c
void ReleasePB( APIPBPtr pb )
```

Release an APIPB and all attached Payload nodes back to their pools.

Calls ReleasePBMembers() first, then returns the PB. The caller must not access pb after this call.

| Parameter | Description |
|-----------|-------------|
| `pb` | APIPB to release; ignores NULL. |

#### `EnqueuePB`

```c
void EnqueuePB( APIPBQueueRef q, APIPBPtr pb )
```

Append an APIPB to the tail of q; notifies the API task if q is the input queue.

Notifies the API task via vTaskNotifyGiveFromISR() or xTaskNotifyGive() depending on whether the call originates from an ISR. This enqueue is the handoff from the CDC receive path to the request-dispatch loop.

| Parameter | Description |
|-----------|-------------|
| `q` | Target queue. |
| `pb` | APIPB to enqueue. |

#### `DequeuePB`

```c
APIPBPtr DequeuePB( APIPBQueueRef q )
```

Remove and return the APIPB at the head of q, or NULL if empty.

| Parameter | Description |
|-----------|-------------|
| `q` | Source queue. |

**Returns:** Oldest queued APIPB, or NULL.

#### `EnqueueBuffer`

```c
void EnqueueBuffer( APIBufferQueueRef q, APIBufferPtr b )
```

Append an APIBuffer chain to the tail of q; notifies the API task if q is the output queue.

If the target is the output queue, notifies the API task with bit 0x02 so that USBSendAll() is called promptly. Works from task or ISR context.

| Parameter | Description |
|-----------|-------------|
| `q` | Target queue. |
| `b` | Head of the APIBuffer chain to enqueue. |

#### `DequeueBuffer`

```c
APIBufferPtr DequeueBuffer( APIBufferQueueRef q )
```

Remove and return the APIBuffer at the head of q, or NULL if empty.

| Parameter | Description |
|-----------|-------------|
| `q` | Source queue. |

**Returns:** Oldest queued APIBuffer, or NULL.

#### `AcquirePayload`

```c
PayloadPtr AcquirePayload( void )
```

Acquire a Payload node from the pool.

**Returns:** Pointer to a Payload, or NULL if the pool is exhausted.

#### `ReleasePayload`

```c
void ReleasePayload( PayloadPtr p )
```

Return a Payload node to the pool after zeroing its data array.

| Parameter | Description |
|-----------|-------------|
| `p` | Payload to release; ignores NULL. |

#### `ReleasePBMembers`

```c
void ReleasePBMembers( APIPBPtr pb )
```

Release all Payload nodes attached to pb without returning the PB itself.

Walks the pb->payload chain and calls ReleasePayload() on each node. The PB remains valid and its payload pointer is set to NULL. Use this when a handler has finished with the request payload but the response PB is still live.

| Parameter | Description |
|-----------|-------------|
| `pb` | APIPB whose payload chain should be freed; ignores NULL. |

#### `AcquireBuffer`

```c
APIBufferPtr AcquireBuffer( void )
```

Acquire a transmit APIBuffer from the pool.

**Returns:** Pointer to a free APIBuffer, or NULL if the pool is exhausted.

#### `ReleaseBuffer`

```c
void ReleaseBuffer( APIBufferPtr b )
```

Return a transmit APIBuffer to the pool after zeroing its data and resetting length.

| Parameter | Description |
|-----------|-------------|
| `b` | Buffer to release; ignores NULL. |

#### `APICoreGetStats`

```c
APICoreStatsRef APICoreGetStats( void )
```

Refresh and return a snapshot of the current API engine statistics.

The returned pointer is valid until the next APICoreInit() call. The live-count fields (inputQueued, outputQueued, *MemUsed) are refreshed on each call; pool free-counts and peak marks are maintained incrementally by the pool helpers.

**Returns:** Read-only pointer to the internal APICoreStats; valid until next APICoreInit().

---

## APIHandlers

*API handler function declarations.*

### Functions

#### `HandlerOvenStatus`

```c
PayloadPtr HandlerOvenStatus( APIPBPtr pb )
```

Return current oven status.

Return current oven status.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with status JSON, or NULL.

#### `HandlerOvenRun`

```c
PayloadPtr HandlerOvenRun( APIPBPtr pb )
```

Start a reflow run with the profile named in pb->rawRequest.

Start a reflow run with the profile named in pb->rawRequest.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerOvenStop`

```c
PayloadPtr HandlerOvenStop( APIPBPtr pb )
```

Stop a running reflow cycle gracefully.

Stop a running reflow cycle gracefully.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerOvenEstop`

```c
PayloadPtr HandlerOvenEstop( APIPBPtr pb )
```

Immediately cut all power to heater and fan (emergency stop).

Immediately cut all power to heater and fan (emergency stop).

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerManualEnable`

```c
PayloadPtr HandlerManualEnable( APIPBPtr pb )
```

Enable manual override mode, allowing direct heater and fan control.

Enable manual override mode, allowing direct heater and fan control.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerManualDisable`

```c
PayloadPtr HandlerManualDisable( APIPBPtr pb )
```

Disable manual override mode and return to automatic control.

Disable manual override mode and return to automatic control.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerManualHeater`

```c
PayloadPtr HandlerManualHeater( APIPBPtr pb )
```

Set the heater drive level while in manual mode.

Set the heater drive level while in manual mode.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerManualFan`

```c
PayloadPtr HandlerManualFan( APIPBPtr pb )
```

Set the oven fan speed while in manual mode.

Set the oven fan speed while in manual mode.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerSensorsTemp`

```c
PayloadPtr HandlerSensorsTemp( APIPBPtr pb )
```

Return all current temperature sensor readings.

Return all current temperature sensor readings.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with sensor JSON, or NULL.

#### `HandlerSensorsMains`

```c
PayloadPtr HandlerSensorsMains( APIPBPtr pb )
```

Return current mains voltage and frequency readings.

Return current mains voltage and frequency readings.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with mains JSON, or NULL.

#### `HandlerProfilesList`

```c
PayloadPtr HandlerProfilesList( APIPBPtr pb )
```

Return a list of all stored reflow profiles.

Return a list of all stored reflow profiles.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with profile list JSON, or NULL.

#### `HandlerProfileGet`

```c
PayloadPtr HandlerProfileGet( APIPBPtr pb )
```

Return the reflow profile named in pb->rawRequest.

Return the reflow profile named in pb->rawRequest.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with profile JSON, or NULL.

#### `HandlerProfileCreate`

```c
PayloadPtr HandlerProfileCreate( APIPBPtr pb )
```

Create a new reflow profile with the name in pb->rawRequest.

Create a new reflow profile with the name in pb->rawRequest.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerProfileUpdate`

```c
PayloadPtr HandlerProfileUpdate( APIPBPtr pb )
```

Update an existing reflow profile named in pb->rawRequest.

Update an existing reflow profile named in pb->rawRequest.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerProfileDelete`

```c
PayloadPtr HandlerProfileDelete( APIPBPtr pb )
```

Delete the reflow profile named in pb->rawRequest.

Delete the reflow profile named in pb->rawRequest.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerConfigGet`

```c
PayloadPtr HandlerConfigGet( APIPBPtr pb )
```

Return the current system configuration.

Return the current system configuration.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with config JSON, or NULL.

#### `HandlerConfigPut`

```c
PayloadPtr HandlerConfigPut( APIPBPtr pb )
```

Apply a new system configuration from the request payload.

Apply a new system configuration from the request payload.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerLogsList`

```c
PayloadPtr HandlerLogsList( APIPBPtr pb )
```

Return a list of all stored log files.

Return a list of all stored log files.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with log list JSON, or NULL.

#### `HandlerLogGet`

```c
PayloadPtr HandlerLogGet( APIPBPtr pb )
```

Return the contents of the log file named in pb->rawRequest.

Return the contents of the log file named in pb->rawRequest.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with log data, or NULL.

#### `HandlerLogDelete`

```c
PayloadPtr HandlerLogDelete( APIPBPtr pb )
```

Delete the log file named in pb->rawRequest.

Delete the log file named in pb->rawRequest.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerLogsClear`

```c
PayloadPtr HandlerLogsClear( APIPBPtr pb )
```

Delete all stored log files.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerStorageGet`

```c
PayloadPtr HandlerStorageGet( APIPBPtr pb )
```

Return flash storage usage statistics.

Return flash storage usage statistics.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with storage JSON, or NULL.

#### `HandlerStorageFormat`

```c
PayloadPtr HandlerStorageFormat( APIPBPtr pb )
```

Erase and reformat the flash storage filesystem.

Erase and reformat the flash storage filesystem.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerSystemStatus`

```c
PayloadPtr HandlerSystemStatus( APIPBPtr pb )
```

Return system health and build information.

Return system health and build information.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with system JSON, or NULL.

#### `HandlerClockGet`

```c
PayloadPtr HandlerClockGet( APIPBPtr pb )
```

Return the current real-time clock value.

Return the current real-time clock value.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with clock JSON, or NULL.

#### `HandlerClockPut`

```c
PayloadPtr HandlerClockPut( APIPBPtr pb )
```

Set the real-time clock from the value in pb->rawRequest.

Set the real-time clock from the value in pb->rawRequest.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerSystemReset`

```c
PayloadPtr HandlerSystemReset( APIPBPtr pb )
```

Perform a software reset of the microcontroller.

Perform a software reset of the microcontroller.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerPowerGet`

```c
PayloadPtr HandlerPowerGet( APIPBPtr pb )
```

Return current USB-PD power contract details.

Return current USB-PD power contract details.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with power JSON, or NULL.

#### `HandlerUiLight`

```c
PayloadPtr HandlerUiLight( APIPBPtr pb )
```

Set the oven light state from pb->rawRequest.

Set the oven light state from pb->rawRequest.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerUiBuzzer`

```c
PayloadPtr HandlerUiBuzzer( APIPBPtr pb )
```

Trigger a buzzer pattern identified by pb->rawRequest.

Trigger a buzzer pattern identified by pb->rawRequest.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

---

## APIRoutes

*API route table — request code enum, route struct, and route table definition.*

### Types

#### `APIRequestCode`

Logical request codes — one per unique API endpoint.

| Value | Description |
|-------|-------------|
| `API_REQ_OVEN_STATUS` | GET /oven/status. |
| `API_REQ_OVEN_RUN` | PUT /oven/run. |
| `API_REQ_OVEN_STOP` | PUT /oven/stop. |
| `API_REQ_OVEN_ESTOP` | PUT /oven/estop. |
| `API_REQ_MANUAL_ENABLE` | PUT /oven/manual/enable. |
| `API_REQ_MANUAL_DISABLE` | PUT /oven/manual/disable. |
| `API_REQ_MANUAL_HEATER` | PUT /oven/manual/heater. |
| `API_REQ_MANUAL_FAN` | PUT /oven/manual/fan. |
| `API_REQ_SENSORS_TEMP` | GET /sensors/temperature. |
| `API_REQ_SENSORS_MAINS` | GET /sensors/mains. |
| `API_REQ_PROFILE_LIST` | GET /profiles. |
| `API_REQ_PROFILE_GET` | GET /profiles/s. |
| `API_REQ_PROFILE_CREATE` | POST /profiles/s. |
| `API_REQ_PROFILE_UPDATE` | PUT /profiles/s. |
| `API_REQ_PROFILE_DELETE` | DELETE /profiles/s. |
| `API_REQ_CONFIG_GET` | GET /config. |
| `API_REQ_CONFIG_PUT` | PUT /config. |
| `API_REQ_LOGS_LIST` | GET /logs. |
| `API_REQ_LOG_GET` | GET /logs/s. |
| `API_REQ_LOG_DELETE` | DELETE /logs/s. |
| `API_REQ_LOGS_CLEAR` | DELETE /logs. |
| `API_REQ_STORAGE_GET` | GET /storage. |
| `API_REQ_STORAGE_FORMAT` | PUT /storage/format. |
| `API_REQ_SYSTEM_STATUS` | GET /system/status. |
| `API_REQ_CLOCK_GET` | GET /system/clock. |
| `API_REQ_CLOCK_PUT` | PUT /system/clock. |
| `API_REQ_SYSTEM_RESET` | PUT /system/reset. |
| `API_REQ_POWER_GET` | GET /power. |
| `API_REQ_UI_LIGHT` | PUT /ui/light. |
| `API_REQ_UI_BUZZER` | PUT /ui/buzzer. |

#### `APIRoute`

A single entry in the API route table.

| Type | Field | Description |
|------|-------|-------------|
| `const char *` | `pattern` | sscanf-compatible pattern, e.g. "GET /profiles/%s". |
| `APIRequestCode` | `reqCode` | Logical request code for the matched route. |
| `APIHandler` | `handler` | Direct handler function to call on dispatch. |

---

## APITask

*USB REST API task — public interface for the API task loop.*

### Functions

#### `APITaskInit`

```c
void APITaskInit( void )
```

Initialise the API core and buffer stream subsystems.

Waits for FlagSystemInitialised in SystemStatusFlagsHandle before proceeding. Called once by app_freertos.c at startup.

Initialise the API core and buffer stream subsystems.

Blocks on FlagSystemInitialised to ensure all drivers are ready before accepting USB requests. Initialises the API core pool and stream parser. Called once by app_freertos.c at startup.

#### `APITaskLoop`

```c
void APITaskLoop( void )
```

Main execution body of the API task loop.

Blocks on xTaskNotifyWait(), processes pending requests via the route table, queues serialised responses, and drains the USB transmit queue. Called repeatedly by app_freertos.c in the task's infinite loop.

Main execution body of the API task loop.

Blocks on xTaskNotifyWait() until woken by a USB receive, TX complete, or APICore request-enqueue notification. Drains the input queue, dispatches each request to its route handler, and calls USBSendAll() to flush any pending output.

Called repeatedly by app_freertos.c in the task's infinite loop.

#### `USBTxDoneHandler`

```c
void USBTxDoneHandler( void )
```

USB transmission complete callback — advances the transmit pipeline.

Called directly from the USB CDC ISR (usbd_cdc_if.c). Releases the completed buffer, starts the next link in the chain if present, or wakes the API task via task notification to re-check the output queue.

Releases the completed buffer. If a next link is available in the chain, starts it immediately via CDC_Transmit_FS(). Otherwise resets currentUSBBuffer to NULL and wakes the API task so it can dequeue the next buffer.

---

## APITypes

*Core API type definitions shared across the codec, core, and handler layers.*

### Types

#### ``

API pool and buffer sizing constants.

| Value | Description |
|-------|-------------|
| `API_PAYLOAD_SIZE` | Maximum bytes in a single Payload data chunk. |
| `API_PAYLOAD_COUNT` | Total Payload objects in the static pool. |
| `APIPB_COUNT` | Total APIPB protocol buffers in the static pool. |
| `API_BUFFER_SIZE` | Maximum bytes in a single APIBuffer transmit chunk. |
| `API_BUFFER_COUNT` | Total APIBuffer objects in the static pool. |
| `API_REQUEST_MAX_LEN` | Maximum bytes captured into APIPB.rawRequest. |

#### `APIStatus`

HTTP-like response status codes returned by handler functions.

| Value | Description |
|-------|-------------|
| `API_STATUS_OK` | Request succeeded. |
| `API_STATUS_CREATED` | Resource created successfully. |
| `API_STATUS_NO_CONTENT` | Request succeeded; no body to return. |
| `API_STATUS_BAD_REQUEST` | Malformed request or invalid parameters. |
| `API_STATUS_NOT_FOUND` | No route matched the incoming request. |
| `API_STATUS_CONFLICT` | Request conflicts with current state. |
| `API_STATUS_INTERNAL_ERROR` | Handler encountered an internal error. |

#### `APIMode`

Request origin mode, used to select the serialisation format on output.

| Value | Description |
|-------|-------------|
| `API_MODE_UNDETERMINED` | Mode has not yet been determined by the parser. |
| `API_MODE_API` | Request arrived as a REST JSON command — serialise as JSON. |
| `API_MODE_CLI` | Request arrived as a CLI text command — serialise as a human-readable prompt. |

#### `APIMethod`

HTTP verb parsed from the incoming request line.

| Value | Description |
|-------|-------------|
| `API_METHOD_UNKNOWN` | No verb recognised. |
| `API_METHOD_GET` | HTTP GET. |
| `API_METHOD_POST` | HTTP POST. |
| `API_METHOD_PUT` | HTTP PUT. |
| `API_METHOD_DELETE` | HTTP DELETE. |

#### `TerminatorType`

Line-ending style detected by the stream parser.

| Value | Description |
|-------|-------------|
| `TypeNull` | No terminator detected (sentinel; unused after a completed parse). |
| `TypeLF` | Bare LF (0x0A). |
| `TypeCR` | Bare CR (0x0D). |
| `TypeCRLF` | CR+LF pair (0x0D 0x0A). |
| `TypeZero` | Null byte (0x00); response is also null-terminated. |

#### `Payload`

Singly-linked data chunk for building multi-part response bodies.

| Type | Field | Description |
|------|-------|-------------|
| `struct Payload *` | `next` | Next chunk in the chain, or NULL if this is the last. |
| `char` | `data` | Raw data bytes for this chunk. |

#### `APIPB`

Protocol buffer — the unit of work flowing through the API pipeline.

| Type | Field | Description |
|------|-------|-------------|
| `struct APIPB *` | `next` | Queue linkage — must remain first. |
| `APIStatus` | `status` | Response status code written by the handler. |
| `uint8_t` | `rawRequest` | Argument bytes captured after the % wildcard in the route pattern. |
| `const struct APIRoute *` | `route` | Matched route entry, or NULL if not found. |
| `PayloadPtr` | `payload` | Head of the response body payload chain. |
| `APIMode` | `origin` | Request origin (API JSON or CLI text). |
| `TerminatorType` | `terminator` | Line-ending style from the incoming request. |

#### `APIBuffer`

Fixed-size outbound transmit buffer.

| Type | Field | Description |
|------|-------|-------------|
| `struct APIBuffer *` | `next` | Queue linkage — must be FIRST. |
| `char` | `data` | Serialised response bytes. |
| `size_t` | `length` | Number of valid bytes in data. |

---

## BlockDevice

*Abstract block device interface for the FileSystem subsystem.*

---

## Buzzer

*Piezo buzzer driver with melodic sequencer.*

---

## DCFan

*EMC2101 DC fan controller driver.*

---

## DeviceTask

*Device task — periodic driver process loop.*

### Functions

#### `DeviceTaskInit`

```c
void DeviceTaskInit( void )
```

Initialise the Device task — waits for system initialisation signal.

Blocks on FlagSystemInitialised in SystemStatusFlagsHandle before returning. Called once by app_freertos.c at startup.

Initialise the Device task — waits for system initialisation signal.

Blocks indefinitely on FlagSystemInitialised so that no driver Process() functions run until all drivers have been initialised by ManagerTask.

#### `DeviceTaskLoop`

```c
void DeviceTaskLoop( void )
```

Main execution body of the Device task loop.

Calls the Process() function for every driver module in sequence. Called repeatedly by app_freertos.c in the task's infinite loop.

Main execution body of the Device task loop.

Each Process() function performs all hardware I/O, updates cached state, and sets/clears status and fault flags for its respective module. No hardware access occurs outside of these calls in this task.

---

## FSFile

*File and directory operations for the FileSystem subsystem.*

---

## FSInternal

*Package-private declarations shared within the FileSystem module.*

---

## FSTypes

*Shared types, error codes, and callback signature for the FileSystem subsystem.*

---

## Features

*Compile-time feature selection for the VesuviOven firmware.*

---

## Firmware

*Product identity and firmware version constants.*

---

## Flash

*External NOR flash driver public interface — MX25L51245GZ2I-10G.*

---

## FlashBlockDevice

*NOR flash concrete block device adapter for the FileSystem subsystem.*

---

## I2CAddress

*I2C device address registry.*

### Types

#### `I2CAddress`

7-bit I2C addresses for every device on the shared I2C bus (PA9/PA10).

| Value | Description |
|-------|-------------|
| `I2CAddrSTPD01` | STPD01PUR — USB-PD power supply controller (ADD=GND) |
| `I2CAddrAS5600` | AS5600 — magnetic encoder / fan tachometer (CN4, fixed) |
| `I2CAddrMCP3221` | MCP3221A2T — 12-bit I2C ADC for NTC thermistor (A2 variant) |
| `I2CAddrEMC2101` | EMC2101 — DC fan controller / inlet-temp sensor (fixed) |
| `I2CAddrTCPP03` | TCPP03-M20 — USB-C port protection controller (I2C_ADD=GND) |

---

## I2CManager

*I2C bus manager with asynchronous and synchronous transfer APIs.*

### Types

#### `I2CID`

Logical identifiers for I2C bus instances managed by this driver.

| Value | Description |
|-------|-------------|
| `I2CBus1` | I2C1 — fan, encoders, thermistors, USB-PD companions. |
| `I2CBusCount` |  |

#### `I2CStatusBit`

Status and diagnostic flag bit positions for an I2C bus instance. These map 1:1 to the bits in the per-instance statusHandle event flag group.

| Value | Description |
|-------|-------------|
| `FlagI2CStatusReady` | Bus initialised and ready for transfers. |
| `FlagI2CStatusBusError` | HAL reported a bus error (BERR or NACK) |
| `FlagI2CStatusArbitrationLost` | Multi-master arbitration lost (ARLO) |
| `FlagI2CStatusTimeout` | Transfer exceeded the caller-supplied timeout. |
| `FlagI2CStatusLocked` | SDA held low by a peripheral (bus locked) |
| `I2CFlagsCount` |  |

### Functions

#### `I2CInitModule`

```c
void I2CInitModule( void )
```

Initialise all I2C bus instances, bind hardware handles, and register HAL callbacks.

#### `I2COpen`

```c
I2CRef I2COpen( I2CID id )
```

Return a handle to a specific I2C bus instance.

| Parameter | Description |
|-----------|-------------|
| `id` | Bus identifier. |

**Returns:** Handle to the instance, or NULL if id is out of range.

#### `I2CGetStatus`

```c
uint32_t I2CGetStatus( I2CRef i2c )
```

Return the full status bitmask for a specific I2C bus instance.

| Parameter | Description |
|-----------|-------------|
| `i2c` | Handle returned by I2COpen(). |

**Returns:** Bitmask of I2CStatusBit flags; 0 if i2c is NULL.

#### `I2CReadAsync`

```c
HAL_StatusTypeDef I2CReadAsync( I2CRef i2c, uint16_t devAddr, uint16_t memAddr, uint16_t size, uint8_t * pData, uint16_t len, I2CCallback cb )
```

Start an asynchronous interrupt-driven memory read.

| Parameter | Description |
|-----------|-------------|
| `i2c` | Handle returned by I2COpen(). |
| `devAddr` | 7-bit device address shifted left by 1. |
| `memAddr` | Register or memory address to read from. |
| `size` | Memory address size (I2C_MEMADD_SIZE_8BIT or _16BIT). |
| `pData` | Destination buffer; must remain valid until cb fires. |
| `len` | Number of bytes to read. |
| `cb` | Completion callback; called from ISR context. |

**Returns:** HAL_OK if the transfer was queued, HAL_BUSY if the bus is unavailable.

#### `I2CReadSync`

```c
HAL_StatusTypeDef I2CReadSync( I2CRef i2c, uint16_t devAddr, uint16_t memAddr, uint16_t size, uint8_t * pData, uint16_t len, uint32_t timeout )
```

Perform a synchronous blocking memory read.

| Parameter | Description |
|-----------|-------------|
| `i2c` | Handle returned by I2COpen(). |
| `devAddr` | 7-bit device address shifted left by 1. |
| `memAddr` | Register or memory address to read from. |
| `size` | Memory address size (I2C_MEMADD_SIZE_8BIT or _16BIT). |
| `pData` | Destination buffer. |
| `len` | Number of bytes to read. |
| `timeout` | Maximum wait time in milliseconds for the bus semaphore and transfer. |

**Returns:** HAL_OK on success, HAL_BUSY if semaphore timed out, HAL_ERROR on bus fault.

#### `I2CReceiveAsync`

```c
HAL_StatusTypeDef I2CReceiveAsync( I2CRef i2c, uint16_t devAddr, uint8_t * pData, uint16_t len, I2CCallback cb )
```

Start an asynchronous interrupt-driven master receive (no register address).

| Parameter | Description |
|-----------|-------------|
| `i2c` | Handle returned by I2COpen(). |
| `devAddr` | 7-bit device address shifted left by 1. |
| `pData` | Destination buffer; must remain valid until cb fires. |
| `len` | Number of bytes to read. |
| `cb` | Completion callback; called from ISR context. |

**Returns:** HAL_OK if queued, HAL_BUSY if bus is in use.

#### `I2CReceiveSync`

```c
HAL_StatusTypeDef I2CReceiveSync( I2CRef i2c, uint16_t devAddr, uint8_t * pData, uint16_t len, uint32_t timeout )
```

Perform a synchronous blocking master receive (no register address).

| Parameter | Description |
|-----------|-------------|
| `i2c` | Handle returned by I2COpen(). |
| `devAddr` | 7-bit device address shifted left by 1. |
| `pData` | Destination buffer. |
| `len` | Number of bytes to read. |
| `timeout` | Maximum wait time in milliseconds for the bus semaphore and transfer. |

**Returns:** HAL_OK on success, HAL_BUSY if semaphore timed out, HAL_ERROR on bus fault.

#### `I2CWriteSync`

```c
HAL_StatusTypeDef I2CWriteSync( I2CRef i2c, uint16_t devAddr, uint16_t memAddr, uint16_t size, uint8_t * pData, uint16_t len, uint32_t timeout )
```

Perform a synchronous blocking memory write.

| Parameter | Description |
|-----------|-------------|
| `i2c` | Handle returned by I2COpen(). |
| `devAddr` | 7-bit device address shifted left by 1. |
| `memAddr` | Register or memory address to write to. |
| `size` | Memory address size (I2C_MEMADD_SIZE_8BIT or _16BIT). |
| `pData` | Source buffer. |
| `len` | Number of bytes to write. |
| `timeout` | Maximum wait time in milliseconds for the bus semaphore and transfer. |

**Returns:** HAL_OK on success, HAL_BUSY if semaphore timed out, HAL_ERROR on bus fault.

---

## LoggingTask

*Logging task — telemetry and event log persistence.*

### Functions

#### `LoggingTaskInit`

```c
void LoggingTaskInit( void )
```

Initialise the Logging task — waits for system initialisation signal.

Blocks on FlagSystemInitialised in SystemStatusFlagsHandle before returning. Called once by app_freertos.c at startup.

Initialise the Logging task — waits for system initialisation signal.

#### `LoggingTaskLoop`

```c
void LoggingTaskLoop( void )
```

Main execution body of the Logging task loop.

Currently a placeholder — implementation pending file manager driver.

Main execution body of the Logging task loop.

---

## MCU

*STM32G0 MCU peripheral driver (internal ADC, RTC, power monitoring, CRC).*

### Types

#### `MCUID`

Logical identifiers for MCU instances managed by this driver.

| Value | Description |
|-------|-------------|
| `MCU0` | STM32G0 on-chip peripherals. |
| `MCUCount` |  |

#### `MCUStatusBit`

Status and diagnostic flag bit positions for the MCU module. Flags for disabled features are defined but never set.

| Value | Description |
|-------|-------------|
| `FlagMCUStatusReady` | MCU peripherals initialised. |
| `FlagMCUStatusLowBattery` | Battery level below 15% — FEATURE_BATTERY_MONITOR. |
| `FlagMCUStatusCriticalBattery` | Battery level below 5% — FEATURE_BATTERY_MONITOR. |
| `FlagMCUStatusOverTemp` | Junction temperature above 80°C — FEATURE_MCU_TEMP_MONITOR. |
| `FlagMCUStatusVoltageUnstable` | VCC outside 3.0–3.6 V — FEATURE_VCC_MONITOR. |
| `FlagMCUStatusRtcInvalid` | RTC set or read failed — FEATURE_RTC. |
| `FlagMCUStatusAdcError` | ADC DMA peripheral error. |
| `FlagMCUStatusCrcBusy` | CRC engine acquired; computation in progress. |
| `FlagMCUStatusTimePending` | RTC time write queued — FEATURE_RTC. |
| `MCUFlagsCount` |  |

### Functions

#### `MCUInitModule`

```c
void MCUInitModule( void )
```

Allocate per-instance resources. Does not access hardware.

#### `MCUOpen`

```c
MCURef MCUOpen( MCUID id )
```

Return a handle to the specified MCU instance.

#### `MCUGetStatus`

```c
uint32_t MCUGetStatus( MCURef mcu )
```

Return the full status bitmask from the private instance status flags.

#### `MCUProcess`

```c
void MCUProcess( void )
```

Refresh cached ADC readings, apply pending RTC writes, and update status flags.

#### `CRCInit`

```c
void CRCInit( MCURef mcu )
```

Acquire the CRC engine and reset the accumulator. Blocks if already in use.

Acquire the CRC engine and reset the accumulator. Blocks if already in use.

#### `CRCUpdate`

```c
void CRCUpdate( MCURef mcu, const uint8_t * data, size_t length )
```

Feed a byte buffer into the running CRC-32 accumulator.

#### `CRCResult`

```c
uint32_t CRCResult( MCURef mcu )
```

Return the final CRC-32 value and release the CRC engine.

#### `CRCCompute`

```c
uint32_t CRCCompute( MCURef mcu, const uint8_t * data, size_t length )
```

Compute CRC-32 over a contiguous buffer. Acquires and releases the engine internally.

Compute CRC-32 over a contiguous buffer. Acquires and releases the engine internally.

#### `CRCVerify`

```c
bool CRCVerify( MCURef mcu, const uint8_t * data, size_t length, uint32_t expected )
```

Compute CRC-32 and compare with an expected value.

Compute CRC-32 and compare with an expected value.

---

## ManagerTask

*Manager task — system supervisor and fault monitor.*

### Functions

#### `ManagerTaskInit`

```c
void ManagerTaskInit( void )
```

Initialise all hardware driver modules and signal system readiness.

Calls XxxInitModule() for every driver in dependency order. Waits for DEVICE_ALL_READY before enabling interrupts and setting FlagSystemInitialised. Called once by app_freertos.c at startup.

Initialise all hardware driver modules and signal system readiness.

Phase 1 — bus managers: create SPI and I2C semaphores. Must precede any driver that calls SPIOpen() or I2COpen() internally. Phase 2 — alloc-only InitModule() calls: create per-driver RTOS handles and assign compile-time GPIO/pin mappings. No hardware I/O at this stage. Phase 3 — wait for DEVICE_ALL_READY (set by device Open() calls in other tasks), enable GPIO interrupts, and broadcast FlagSystemInitialised.

#### `ManagerTaskLoop`

```c
void ManagerTaskLoop( void )
```

Main execution body of the Manager task loop — fault supervisor.

Blocks indefinitely on FaultFlagsHandle (any fault bit). The notification value is captured but not yet acted upon; fault handling policy is TBD. Called repeatedly by app_freertos.c in the task's infinite loop.

Main execution body of the Manager task loop — fault supervisor.

Currently captures the fault bitmask but does not implement a reaction policy. Fault handling (safe-state enforcement, buzzer alert, etc.) is TBD.

---

## OvenController

*Oven temperature regulator.*

### Types

#### `OvenControllerID`

Logical identifiers for oven controller instances.

| Value | Description |
|-------|-------------|
| `OvenController1` | Primary oven controller instance. |
| `OvenControllerCount` |  |

#### `OvenState`

Regulation state reported by the controller in OvenControlPB.state.

| Value | Description |
|-------|-------------|
| `OvenStateIdle` | Controller is not running. |
| `OvenStateHeating` | Driving heaters toward target. |
| `OvenStateCooling` | Target is below current temperature; waiting to cool. |
| `OvenStateAtTemp` | Measured temperature within tolerance of target. |
| `OvenStateCount` |  |

#### `OvenControllerStatusBit`

Status and diagnostic flag bit positions for the oven controller instance.

| Value | Description |
|-------|-------------|
| `FlagOvenControllerStatusReady` | Internal devices resolved; regulation may run. |
| `FlagOvenControllerStatusActive` | Regulation loop is running. |
| `FlagOvenControllerStatusAtTemp` | Measured temperature within tolerance of target. |
| `FlagOvenControllerStatusOverTemp` | Measured temperature exceeded safe maximum. |
| `FlagOvenControllerStatusFault` | Required sensor or actuator has faulted. |
| `OvenControllerFlagsCount` |  |

#### `OvenControlPB`

Shared control and status block for an oven regulation run.

| Type | Field | Description |
|------|-------|-------------|
| `Temperature` | `targetTemp` | Desired cavity temperature in milli-degrees C. |
| `Temperature` | `tolerance` | Acceptable deviation from target in milli-degrees C. |
| `Temperature` | `rampRate` | Maximum rate of rise in milli-degrees C per second; 0 = unlimited. |
| `uint8_t` | `heaterTop` | Permit the top heater. |
| `uint8_t` | `heaterRear` | Permit the rear convection element. |
| `uint8_t` | `heaterBottom` | Permit the bottom heater. |
| `uint8_t` | `__pad0__` | Reserved — must be zero. |
| `uint8_t` | `heaters` |  |
| `union OvenControlPB` | `` |  |
| `osEventFlagsId_t` | `statusHandle` |  |
| `Temperature` | `currentTemp` | Averaged cavity temperature from active sources, in milli-degrees C. |
| `OvenState` | `state` | Current regulation state. |

### Functions

#### `OCInitModule`

```c
void OCInitModule( void )
```

Allocate per-instance resources. Does not access hardware.

#### `OCOpen`

```c
OvenControllerRef OCOpen( OvenControllerID id )
```

Open a handle to a specific oven controller instance.

Resolves all internal TRIAC and sensor handles. Sets FlagOvenControllerStatusReady and signals DeviceStatusFlagsHandle on success.

| Parameter | Description |
|-----------|-------------|
| `id` | Controller instance identifier. |

**Returns:** Handle to the instance, or NULL if id is out of range.

#### `OCStart`

```c
void OCStart( OvenControllerRef controller, OvenControlPBPtr pb )
```

Start the regulation loop using the supplied parameter block.

The controller holds a pointer to pb for the duration of the run, reading mandate fields and writing currentTemp and state each tick. The caller must not free or invalidate pb while the controller is running.

| Parameter | Description |
|-----------|-------------|
| `controller` | Handle returned by OCOpen(). |
| `pb` | Parameter block owned by the caller. |

#### `OCStop`

```c
void OCStop( OvenControllerRef controller )
```

Stop the regulation loop and de-energise all heating elements.

| Parameter | Description |
|-----------|-------------|
| `controller` | Handle returned by OCOpen(). |

#### `OCGetStatus`

```c
uint32_t OCGetStatus( OvenControllerRef controller )
```

Return the full status bitmask for this controller instance.

| Parameter | Description |
|-----------|-------------|
| `controller` | Handle returned by OCOpen(). |

**Returns:** Bitmask of OvenControllerStatusBit flags; BIT(FlagOvenControllerStatusFault) if NULL.

#### `OCProcess`

```c
void OCProcess( void )
```

Run the regulation loop — read sensors, compute output, drive actuators.

Skip if FlagOvenControllerStatusActive is clear.
Grab the PB pointer atomically; skip if NULL.
Advance rampTarget toward pb->targetTemp at most pb->rampRate milli-degrees/second.
Sample temperature; if over-temp threshold, de-energise and latch fault.
Check TRIAC fault; if present, de-energise and latch fault.
Clear any prior fault on a clean tick.
Apply control law:
Above target by more than tolerance → TriacOff (cooling, wait).
Below rampTarget → TriacOn full power (tracking the ramp).
Within ramp, below target → proportional burst-fire: power scales linearly from 1000 permille at one tolerance below target to 0 at target.


Write currentTemp and state back into the PB.

---

## Partition

*Partition table management for the FileSystem subsystem.*

---

## Platform

*Platform abstraction header — pulls in the STM32G0 HAL.*

---

## PowerManager

*AC mains power management and E-Stop safety supervisor.*

### Types

#### `PMStatusBit`

Granular status and fault bit positions for the Power Manager. These map 1:1 to the bits in the private pmStatus shadow word, returned by PMGetStatus().

| Value | Description |
|-------|-------------|
| `FlagPMStatusReady` | Power manager initialised and running. |
| `FlagPMEStopTripped` | E-Stop loop is open (hardware detected) |
| `FlagPMMainsPower` | AC mains input detected (vs USB-only supply) |
| `FlagPMSwitchedACLive` | ZCD confirms switched AC is present at the output. |
| `FlagPMHotSideEnabled` | Hot-side relay command is active (GPIO driven low) |
| `FlagPMAuxPowerEnabled` | Aux 24 V rail is active. |
| `FlagPMHotSideBlocked` | E-Stop is preventing the hot side from turning on. |
| `FlagPMHotSideRogue` | AC live detected but hot side should be off (safety fault) |
| `FlagPMHotSideDead` | Hot side commanded on but AC not detected (relay fault) |
| `PMFlagsCount` |  |

### Functions

#### `PMInitModule`

```c
void PMInitModule( void )
```

Initialise the Power Manager, sample GPIO state, and disable both output rails.

Initialise the Power Manager, sample GPIO state, and disable both output rails.

Reads the MAINS_PWR_N and ESTOP pins to populate the initial shadow state, then drives both output rails to their safe (off) state. Signals PMReady in DeviceStatusFlagsHandle.

#### `PMEnableHotSide`

```c
bool PMEnableHotSide( void )
```

Enable the hot-side relay if mains power is present and E-Stop is clear.

**Returns:** true if the relay was energised, false if blocked by E-Stop or no mains.

#### `PMDisableHotSide`

```c
bool PMDisableHotSide( void )
```

Disable the hot-side relay unconditionally.

**Returns:** Always true.

#### `PMEnableAuxPower`

```c
bool PMEnableAuxPower( void )
```

Enable the auxiliary 24 V power rail.

**Returns:** Always true.

#### `PMDisableAuxPower`

```c
bool PMDisableAuxPower( void )
```

Disable the auxiliary 24 V power rail.

**Returns:** Always true.

#### `PMGetStatus`

```c
uint32_t PMGetStatus( void )
```

Return the current power manager status bitmask.

**Returns:** Bitmask of PMStatusBit flags; safe to call from any task context.

#### `PMProcess`

```c
void PMProcess( void )
```

Reconcile commanded and observed power state and update fault flags.

#### `PMHandleZCDInterrupt`

```c
void PMHandleZCDInterrupt( uint16_t GPIO_Pin )
```

ZCD rising-edge ISR handler — records the tick and marks AC as live.

| Parameter | Description |
|-----------|-------------|
| `GPIO_Pin` | The HAL pin mask (unused; only one ZCD pin exists). |

#### `PMHandleEStopInterrupt`

```c
void PMHandleEStopInterrupt( uint16_t GPIO_Pin )
```

E-Stop rising-edge ISR handler — immediately de-energises the hot-side relay.

| Parameter | Description |
|-----------|-------------|
| `GPIO_Pin` | The HAL pin mask (unused; only one E-Stop pin exists). |

---

## Reflow

*Reflow profile engine — public API.*

### Types

#### `ReflowFlagBit`

Reflow engine status flags, stored in ReflowStatusFlagsHandle.

| Value | Description |
|-------|-------------|
| `FlagReflowReady` | Engine opened and ready to accept a profile. |
| `FlagReflowInProgress` | A reflow cycle is actively running. |
| `FlagReflowPaused` | Cycle suspended; oven holding at current stage target. |
| `FlagReflowPreheating` | Currently in the preheat ramp stage. |
| `FlagReflowSoaking` | Currently in the thermal soak stage. |
| `FlagReflowReflowing` | Currently in the reflow (liquidus) stage. |
| `FlagReflowCooling` | Currently in the cooldown stage. |
| `FlagReflowDone` | Cycle completed successfully. |
| `FlagReflowAborted` | Cycle was stopped before completion. |
| `FlagReflowStatusFault` | Fault during execution — also propagated to FaultFlagsHandle. |
| `ReflowFlagsCount` | Number of flags — must stay <= 24. |

### Functions

#### `ReflowOpen`

```c
ReflowRef ReflowOpen( void )
```

Open the reflow engine and acquire its OvenController reference.

Creates ReflowStatusFlagsHandle and initialises the engine state. Must be called once before any other Reflow function.

Idempotent — subsequent calls return the existing handle without re-initialising. Sets FlagReflowReady and FlagReflowEngineReady in DeviceStatusFlagsHandle.

**Returns:** Handle to the engine instance, or NULL on failure.

#### `ReflowStart`

```c
bool ReflowStart( ReflowRef ref, const char * name )
```

Load a profile by name and begin execution.

Resolves name via the ReflowProfile module. If the named file is not found on the filesystem, the hardcoded default profile is used. Resets all status flags and starts the stage state machine from stage 0.

If name is NULL or the file is not found on the filesystem, the hardcoded default profile is used. Resets all transient status flags and calls OCStart().

| Parameter | Description |
|-----------|-------------|
| `ref` | Engine handle returned by ReflowOpen(). |
| `name` | Profile filename or full path. Pass NULL to use the default. |

**Returns:** True if the profile was accepted and execution has started.

#### `ReflowAbort`

```c
void ReflowAbort( ReflowRef ref )
```

Abort the active cycle and return the oven to idle.

Sets FlagReflowAborted, calls OCStop(), and clears FlagReflowInProgress. Safe to call at any point, including when no cycle is running.

| Parameter | Description |
|-----------|-------------|
| `ref` | Engine handle returned by ReflowOpen(). |

#### `ReflowPause`

```c
void ReflowPause( ReflowRef ref )
```

Suspend the active cycle and hold at the current stage target temperature.

Sets FlagReflowPaused. The stage timer is frozen and the OvenController continues to regulate at the current target. Has no effect if no cycle is running or the cycle is already paused.

| Parameter | Description |
|-----------|-------------|
| `ref` | Engine handle returned by ReflowOpen(). |

#### `ReflowResume`

```c
void ReflowResume( ReflowRef ref )
```

Resume a paused cycle from the point at which it was suspended.

Clears FlagReflowPaused and restarts the stage timer. Has no effect if the cycle is not paused.

| Parameter | Description |
|-----------|-------------|
| `ref` | Engine handle returned by ReflowOpen(). |

#### `ReflowProcess`

```c
void ReflowProcess( ReflowRef ref )
```

Execute one iteration of the reflow state machine.

Called from ReflowTaskLoop() in a continuous loop. When a cycle is active this function may block on osDelay() during timed holds. When idle it blocks on FlagReflowInProgress to yield the CPU.

When idle, blocks on FlagReflowInProgress to yield the CPU. When running, checks hold conditions, updates fan speed, and advances stages. Delays 200 ms per tick so the OC regulation loop (in DeviceTask) runs freely.

| Parameter | Description |
|-----------|-------------|
| `ref` | Engine handle returned by ReflowOpen(). |

#### `ReflowGetStatus`

```c
uint32_t ReflowGetStatus( ReflowRef ref )
```

Return the current status flags as a snapshot bitmask.

| Parameter | Description |
|-----------|-------------|
| `ref` | Engine handle returned by ReflowOpen(). |

**Returns:** Bitmask of ReflowFlagBit values currently set.

#### `ReflowGetStageIndex`

```c
uint8_t ReflowGetStageIndex( ReflowRef ref )
```

Return the zero-based index of the stage currently executing.

Valid only while FlagReflowInProgress is set. Returns 0 when idle. Combined with the profile name this identifies the exact position in a multi-segment cycle.

| Parameter | Description |
|-----------|-------------|
| `ref` | Engine handle returned by ReflowOpen(). |

**Returns:** Current stage index (0 to stageCount − 1).

---

## ReflowProfile

*Reflow profile data structures and filesystem I/O.*

### Types

#### ``

Maximum number of stages in a reflow profile.

| Value | Description |
|-------|-------------|
| `kReflowMaxStages` |  |

#### `ReflowStageType`

Classification of a reflow profile stage.

| Value | Description |
|-------|-------------|
| `ReflowStagePreheat` | Controlled ramp to soak temperature. |
| `ReflowStageSoak` | Thermal equalisation hold. |
| `ReflowStageReflow` | Ramp to peak; hold above liquidus. |
| `ReflowStageCool` | Controlled or passive cooldown. |

#### `ReflowHoldCriterion`

Criterion that must be satisfied before advancing to the next stage.

| Value | Description |
|-------|-------------|
| `ReflowHoldTime` | Hold for a fixed duration once target temperature is reached. |
| `ReflowHoldTemperature` | Hold until temperature crosses a threshold. |

#### `ReflowStage`

A single stage within a reflow profile.

| Type | Field | Description |
|------|-------|-------------|
| `ReflowStageType` | `type` | Stage classification — used for display and fault context. |
| `int16_t` | `targetTempC` | Target temperature in °C. |
| `float` | `rampRateC` | Ramp rate in °C/s. Positive = heat, negative = cool. |
| `ReflowHoldCriterion` | `holdCriterion` | What triggers the advance to the next stage. |
| `uint32_t` | `holdMs` | Hold duration (ReflowHoldTime): milliseconds at target. |
| `int16_t` | `holdTempC` | Hold threshold (ReflowHoldTemperature): advance when temp crosses this. |
| `union ReflowStage` | `` |  |
| `Permille` | `fanStart` | Oven fan speed at stage entry (0–1000). |
| `Permille` | `fanEnd` | Oven fan speed at stage exit. Equal to fanStart for fixed speed. |
| `uint8_t` | `heaterTop` | Enable top heating element. |
| `uint8_t` | `heaterRear` | Enable rear convection element. |
| `uint8_t` | `heaterBottom` | Enable bottom heating element. |
| `uint8_t` | `__pad0__` | Reserved — must be zero. |
| `uint8_t` | `heaters` |  |
| `union ReflowStage` | `` |  |

#### `ReflowProfile`

A complete reflow profile — a named sequence of stages.

| Type | Field | Description |
|------|-------|-------------|
| `char` | `name` | Human-readable profile name. |
| `uint8_t` | `stageCount` | Number of valid entries in stages. |
| `ReflowStage` | `stages` | Stage definitions, in execution order. |

### Functions

#### `ReflowProfileGetDefault`

```c
const ReflowProfile * ReflowProfileGetDefault( void )
```

Return a pointer to the hardcoded default lead-free reflow profile.

The returned pointer is valid for the lifetime of the program.

Return a pointer to the hardcoded default lead-free reflow profile.

The returned pointer is valid for the lifetime of the program.

#### `ReflowProfileLoad`

```c
bool ReflowProfileLoad( const char * name, ReflowProfilePtr out )
```

Load a profile from the filesystem into a caller-supplied buffer.

name may be a bare filename (e.g. "leaded.rfl") or a full pathname. Bare filenames are resolved against the profile storage directory.

The file is read as a ProfileFileHeader followed by a ReflowProfile struct. Returns false if the file is absent, the header magic or version is wrong, the read size is incorrect, or stageCount exceeds kReflowMaxStages.

| Parameter | Description |
|-----------|-------------|
| `name` | Filename or full path of the profile to load. |
| `out` | Caller-supplied buffer to receive the profile data. |

**Returns:** True if the profile was read and validated successfully.

#### `ReflowProfileSave`

```c
bool ReflowProfileSave( const ReflowProfile * profile, const char * name )
```

Save a profile to the filesystem.

name may be a bare filename or a full pathname. Bare filenames are written to the profile storage directory.

Writes a ProfileFileHeader followed by the ReflowProfile struct. Creates the file if absent; truncates it if it exists. Returns false on any I/O error or if path resolution fails.

| Parameter | Description |
|-----------|-------------|
| `profile` | Profile to save. |
| `name` | Destination filename or full path. |

**Returns:** True if the profile was written successfully.

---

## ReflowTask

*Reflow task — profile execution and thermal control loop.*

### Functions

#### `ReflowTaskInit`

```c
void ReflowTaskInit( void )
```

Initialise the Reflow task — waits for system initialisation signal.

Blocks on FlagSystemInitialised in SystemStatusFlagsHandle before returning. Called once by app_freertos.c at startup.

#### `ReflowTaskLoop`

```c
void ReflowTaskLoop( void )
```

Main execution body of the Reflow task loop.

Currently a placeholder — implementation pending reflow profile driver.

---

## RotaryEncoder

*AS5600 magnetic rotary encoder driver for oven fan speed measurement.*

---

## SPIManager

*SPI bus manager with asynchronous and synchronous transfer APIs.*

### Types

#### `SPIID`

Logical identifiers for SPI bus instances managed by this driver.

| Value | Description |
|-------|-------------|
| `SPIBus1` | SPI1 — flash and thermocouple bus. |
| `SPIBusCount` |  |

#### `SPIStatusBit`

Status and diagnostic flag bit positions for an SPI bus instance. These map 1:1 to the bits in the per-instance statusHandle event flag group.

| Value | Description |
|-------|-------------|
| `FlagSPIStatusReady` | Bus initialised and ready for transfers. |
| `FlagSPIStatusBusError` | MODF or general HAL bus error. |
| `FlagSPIStatusOverrun` | RX overrun (OVR) |
| `FlagSPIStatusTimeout` | Semaphore or transfer timeout. |
| `FlagSPIStatusCRCError` | CRC mismatch on received data. |
| `FlagSPIStatusDMAError` | DMA transfer error. |
| `FlagSPIStatusResourceConflict` | High-priority diagnostic for RTOS task collisions. |
| `FlagSPIStatusInvalidParam` | NULL pointer or zero-length buffer passed to API. |
| `FlagSPIStatusConfigError` | Peripheral register corruption detected at init. |
| `SPIFlagsCount` |  |

### Functions

#### `SPIInitModule`

```c
void SPIInitModule( void )
```

Initialise all SPI bus instances, bind hardware handles, and register HAL callbacks.

#### `SPIOpen`

```c
SPIRef SPIOpen( SPIID id )
```

Return a handle to a specific SPI bus instance.

| Parameter | Description |
|-----------|-------------|
| `id` | Bus identifier. |

**Returns:** Handle to the instance, or NULL if id is out of range.

#### `SPIGetStatus`

```c
uint32_t SPIGetStatus( SPIRef spi )
```

Return the full status bitmask for a specific SPI bus instance.

| Parameter | Description |
|-----------|-------------|
| `spi` | Handle returned by SPIOpen(). |

**Returns:** Bitmask of SPIStatusBit flags; 0 if spi is NULL.

#### `SPIReadAsync`

```c
void SPIReadAsync( SPIRef spi, GPIO_TypeDef * csPort, uint16_t csPin, uint8_t * pData, uint16_t len, SPICallback cb )
```

Start an asynchronous interrupt-driven read from a device.

| Parameter | Description |
|-----------|-------------|
| `spi` | Handle returned by SPIOpen(). |
| `csPort` | GPIO port for the chip-select line. |
| `csPin` | GPIO pin for the chip-select line. |
| `pData` | Destination buffer; must remain valid until cb fires. |
| `len` | Number of bytes to receive. |
| `cb` | Completion callback invoked from HAL ISR context. |

#### `SPIWriteAsync`

```c
void SPIWriteAsync( SPIRef spi, GPIO_TypeDef * csPort, uint16_t csPin, uint8_t * pData, uint16_t len, SPICallback cb )
```

Start an asynchronous interrupt-driven write to a device.

| Parameter | Description |
|-----------|-------------|
| `spi` | Handle returned by SPIOpen(). |
| `csPort` | GPIO port for the chip-select line. |
| `csPin` | GPIO pin for the chip-select line. |
| `pData` | Source buffer; must remain valid until cb fires. |
| `len` | Number of bytes to transmit. |
| `cb` | Completion callback invoked from HAL ISR context. |

#### `SPITransceiveAsync`

```c
void SPITransceiveAsync( SPIRef spi, GPIO_TypeDef * csPort, uint16_t csPin, uint8_t * pTxData, uint16_t txLen, uint8_t * pRxData, uint16_t rxLen, SPICallback cb )
```

Start an atomic asynchronous write-then-read without toggling CS between phases.

| Parameter | Description |
|-----------|-------------|
| `spi` | Handle returned by SPIOpen(). |
| `csPort` | GPIO port for the chip-select line. |
| `csPin` | GPIO pin for the chip-select line. |
| `pTxData` | Transmit buffer; must remain valid until cb fires. |
| `txLen` | Number of bytes to transmit. |
| `pRxData` | Receive buffer; must remain valid until cb fires. |
| `rxLen` | Number of bytes to receive. |
| `cb` | Completion callback invoked from HAL ISR context. |

#### `SPIWriteSync`

```c
bool SPIWriteSync( SPIRef spi, GPIO_TypeDef * csPort, uint16_t csPin, uint8_t * pData, uint16_t len, uint32_t timeout )
```

Perform a synchronous blocking write.

| Parameter | Description |
|-----------|-------------|
| `spi` | Handle returned by SPIOpen(). |
| `csPort` | GPIO port for the chip-select line. |
| `csPin` | GPIO pin for the chip-select line. |
| `pData` | Source buffer. |
| `len` | Number of bytes to transmit. |
| `timeout` | Maximum wait in milliseconds. |

**Returns:** true on success, false on timeout or bus error.

#### `SPIReadSync`

```c
bool SPIReadSync( SPIRef spi, GPIO_TypeDef * csPort, uint16_t csPin, uint8_t * pData, uint16_t len, uint32_t timeout )
```

Perform a synchronous blocking read.

| Parameter | Description |
|-----------|-------------|
| `spi` | Handle returned by SPIOpen(). |
| `csPort` | GPIO port for the chip-select line. |
| `csPin` | GPIO pin for the chip-select line. |
| `pData` | Destination buffer. |
| `len` | Number of bytes to receive. |
| `timeout` | Maximum wait in milliseconds. |

**Returns:** true on success, false on timeout or bus error.

#### `SPITransceiveSync`

```c
bool SPITransceiveSync( SPIRef spi, GPIO_TypeDef * csPort, uint16_t csPin, uint8_t * pTxData, uint16_t txLen, uint8_t * pRxData, uint16_t rxLen, uint32_t timeout )
```

Perform an atomic synchronous write-then-read without CS toggling between phases.

| Parameter | Description |
|-----------|-------------|
| `spi` | Handle returned by SPIOpen(). |
| `csPort` | GPIO port for the chip-select line. |
| `csPin` | GPIO pin for the chip-select line. |
| `pTxData` | Transmit buffer. |
| `txLen` | Number of bytes to transmit. |
| `pRxData` | Receive buffer. |
| `rxLen` | Number of bytes to receive. |
| `timeout` | Maximum wait in milliseconds. |

**Returns:** true on success, false on timeout or bus error.

---

## SystemStatusFlags

*Global event flag group definitions for cross-task system state.*

### Types

#### `SystemFlagBit`

System milestone flags, stored in SystemStatusFlagsHandle.

| Value | Description |
|-------|-------------|
| `FlagSystemInitialised` | All drivers are ready; tasks may proceed. |
| `FlagInterruptsEnabled` | GPIO edge interrupts have been unmasked. |
| `FlagSupervisorServiceRequest` | ManagerTask: a supervisor action is requested. |
| `SystemFlagsCount` | Number of flags — must stay <= 24. |

#### `DeviceFlagsBit`

Per-device ready flags, stored in DeviceStatusFlagsHandle.

| Value | Description |
|-------|-------------|
| `FlagMCUReady` | MCU peripheral init complete. |
| `FlagUSBPDReady` | USB-PD controller init complete. — FEATURE_USB_PD. |
| `FlagThermocouple1Ready` | MAX31856 channel 1 ready. — FEATURE_THERMOCOUPLE_1. |
| `FlagThermocouple2Ready` | MAX31856 channel 2 ready. — FEATURE_THERMOCOUPLE_2. |
| `FlagThermistorCJT1Ready` | Cold-junction thermistor 1 (MCP3221) ready. — FEATURE_THERMISTOR_CJT_1. |
| `FlagThermistorCJT2Ready` | Cold-junction thermistor 2 (MCP3221) ready. — FEATURE_THERMISTOR_CJT_2. |
| `FlagThermistorOvenReady` | Oven cavity thermistor (MCP3221) ready. — FEATURE_THERMISTOR_OVEN. |
| `FlagThermistorHeatsinkReady` | Heatsink thermistor (EMC2101 I2C) ready. — FEATURE_THERMISTOR_HEATSINK. |
| `FlagPowerManagerReady` | Power manager init complete. |
| `FlagBuzzerReady` | Buzzer driver init complete. — FEATURE_BUZZER. |
| `FlagOvenFanReady` | Oven fan (AC) driver ready. — FEATURE_OVEN_FAN. |
| `FlagBoardFanReady` | Board cooling fan (DC, EMC2101) ready. — FEATURE_BOARD_FAN. |
| `FlagTriacReady` | TRIAC phase-angle driver ready. |
| `FlagFlashReady` | External NOR flash init and verified. — FEATURE_FLASH. |
| `FlagHeaterTopReady` | Top heating element TRIAC channel ready. — FEATURE_HEATER_TOP. |
| `FlagHeaterRearReady` | Rear heating element TRIAC channel ready. — FEATURE_HEATER_REAR. |
| `FlagHeaterBottomReady` | Bottom heating element TRIAC channel ready. — FEATURE_HEATER_BOTTOM. |
| `FlagOvenLightReady` | Oven interior light TRIAC channel ready. — FEATURE_OVEN_LIGHT. |
| `FlagOvenControllerReady` | Oven controller initialised and all device refs valid. |
| `FlagRotaryEncoderReady` | Rotary encoder (AS5600) initialised and I2C ref valid. — FEATURE_ROTARY_ENCODER. |
| `FlagReflowEngineReady` | Reflow engine opened and OvenController ref acquired. |
| `DeviceFlagsCount` | Number of flags — must stay <= 24. |

#### `FaultFlagsBit`

Active fault flags, stored in FaultFlagsHandle.

| Value | Description |
|-------|-------------|
| `FlagESTOP` | Emergency stop was triggered. |
| `FlagMCUFault` | MCU peripheral or watchdog fault. |
| `FlagPowerManagerFault` | Power supply out of range. |
| `FlagUSBPDFault` | USB-PD negotiation or hardware fault. — FEATURE_USB_PD. |
| `FlagThermocouple1Fault` | Thermocouple 1 open-circuit or CRC error. — FEATURE_THERMOCOUPLE_1. |
| `FlagThermocouple2Fault` | Thermocouple 2 open-circuit or CRC error. — FEATURE_THERMOCOUPLE_2. |
| `FlagThermistorCJT1Fault` | Cold-junction thermistor 1 read failure. — FEATURE_THERMISTOR_CJT_1. |
| `FlagThermistorCJT2Fault` | Cold-junction thermistor 2 read failure. — FEATURE_THERMISTOR_CJT_2. |
| `FlagThermistorOvenFault` | Oven cavity thermistor read failure. — FEATURE_THERMISTOR_OVEN. |
| `FlagThermistorHeatsinkFault` | Heatsink thermistor read failure. — FEATURE_THERMISTOR_HEATSINK. |
| `FlagOvenFanFault` | Oven fan stall or speed error. — FEATURE_OVEN_FAN. |
| `FlagBoardFanFault` | Board fan stall or speed error. — FEATURE_BOARD_FAN. |
| `FlagBuzzerFault` | Buzzer hardware fault. — FEATURE_BUZZER. |
| `FlagReflowFault` | Reflow profile logic error. |
| `FlagI2CFault` | I2C bus error or timeout. |
| `FlagSPIFault` | SPI bus error or timeout. |
| `FlagTriacFault` | TRIAC gate or ZCD fault. |
| `FlagFlashFault` | NOR flash SPI error or JEDEC ID mismatch. — FEATURE_FLASH. |
| `FlagHeaterTopFault` | Top heating element TRIAC configuration error. — FEATURE_HEATER_TOP. |
| `FlagHeaterRearFault` | Rear heating element TRIAC configuration error. — FEATURE_HEATER_REAR. |
| `FlagHeaterBottomFault` | Bottom heating element TRIAC configuration error. — FEATURE_HEATER_BOTTOM. |
| `FlagOvenLightFault` | Oven interior light TRIAC configuration error. — FEATURE_OVEN_LIGHT. |
| `FlagOvenControllerFault` | Oven controller regulation fault or required sensor failure. |
| `FaultFlagsCount` | Number of flags — must stay <= 24. |

---

## TaskUtils

*FreeRTOS / bare-metal portability layer.*

---

## Thermistor

*NTC thermistor driver using the STM32 internal ADC DMA buffer.*

---

## ThermistorI2C

*MCP3221 I2C ADC heatsink thermistor driver.*

---

## Thermocouple

*MAX31856 dual thermocouple driver.*

---

## Triac

*Phase-angle and burst-fire TRIAC driver for AC load control.*

### Types

#### `TriacStatusBit`

Status and diagnostic flag bit positions for a TRIAC channel. These map 1:1 to the bits in each instance's private event flag group.

| Value | Description |
|-------|-------------|
| `FlagTriacStatusReady` | Driver initialised and GPIO mapped. |
| `FlagTriacStatusActive` | Power is requested (manual ON or burst window ON) |
| `FlagTriacStatusGateOpen` | Physical GPIO is LOW (gate is conducting) |
| `FlagTriacStatusZCDLost` | AC line sync lost (ZCD watchdog timeout) |
| `FlagTriacStatusConfigError` | Invalid parameters detected in TriacRun() |
| `FlagTriacStatusPhaseAngle` | Sequenced phase-angle control mode is active. |
| `FlagTriacStatusPulsePending` | Sequencer has queued this channel for a timed pulse. |
| `FlagTriacStatusPulseActive` | This channel is currently inside its 100 µs pulse window. |
| `TriacFlagsCount` |  |

#### `TriacID`

Logical identifiers for fitted AC load channels. Only channels whose FEATURE_ flag is set are compiled in. TriacCount reflects the number of fitted channels; the internal array is sized accordingly.

| Value | Description |
|-------|-------------|
| `TriacCount` |  |

#### `TriacDriveParams`

TRIAC drive parameters for a single channel.

| Type | Field | Description |
|------|-------|-------------|
| `uint16_t` | `phaseDelayUs` | Delay from ZCD to gate fire (0 = full power, up to 10000 µs) |
| `uint8_t` | `burstOn` | Number of half-cycles 'On' within the burst window. |
| `uint8_t` | `burstWindow` | Total window size in half-cycles (burstOn + burstOff) |

### Functions

#### `TriacInitModule`

```c
void TriacInitModule( void )
```

Initialise all TRIAC channels, create per-instance event flags, and register TIM16 callback.

Sets all gate GPIOs to the inactive (HIGH, active-low) state and sets FlagTriacStatusReady for each channel. Signals FlagTriacReady in DeviceStatusFlagsHandle.

Creates one osEventFlagsId_t per channel (stored in dev->flags). Sets all gate GPIOs to the safe inactive state (HIGH = gate off, active-low). Sets FlagTriacStatusReady on each channel and signals FlagTriacReady in DeviceStatusFlagsHandle.

#### `TriacOpen`

```c
TriacRef TriacOpen( TriacID id )
```

Return a handle to the specified TRIAC channel.

| Parameter | Description |
|-----------|-------------|
| `id` | Channel identifier from TriacID. |

**Returns:** Handle to the instance, or NULL if id is out of range.

#### `TriacOn`

```c
void TriacOn( TriacRef triac )
```

Drive a TRIAC at full power, bypassing phase-angle sequencing.

| Parameter | Description |
|-----------|-------------|
| `triac` | Handle returned by TriacOpen(). |

#### `TriacOff`

```c
void TriacOff( TriacRef triac )
```

De-energise a TRIAC and remove it from sequenced control.

| Parameter | Description |
|-----------|-------------|
| `triac` | Handle returned by TriacOpen(). |

#### `TriacRun`

```c
void TriacRun( TriacRef triac, TriacDriveParams params )
```

Configure a TRIAC for phase-angle / burst-fire control and validate the parameters.

Validates that phaseDelayUs ≤ 10000 µs and burstOn ≤ burstWindow. On validation failure, sets FlagTriacStatusConfigError and raises FlagTriacFault. On success, loads the parameters and sets FlagTriacStatusPhaseAngle.

Validates parameters, then loads them and sets FlagTriacStatusPhaseAngle. The change takes effect at the next ZCD interrupt.

| Parameter | Description |
|-----------|-------------|
| `triac` | Handle returned by TriacOpen(). |
| `params` | Drive parameters to apply. |

#### `TriacGetStatus`

```c
uint32_t TriacGetStatus( TriacRef triac )
```

Return the full status bitmask for a TRIAC channel.

| Parameter | Description |
|-----------|-------------|
| `triac` | Handle returned by TriacOpen(). |

**Returns:** Bitmask of TriacStatusBit flags; 0 if triac is NULL.

#### `TriacProcess`

```c
void TriacProcess( void )
```

Task-loop tick: check the ZCD watchdog and assert ZCDLost / TriacFault on AC loss.

If more than 50 ms have elapsed since the last ZCD interrupt (equivalent to 5 missed zero-crosses at 50 Hz), sets FlagTriacStatusZCDLost on all channels and raises FlagTriacFault in FaultFlagsHandle.

#### `ZCDHandler`

```c
void ZCDHandler( uint16_t GPIO_Pin )
```

Zero-cross rising-edge ISR handler — drives the burst sequencer and primes TIM16.

Resets all sequencer transient flags, updates burst counters, identifies channels that need a timed gate pulse, sorts them by phase delay, and arms TIM16 to fire at the first channel's phase offset.

On every AC zero-cross rising edge:
Resets the sequencer state variables.
Iterates all channels: clears transient flags, advances burst counters, and for channels in phase-angle mode, either fires immediately (alpha=0) or adds them to activeSequence for timed firing.
Insertion-sorts activeSequence by phaseDelayUs (shortest delay first).
Arms TIM16 with the first channel's phase delay and generates an immediate update event to latch the ARR before starting the timer.

| Parameter | Description |
|-----------|-------------|
| `GPIO_Pin` | HAL pin mask (unused; only one ZCD pin exists). |

---

## Types

*Project-wide primitive type aliases.*

---

## USBPDTask

*USB Power Delivery task — independent 10 ms process loop.*

---

## USBPowerDelivery

*USB Power Delivery driver for STPD01 buck converter and TCPP03 protection IC.*

---

## Volume

*Volume (mounted filesystem instance) management.*

---

