# Function Reference

---

## ACFan

*AC oven circulation fan driver.*

### Types

#### `ACFanID`

Logical identifiers for AC fan instances managed by this driver.

| Value | Description |
|-------|-------------|
| `OvenFan` | AC induction fan in the oven cavity. |
| `ACFanCount` |  |

#### `ACFanStatusBit`

Status and diagnostic flag bit positions for an AC fan instance. These map 1:1 to the bits in the per-instance statusHandle event flag group.

| Value | Description |
|-------|-------------|
| `FlagACFanStatusReady` | Profile loaded and drive is operational. |
| `FlagACFanCalibrationRequired` | No valid profile found; speed commands ignored. |
| `FlagACFanHasEncoder` | An encoder has been attached via ACFanAttachEncoder(). |
| `FlagACFanStatusSpinning` | Encoder confirms rotation above threshold. |
| `FlagACFanStatusStall` | Speed > 0 requested but encoder reads ~0 RPM. |
| `FlagACFanStatusHardwareFault` | TRIAC config error or encoder communication failure. |
| `FlagACFanSpeedPending` | Speed command queued; not yet applied to TRIAC. |
| `ACFanFlagsCount` |  |

### Functions

#### `ACFanInitModule`

```c
void ACFanInitModule( void )
```

Allocate per-instance resources. Does not access hardware or the filesystem.

#### `ACFanOpen`

```c
ACFanRef ACFanOpen( ACFanID id, TriacID triacID )
```

Open a handle to a specific AC fan instance.

On first call for a given ID: opens the TRIAC handle and attempts to load the calibration profile from flash. Sets FlagACFanStatusReady on success or FlagACFanCalibrationRequired if no valid profile is found. Subsequent calls with the same ID return the existing instance without re-opening. Call ACFanAttachEncoder() after ACFanOpen() to enable speed feedback.

| Parameter | Description |
|-----------|-------------|
| `id` | Fan instance identifier. |
| `triacID` | TRIAC channel identifier for the oven fan. |

**Returns:** Handle to the instance, or NULL if id is out of range.

#### `ACFanAttachEncoder`

```c
void ACFanAttachEncoder( ACFanRef fan, RotaryEncoderID encoderID )
```

Attach a rotary encoder to a fan instance for speed feedback.

Looks up the encoder by ID and stores it in the instance. ACFanProcess() will then update spinning/stall flags and ACFanGetSpeed() will return live RPM. Safe to call at any time after ACFanOpen().

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by ACFanOpen(). |
| `encoderID` | Rotary encoder identifier for the fan shaft. |

#### `ACFanSetSpeed`

```c
void ACFanSetSpeed( ACFanRef fan, Permille speed )
```

Queue a speed request; applied to the TRIAC by ACFanProcess() on the next tick.

Ignored if FlagACFanCalibrationRequired is set. Speed is clamped to [0, 1000].

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by ACFanOpen(). |
| `speed` | Desired speed in permille of motorMaxRPM (0 = off, 1000 = full). |

#### `ACFanGetSpeed`

```c
Rpm ACFanGetSpeed( ACFanRef fan )
```

Return the most recently measured fan speed from the encoder.

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by ACFanOpen(). |

**Returns:** Cached speed in RPM; 0 if fan is NULL or FEATURE_ROTARY_ENCODER is disabled.

#### `ACFanGetStatus`

```c
uint32_t ACFanGetStatus( ACFanRef fan )
```

Return the full status bitmask for this fan instance.

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by ACFanOpen(). |

**Returns:** Bitmask of ACFanStatusBit flags; BIT(FlagACFanStatusHardwareFault) if fan is NULL.

#### `ACFanProcess`

```c
void ACFanProcess( void )
```

Apply any pending speed command and update status flags from the encoder.

#### `ACFanCalibrate`

```c
bool ACFanCalibrate( ACFanRef fan )
```

Run the full calibration sequence and persist the result for this fan instance.

Requires an encoder attached via ACFanAttachEncoder() — returns immediately if FlagACFanHasEncoder is not set. On success, writes the calibration profile to /system/fanN.profile (N = instance ID), loads it into the instance, and clears FlagACFanCalibrationRequired.

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by ACFanOpen(). |

**Returns:** True if calibration succeeded and the fan is ready to accept speed commands; false if aborted, stopped, or the motor failed to spin.

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

Initialise (or reset) the stream parser. Call once before ProcessStream().

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

Dequeue the next complete request from the input queue, or NULL if none available.

The caller takes ownership and must return the PB with ReleasePB() when done.

The caller takes ownership and must call ReleasePB() when done.

**Returns:** Pointer to the oldest queued APIPB, or NULL.

#### `APIQueueForSend`

```c
void APIQueueForSend( APIPBPtr pb )
```

Serialise a completed APIPB response and enqueue it for USB transmission.

Serialise a completed APIPB and enqueue for USB transmission.

Dispatches to SerialiseAPI() for API_MODE_API or SerialiseCLI() for all other origins. Releases the PB and all attached payload back to their pools.

| Parameter | Description |
|-----------|-------------|
| `pb` | APIPB to serialise and release; may be NULL. |

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

Initialise the API engine — zero all storage and populate the free-list pools.

Initialise pools and queues. Call once at startup before any other APICore function.

Must be called once before any other APICore function. APITaskInit() calls this after waiting for FlagSystemInitialised. All previously acquired objects are invalidated; do not call after startup.

#### `GetInputQueue`

```c
APIPBQueueRef GetInputQueue( void )
```

Return the input queue — received requests awaiting dispatch.

Return an opaque reference to the input (received-request) queue.

Return the input queue — received requests awaiting dispatch.

**Returns:** Queue reference for use with EnqueuePB() / DequeuePB().

#### `GetOutputQueue`

```c
APIBufferQueueRef GetOutputQueue( void )
```

Return the output queue — serialised responses awaiting transmission.

Return an opaque reference to the output (serialised-response) queue.

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

Return an APIPB and all attached Payload nodes to their respective pools.

Calls ReleasePBMembers() first, then returns the PB. The caller must not access pb after this call.

| Parameter | Description |
|-----------|-------------|
| `pb` | APIPB to release; ignores NULL. |

#### `EnqueuePB`

```c
void EnqueuePB( APIPBQueueRef q, APIPBPtr pb )
```

Append an APIPB to the tail of q; notifies the API task if q is the input queue.

Append an APIPB to the tail of a queue and wake the API task if it is the input queue.

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

Remove and return the APIPB at the head of a queue, or NULL if empty.

| Parameter | Description |
|-----------|-------------|
| `q` | Source queue. |

**Returns:** Oldest queued APIPB, or NULL.

#### `EnqueueBuffer`

```c
void EnqueueBuffer( APIBufferQueueRef q, APIBufferPtr b )
```

Append an APIBuffer chain to the tail of q; notifies the API task if q is the output queue.

Append a serialised APIBuffer chain to an output queue and wake the API task.

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

Remove and return the APIBuffer at the head of a queue, or NULL if empty.

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

Zero a Payload node's data array and return it to the pool.

| Parameter | Description |
|-----------|-------------|
| `p` | Payload to release; ignores NULL. |

#### `ReleasePBMembers`

```c
void ReleasePBMembers( APIPBPtr pb )
```

Release all Payload nodes attached to pb without returning the PB itself.

Release all Payload nodes attached to an APIPB without returning the PB itself.

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

Zero a transmit APIBuffer and return it to the pool.

| Parameter | Description |
|-----------|-------------|
| `b` | Buffer to release; ignores NULL. |

#### `APICoreGetStats`

```c
APICoreStatsRef APICoreGetStats( void )
```

Refresh and return a snapshot of the current API engine statistics.

Snapshot current engine statistics into the APICoreStats struct and return a pointer.

The returned pointer is valid until the next APICoreInit() call. The live-count fields (inputQueued, outputQueued, *MemUsed) are refreshed on each call; pool free-counts and peak marks are maintained incrementally by the pool helpers.

**Returns:** Read-only pointer to the internal APICoreStats; valid until next APICoreInit().

---

## APIHandlers

*API handler function declarations.*

### Functions

#### `HandlerOvenStatus`

```c
APIPBPtr HandlerOvenStatus( APIPBPtr pb )
```

Report the current oven run state and active profile.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with status JSON, or NULL.

#### `HandlerOvenRun`

```c
APIPBPtr HandlerOvenRun( APIPBPtr pb )
```

Start a reflow run using the profile specified in the request payload.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerOvenStop`

```c
APIPBPtr HandlerOvenStop( APIPBPtr pb )
```

Stop the current reflow run gracefully.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerOvenEstop`

```c
APIPBPtr HandlerOvenEstop( APIPBPtr pb )
```

Trigger an immediate emergency stop.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerManualEnable`

```c
APIPBPtr HandlerManualEnable( APIPBPtr pb )
```

Enter manual control mode, allowing direct heater and fan commands.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerManualDisable`

```c
APIPBPtr HandlerManualDisable( APIPBPtr pb )
```

Exit manual control mode and return to safe idle state.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerManualHeater`

```c
APIPBPtr HandlerManualHeater( APIPBPtr pb )
```

Set heater power level in manual control mode.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerManualFan`

```c
APIPBPtr HandlerManualFan( APIPBPtr pb )
```

Set cooling fan speed in manual control mode.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerSensorsTemp`

```c
APIPBPtr HandlerSensorsTemp( APIPBPtr pb )
```

Return current temperature readings from all sensors.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with sensor JSON, or NULL.

#### `HandlerSensorsMains`

```c
APIPBPtr HandlerSensorsMains( APIPBPtr pb )
```

Return mains voltage and ZCD status.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with mains JSON, or NULL.

#### `HandlerProfilesList`

```c
APIPBPtr HandlerProfilesList( APIPBPtr pb )
```

Return a list of stored reflow profiles.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with profile list JSON, or NULL.

#### `HandlerProfileGet`

```c
APIPBPtr HandlerProfileGet( APIPBPtr pb )
```

Return a single profile identified by the request payload.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with profile JSON, or NULL.

#### `HandlerProfileCreate`

```c
APIPBPtr HandlerProfileCreate( APIPBPtr pb )
```

Create a new reflow profile from the request payload.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerProfileUpdate`

```c
APIPBPtr HandlerProfileUpdate( APIPBPtr pb )
```

Update an existing profile with data from the request payload.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerProfileDelete`

```c
APIPBPtr HandlerProfileDelete( APIPBPtr pb )
```

Delete the profile identified by the request payload.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerConfigGet`

```c
APIPBPtr HandlerConfigGet( APIPBPtr pb )
```

Return the device configuration.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with config JSON, or NULL.

#### `HandlerConfigPut`

```c
APIPBPtr HandlerConfigPut( APIPBPtr pb )
```

Replace the device configuration with the request payload.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerLogsList`

```c
APIPBPtr HandlerLogsList( APIPBPtr pb )
```

Return a list of stored log files.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with log list JSON, or NULL.

#### `HandlerLogGet`

```c
APIPBPtr HandlerLogGet( APIPBPtr pb )
```

Return the contents of a single log file.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with log data, or NULL.

#### `HandlerLogDelete`

```c
APIPBPtr HandlerLogDelete( APIPBPtr pb )
```

Delete a single log file identified by the request payload.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerLogsClear`

```c
APIPBPtr HandlerLogsClear( APIPBPtr pb )
```

Delete all stored log files.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerStorageGet`

```c
APIPBPtr HandlerStorageGet( APIPBPtr pb )
```

Return file system status and free space.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with storage JSON, or NULL.

#### `HandlerStorageFormat`

```c
APIPBPtr HandlerStorageFormat( APIPBPtr pb )
```

Format the storage partition.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerSystemStatus`

```c
APIPBPtr HandlerSystemStatus( APIPBPtr pb )
```

Return system status flags, uptime, and firmware version.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with system JSON, or NULL.

#### `HandlerClockGet`

```c
APIPBPtr HandlerClockGet( APIPBPtr pb )
```

Return the current RTC date and time.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with clock JSON, or NULL.

#### `HandlerClockPut`

```c
APIPBPtr HandlerClockPut( APIPBPtr pb )
```

Set the RTC date and time from the request payload.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerSystemReset`

```c
APIPBPtr HandlerSystemReset( APIPBPtr pb )
```

Perform a software reset of the MCU.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerPowerGet`

```c
APIPBPtr HandlerPowerGet( APIPBPtr pb )
```

Return USB PD contract details and live voltage/current readings.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** Payload with power JSON, or NULL.

#### `HandlerUiLight`

```c
APIPBPtr HandlerUiLight( APIPBPtr pb )
```

Set the status indicator light state from the request payload.

| Parameter | Description |
|-----------|-------------|
| `pb` | Request PB — caller must ReleasePB(pb). |

**Returns:** NULL.

#### `HandlerUiBuzzer`

```c
APIPBPtr HandlerUiBuzzer( APIPBPtr pb )
```

Play a buzzer melody or tone specified in the request payload.

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
| `APIReqOvenStatus` | get /oven/status |
| `APIReqOvenRun` | put /oven/run |
| `APIReqOvenStop` | put /oven/stop |
| `APIReqOvenEstop` | put /oven/estop |
| `APIReqManualEnable` | put /oven/manual/enable |
| `APIReqManualDisable` | put /oven/manual/disable |
| `APIReqManualHeater` | put /oven/manual/heater |
| `APIReqManualFan` | put /oven/manual/fan |
| `APIReqSensorsTemp` | get /sensors/temperature |
| `APIReqSensorsMains` | get /sensors/mains |
| `APIReqProfileList` | get /profiles |
| `APIReqProfileGet` | get /profiles/s |
| `APIReqProfileCreate` | post /profiles/s |
| `APIReqProfileUpdate` | put /profiles/s |
| `APIReqProfileDelete` | delete /profiles/s |
| `APIReqConfigGet` | get /config |
| `APIReqConfigPut` | put /config |
| `APIReqLogsList` | get /logs |
| `APIReqLogGet` | get /logs/s |
| `APIReqLogDelete` | delete /logs/s |
| `APIReqLogsClear` | delete /logs |
| `APIReqStorageGet` | get /storage |
| `APIReqStorageFormat` | put /storage/format |
| `APIReqSystemStatus` | get /system/status |
| `APIReqClockGet` | get /system/clock |
| `APIReqClockPut` | put /system/clock |
| `APIReqSystemReset` | put /system/reset |
| `APIReqPowerGet` | get /power |
| `APIReqUiLight` | put /ui/light |
| `APIReqUiBuzzer` | put /ui/buzzer |

#### `APIRoute`

A single entry in the API route table.

| Type | Field | Description |
|------|-------|-------------|
| `const char *` | `pattern` | sscanf-compatible pattern used for both route matching and typed argument extraction. |
| `APISyntax` | `syntax` | Serialisation format for the response. |
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

Initialise the API task — waits for system init then starts API subsystems.

Waits for FlagSystemInitialised in SystemStatusFlagsHandle before proceeding. Called once by app_freertos.c at startup.

Initialise the API core and buffer stream subsystems.

Blocks on FlagSystemInitialised to ensure all drivers are ready before accepting USB requests. Initialises the API core pool and stream parser. Called once by app_freertos.c at startup.

#### `APITaskLoop`

```c
void APITaskLoop( void )
```

Main execution body of the API task loop.

API task main loop body — processes requests and drives the transmit queue.

Blocks on xTaskNotifyWait(), processes pending requests via the route table, queues serialised responses, and drains the USB transmit queue. Called repeatedly by app_freertos.c in the task's infinite loop.

Main execution body of the API task loop.

Blocks on xTaskNotifyWait() until woken by a USB receive, TX complete, or APICore request-enqueue notification. Drains the input queue, dispatches each request to its route handler, and calls USBSendAll() to flush any pending output.

Called repeatedly by app_freertos.c in the task's infinite loop.

#### `USBTxDoneHandler`

```c
void USBTxDoneHandler( void )
```

USB transmission complete callback — advances the transmit pipeline.

USB CDC TX-complete callback — advances or closes the transmit pipeline.

Called directly from the USB CDC ISR (usbd_cdc_if.c). Releases the completed buffer, starts the next link in the chain if present, or wakes the API task via task notification to re-check the output queue.

Releases the completed buffer. If a next link is available in the chain, starts it immediately via CDC_Transmit_FS(). Otherwise resets currentUSBBuffer to NULL and wakes the API task so it can dequeue the next buffer.

---

## APITypes

*Core API type definitions shared across the codec, core, and handler layers.*

### Types

#### `APIStatus`

HTTP-like response status codes returned by handler functions.

| Value | Description |
|-------|-------------|
| `APIStatusOK` | Request succeeded. |
| `APIStatusCreated` | Resource created successfully. |
| `APIStatusNoContent` | Request succeeded; no body to return. |
| `APIStatusBadRequest` | Malformed request or invalid parameters. |
| `APIStatusNotFound` | No route matched the incoming request. |
| `APIStatusConflict` | Request conflicts with current state. |
| `APIStatusInternalError` | Handler encountered an internal error. |

#### `APISyntax`

Serialisation syntax declared in each route entry.

| Value | Description |
|-------|-------------|
| `APISyntaxREST` | Serialise as REST response (e.g. JSON or CBOR for a web client). |
| `APISyntaxCLI` | Serialise as human-readable text for a terminal user. |

#### `APIMethod`

HTTP verb parsed from the incoming request line.

| Value | Description |
|-------|-------------|
| `APIMethodUnknown` | No verb recognised. |
| `APIMethodGet` | HTTP GET. |
| `APIMethodPost` | HTTP POST. |
| `APIMethodPut` | HTTP PUT. |
| `APIMethodDelete` | HTTP DELETE. |

#### `TerminatorType`

Line-ending style detected by the stream parser.

| Value | Description |
|-------|-------------|
| `TermNull` | No terminator detected (sentinel; unused after a completed parse). |
| `TermLF` | Bare LF (0x0A). |
| `TermCR` | Bare CR (0x0D). |
| `TermCRLF` | CR+LF pair (0x0D 0x0A). |
| `TermZero` | Null byte (0x00); response is also null-terminated. |

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
| `uint8_t` | `reqString` | Full sanitised (lowercased, space-collapsed) request line captured by the stream parser. |
| `const struct APIRoute *` | `route` | Matched route entry, or NULL if not found. |
| `PayloadPtr` | `payload` | Head of the response body payload chain. |
| `APISyntax` | `syntax` | Serialisation format from the matched route. |
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

Initialise the Device task — waits for system initialisation then opens I2C peripherals.

Blocks on FlagSystemInitialised in SystemStatusFlagsHandle before returning. Called once by app_freertos.c at startup.

Initialise the Device task — waits for system initialisation signal.

Blocks on FlagSystemInitialised to ensure all modules are initialised. Then opens any I2C-connected peripherals that this task owns, supplying the bus reference they need. Hardware errors during Open() set fault flags rather than blocking startup.

#### `DeviceTaskLoop`

```c
void DeviceTaskLoop( void )
```

Main execution body of the Device task loop.

Call the Process() function for every driver module in sequence.

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

## FSUtils

*Filesystem utility helpers — path manipulation and single-call file I/O.*

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
| `I2CAddrSTPD01` | STPD01PUR — USB-PD power supply controller (ADD=GND). |
| `I2CAddrAS5600` | AS5600 — magnetic encoder / fan tachometer (CN4, fixed). |
| `I2CAddrMCP3221` | MCP3221A2T — 12-bit I2C ADC for NTC thermistor (A2 variant). |
| `I2CAddrEMC2101` | EMC2101 — DC fan controller / inlet-temp sensor (fixed). |
| `I2CAddrTCPP03` | TCPP03-M20 — USB-C port protection controller (I2C_ADD=GND). |

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
| `FlagI2CStatusBusError` | HAL reported a bus error (BERR or NACK). |
| `FlagI2CStatusArbitrationLost` | Multi-master arbitration lost (ARLO). |
| `FlagI2CStatusTimeout` | Transfer exceeded the caller-supplied timeout. |
| `FlagI2CStatusLocked` | SDA held low by a peripheral (bus locked). |
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

## Interrupts

*System interrupt infrastructure — GPIO dispatch and software interrupt bus.*

### Types

#### `SWIReasonBit`

Reason bits passed to SystemTriggerSWI().

| Value | Description |
|-------|-------------|
| `SWIReasonESTOP` | Emergency stop asserted. |
| `SWIReasonAbort` | General controlled abort. |
| `SWIReasonCount` | Number of reason bits — must stay <= 32. |

### Functions

#### `SystemTriggerSWI`

```c
void SystemTriggerSWI( uint32_t reason )
```

Trigger the system software interrupt with the given reason bitmask.

Stores reason, pends the software interrupt NVIC line, and returns. The ISR sets FlagSystemAborted and dispatches to all handlers whose mask intersects reason in table order.

| Parameter | Description |
|-----------|-------------|
| `reason` | One or more SWIReasonBit values OR'd together via BIT(). |

#### `SystemSWIHandler`

```c
void SystemSWIHandler( void )
```

System software interrupt handler — call from ADC1_COMP_IRQHandler in stm32g0xx_it.c.

System software interrupt handler — called from ADC1_COMP_IRQHandler.

---

## LoggingTask

*Logging task — telemetry and event log persistence.*

### Functions

#### `LoggingTaskInit`

```c
void LoggingTaskInit( void )
```

Initialise the Logging task — waits for system initialisation signal.

Initialise the Logging task — waits for system initialisation before proceeding.

Blocks on FlagSystemInitialised in SystemStatusFlagsHandle before returning. Called once by app_freertos.c at startup.

Initialise the Logging task — waits for system initialisation signal.

#### `LoggingTaskLoop`

```c
void LoggingTaskLoop( void )
```

Main execution body of the Logging task loop.

Logging task loop body — currently empty pending file manager implementation.

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

Refresh cached ADC readings, apply any pending RTC write, and update status flags.

#### `CRCInit`

```c
void CRCInit( MCURef mcu )
```

Acquire the CRC engine and reset the accumulator. Blocks if already in use.

Acquire the CRC engine and reset the accumulator. Blocks until the engine is free.

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

Convenience wrapper: compute CRC-32 over a single contiguous buffer.

Compute CRC-32 over a contiguous buffer. Acquires and releases the engine internally.

#### `CRCVerify`

```c
bool CRCVerify( MCURef mcu, const uint8_t * data, size_t length, uint32_t expected )
```

Compute CRC-32 and compare with an expected value.

Convenience wrapper: compute CRC-32 and compare against an expected value.

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

Initialise all driver modules in dependency order, then signal system readiness.

Calls XxxInitModule() for every driver in dependency order. Waits for DEVICE_ALL_READY before enabling interrupts and setting FlagSystemInitialised. Called once by app_freertos.c at startup.

Initialise all hardware driver modules and signal system readiness.

Phase 1 — bus managers: create SPI and I2C semaphores. Must precede any driver that calls SPIOpen() or I2COpen() internally. Phase 2 — all driver InitModule() calls. Each sets its own ready bit in DeviceStatusFlagsHandle on completion, satisfying DEVICE_ALL_READY. Phase 3 — wait for DEVICE_ALL_READY, enable GPIO interrupts, broadcast FlagSystemInitialised.

#### `ManagerTaskLoop`

```c
void ManagerTaskLoop( void )
```

Main execution body of the Manager task loop — fault supervisor.

Supervisor loop — blocks on any active fault and reacts accordingly.

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
| `union OvenControlPB::@100131271336230067170025222116232151313334207060` | `@316011250047115333021116104170373357037053357005` |  |
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
| `FlagPMEStopTripped` | E-Stop loop is open (hardware detected). |
| `FlagPMMainsPower` | AC mains input detected (vs USB-only supply). |
| `FlagPMSwitchedACLive` | ZCD confirms switched AC is present at the output. |
| `FlagPMHotSideEnabled` | Hot-side relay command is active (GPIO driven low). |
| `FlagPMAuxPowerEnabled` | Aux 24 V rail is active. |
| `FlagPMHotSideBlocked` | E-Stop is preventing the hot side from turning on. |
| `FlagPMHotSideRogue` | AC live detected but hot side should be off (safety fault). |
| `FlagPMHotSideDead` | Hot side commanded on but AC not detected (relay fault). |
| `PMFlagsCount` |  |

### Functions

#### `PMInitModule`

```c
void PMInitModule( void )
```

Initialise the Power Manager, sample GPIO state, and disable both output rails.

Initialise the Power Manager — sample GPIO state, disable output rails, set ready flag.

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

ZCD rising-edge ISR handler — records the timestamp and marks AC as live.

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

Reflow command and status flag bit indices for use with BIT().

| Value | Description |
|-------|-------------|
| `FlagReflowStart` | Start a cycle; profile already loaded into state by ReflowStart(). |
| `FlagReflowStop` | Stop the active cycle. |
| `FlagReflowRunning` | A reflow cycle is active. |
| `FlagReflowHoldActive` | Oven has reached stage target; hold condition is being evaluated. |
| `FlagReflowDone` | Cycle completed successfully. |
| `ReflowFlagsCount` | Number of flag bits — must stay <= 32. |

### Functions

#### `ReflowInitModule`

```c
void ReflowInitModule( void )
```

Initialise singleton state and signal module readiness.

Acquire OvenController and ACFan references and signal module readiness.

Called by ManagerTask during the InitModule phase. Acquires OvenController and ACFan references and sets FlagReflowEngineReady in DeviceStatusFlagsHandle.

Initialise singleton state and signal module readiness.

#### `ReflowStart`

```c
bool ReflowStart( const char * name )
```

Load a profile by name and begin execution.

Load a profile by name and signal ReflowProcess() to begin execution.

Resolves name via the ReflowProfile module, falling back to the hardcoded default if the named file is not found. Starts the OvenController and sets FlagReflowInProgress. Clears all transient status flags before starting.

Falls back to the hardcoded default if name is NULL or the file is not found. Sets FlagReflowStart; ReflowProcess() picks it up on the next tick and owns the cycle initialisation from there.

| Parameter | Description |
|-----------|-------------|
| `name` | Profile filename or full path. Pass NULL to use the default. |

**Returns:** True if the profile was accepted and execution has started.

#### `ReflowProcess`

```c
void ReflowProcess( void )
```

Execute one iteration of the reflow state machine.

Advance the reflow state machine by one iteration.

Called from ReflowTask in a continuous loop. Blocks on FlagReflowInProgress when idle. When a cycle is active, advances the stage machine, checks hold conditions, and watches FlagReflowStop and FlagSystemAborted between ticks.

Execute one iteration of the reflow state machine.

Blocks on FlagReflowRunning when idle. When running, checks hold conditions, updates fan speed, and advances stages. Delays 200 ms per tick so the OC regulation loop runs freely.

---

## ReflowProfile

*Reflow profile data structures and filesystem I/O.*

### Types

#### `ReflowFn`

Optional special action triggered when this stage enters its hold phase.

| Value | Description |
|-------|-------------|
| `ReflowFnNone` | Normal hold — time or temperature criterion only. |
| `ReflowFnCalFan` | Run AC fan calibration sequence during hold. |
| `ReflowFnCalThermal` | Run thermal calibration sequence during hold. |

#### `ReflowStage`

A single stage within a reflow profile — a heap-allocated linked list node.

| Type | Field | Description |
|------|-------|-------------|
| `char` | `name` | Human-readable stage label; empty string = no ty seen. |
| `Temperature` | `targetTemp` | Target temperature in milli-°C. |
| `RampRate` | `rampRate` | Ramp rate in milli-°C/s. Positive = heat, negative = cool. |
| `DurationMs` | `timeoutMs` | Maximum ms to wait for target temperature. 0 = no timeout. |
| `DurationMs` | `holdMs` | Milliseconds to hold once target temperature is reached. |
| `Permille` | `fanSpeed` | Target oven fan speed for this stage (0–1000). |
| `DurationMs` | `accelTimeMs` | Time to ramp from the previous fan speed to fanSpeed. 0 = instant. |
| `bool` | `heaterTop` | Enable top heating element (tag h0=). |
| `bool` | `heaterRear` | Enable rear convection element (tag h1=). |
| `bool` | `heaterBottom` | Enable bottom heating element (tag h2=). |
| `ReflowFn` | `function` | Action invoked once at hold entry, before the hold timer starts. ReflowFnNone for no action. |
| `struct ReflowStage *` | `next` | Next stage in profile, or NULL if last. |

#### `ReflowProfile`

A complete reflow profile — a heap-allocated linked list of stages.

| Type | Field | Description |
|------|-------|-------------|
| `ReflowStagePtr` | `stages` | Head of stage linked list; NULL = empty. |

### Functions

#### `ReflowProfileLoad`

```c
ReflowProfilePtr ReflowProfileLoad( const char * name )
```

Load a profile by name, or build the built-in default if not found.

Load a named profile from flash, or return the built-in default.

Allocates the profile struct and all stage nodes from the heap. Falls back to the built-in lead-free default if name is NULL or the file is absent. The caller owns the result and must call ReflowProfileFree() when done.

| Parameter | Description |
|-----------|-------------|
| `name` | Profile filename or full path. NULL → built-in default. |

**Returns:** Heap-allocated profile, or NULL on allocation failure.

#### `ReflowProfileFree`

```c
void ReflowProfileFree( ReflowProfilePtr profile )
```

Free a heap-allocated profile and all its stage nodes.

Walk the stage list and free every node, then the profile itself.

Safe to call with NULL. After returning, the pointer is invalid.

| Parameter | Description |
|-----------|-------------|
| `profile` | Profile returned by ReflowProfileLoad(). |

#### `ReflowProfileSave`

```c
bool ReflowProfileSave( ReflowProfilePtr profile, const char * name )
```

Save a profile to the filesystem.

Serialise a profile to tagged CSV and write it to flash.

name may be a bare filename or a full pathname. Bare filenames are written to the profile storage directory.

| Parameter | Description |
|-----------|-------------|
| `profile` | Profile to serialise. |
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
| `FlagSPIStatusOverrun` | RX overrun (OVR). |
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

Start an asynchronous interrupt-driven SPI read.

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

Start an asynchronous interrupt-driven SPI write.

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

Start an atomic asynchronous write-then-read without CS toggling between phases.

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

Perform a synchronous blocking SPI write.

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

Perform a synchronous blocking SPI read.

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
| `FlagSystemInitialised` | All modules are initialised; tasks may proceed. |
| `FlagInterruptsEnabled` | GPIO edge interrupts have been unmasked. |
| `FlagSupervisorServiceRequest` | ManagerTask: a supervisor action is requested. |
| `FlagSystemAborted` | Software interrupt has fired; system is aborting. |
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

### Functions

#### `WaitForFlags`

```c
bool WaitForFlags( osEventFlagsId_t handle, uint32_t bits, uint32_t ms )
```

Wait up to ms for any bit in bits to be set in handle, without clearing.

Standardises the osFlagsWaitAny | osFlagsNoClear pattern used throughout the codebase. Any task may call this to block on any event flag group.

| Parameter | Description |
|-----------|-------------|
| `handle` | CMSIS-RTOS2 event flag group to wait on. |
| `bits` | Bitmask of flag bits to watch (any bit fires the return). |
| `ms` | Maximum wait time in milliseconds; use osWaitForever to block indefinitely. |

**Returns:** True if any watched bit was set before the timeout.

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
| `FlagTriacStatusActive` | Power is requested (manual ON or burst window ON). |
| `FlagTriacStatusGateOpen` | Physical GPIO is LOW (gate is conducting). |
| `FlagTriacStatusZCDLost` | AC line sync lost (ZCD watchdog timeout). |
| `FlagTriacStatusConfigError` | Invalid parameters detected in TriacRun(). |
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
| `uint16_t` | `phaseDelayUs` | Delay from ZCD to gate fire (0 = full power, up to 10000 µs). |
| `uint8_t` | `burstOn` | Number of half-cycles 'On' within the burst window. |
| `uint8_t` | `burstWindow` | Total window size in half-cycles (burstOn + burstOff). |

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

Configure a TRIAC for phase-angle / burst-fire control.

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

Task-loop tick: check the ZCD watchdog and raise faults on AC loss.

If more than 50 ms have elapsed since the last ZCD interrupt (equivalent to 5 missed zero-crosses at 50 Hz), sets FlagTriacStatusZCDLost on all channels and raises FlagTriacFault in FaultFlagsHandle.

#### `ZCDHandler`

```c
void ZCDHandler( uint16_t GPIO_Pin )
```

Zero-cross rising-edge ISR handler — drives the burst sequencer and primes TIM16.

Zero-cross ISR handler — drives the burst sequencer and arms TIM16.

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

