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
ACFanRef ACFanOpen( ACFanID id, TriacID triacID, RotaryEncoderID encoderID )
```

Open a handle to a specific AC fan instance.

On first call for a given ID: opens the TRIAC and encoder handles internally, then attempts to load the calibration profile from flash. Sets FlagACFanStatusReady on success or FlagACFanCalibrationRequired if no valid profile is found. Subsequent calls with the same ID return the existing instance without re-opening.

| Parameter | Description |
|-----------|-------------|
| `id` | Fan instance identifier. |
| `triacID` | TRIAC channel identifier for the oven fan. |
| `encoderID` | Rotary encoder identifier for the fan shaft. |

**Returns:** Handle to the instance, or NULL if id is out of range.

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

**Returns:** Cached speed in RPM; 0 if fan is NULL.

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

---

## ACFanTuning

*AC fan calibration and runtime drive engine — public API.*

### Types

#### `ACFanDriveParams`

Internal TRIAC drive parameter set.

| Type | Field | Description |
|------|-------|-------------|
| `uint16_t` | `phaseDelayUs` | TRIAC phase-angle delay in microseconds (0 = full power). |
| `uint8_t` | `burstOn` | Number of mains half-cycles per burst that fire the gate. |
| `uint8_t` | `burstWindow` | Total burst window size in half-cycles (on + off). |

#### `ACFanProfileSlot`

A single calibrated speed step in the profile map.

| Type | Field | Description |
|------|-------|-------------|
| `bool` | `isFeasible` | False if no valid drive strategy was found for this step. |
| `ACFanDriveParams` | `strategy` | Drive parameters to apply to achieve this speed step. |
| `Rpm` | `actualRPM` | Measured RPM achieved by strategy during calibration. |

#### `ACFanProfileMap`

Full calibration output produced by ACFanRunCalibration().

| Type | Field | Description |
|------|-------|-------------|
| `ACFanProfileSlot` | `slots` | Calibrated slot array. |
| `Rpm` | `motorMaxRPM` | Unloaded maximum RPM measured at calibration start. |

### Functions

#### `ACFanInitCalibration`

```c
void ACFanInitCalibration( void )
```

Initialise the AC fan tuning module.

Acquires the TRIAC channel and rotary encoder handles used internally. Must be called once before ACFanRunCalibration() or ACFanDrive().

Acquires the TRIAC channel (AC_FAN_TRIAC_ID) and the rotary encoder handle. Must be called once before ACFanRunCalibration() or ACFanDrive().

#### `ACFanRunCalibration`

```c
void ACFanRunCalibration( ACFanProfileMapPtr mapOut )
```

Run the full calibration sequence and populate mapOut.

Executes a two-phase grid search (coarse + fine) for each of the kAcFanNumSteps speed targets, measuring RPM and stress at each candidate TRIAC drive point. The result is written into mapOut.

| Parameter | Description |
|-----------|-------------|
| `mapOut` | Caller-allocated profile map to populate. |

#### `ACFanDrive`

```c
void ACFanDrive( const ACFanProfileMapPtr map, Permille requestedPm )
```

Drive the fan at the requested speed using a calibrated profile map.

Drive the fan at requestedPm permille of motorMaxRPM using a calibrated map.

Looks up the two neighbouring profile slots that bracket requestedPm, linearly interpolates all three drive parameters, and applies the result via TriacRun().

The profile map divides the speed range into kAcFanNumSteps equal bands. Requests exactly on a band boundary are applied directly. Requests between two boundaries are linearly interpolated across all three drive parameters (phaseDelayUs, burstOn, burstWindow).

Special case: if one neighbouring slot has burstOn=0 (motor off) and the other does not, interpolation would produce meaningless fractional values. The function snaps to whichever slot is nearer instead.

Guard: after interpolation, burstOn is clamped to burstWindow. Independent interpolation of both fields can produce a rounding inconsistency where burstOn > burstWindow, which the TRIAC driver would reject.

| Parameter | Description |
|-----------|-------------|
| `map` | A previously populated ACFanProfileMap. |
| `requestedPm` | Desired speed in permille of motorMaxRPM (0–1000). |

---

## ACLight

*Oven interior light driver.*

### Types

#### `ACLightStatusBit`

Status and diagnostic flag bit positions for the AC light instance.

| Value | Description |
|-------|-------------|
| `FlagACLightStatusReady` | TRIAC channel opened and ready. |
| `FlagACLightStatusOn` | Power > 0 is currently applied. |
| `FlagACLightStatusHardwareFault` | TRIAC configuration error detected. |
| `FlagACLightPowerPending` | Power command queued; not yet applied to TRIAC. |
| `ACLightFlagsCount` |  |

### Functions

#### `ACLightInitModule`

```c
void ACLightInitModule( void )
```

Allocate instance resources. Does not access hardware.

#### `ACLightOpen`

```c
ACLightRef ACLightOpen( void )
```

Open the AC light and acquire its TRIAC channel.

On first call, opens the TRIAC channel and sets FlagACLightStatusReady plus the global DeviceStatusFlagsHandle ready bit. Subsequent calls return the existing instance without re-opening.

**Returns:** Handle to the instance, or NULL on internal error.

#### `ACLightSetPower`

```c
void ACLightSetPower( ACLightRef light, Permille power )
```

Queue a power request; applied to the TRIAC by ACLightProcess() on the next tick.

Power is clamped to [0, 1000]. Ignored if FlagACLightStatusReady is not set.

| Parameter | Description |
|-----------|-------------|
| `light` | Handle returned by ACLightOpen(). |
| `power` | Desired power in permille (0 = off, 1000 = full). |

#### `ACLightGetStatus`

```c
uint32_t ACLightGetStatus( ACLightRef light )
```

Return the full status bitmask for the light instance.

| Parameter | Description |
|-----------|-------------|
| `light` | Handle returned by ACLightOpen(). |

**Returns:** Bitmask of ACLightStatusBit flags; BIT(FlagACLightStatusHardwareFault) if light is NULL.

#### `ACLightProcess`

```c
void ACLightProcess( void )
```

Apply any pending power command and update status flags.

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
PayloadPtr HandlerOvenStatus( APIPBPtr pb )
```

Return current oven status.

Report the current oven run state and active profile.

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

Start a reflow run using the profile specified in the request payload.

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

Stop the current reflow run gracefully.

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

Trigger an immediate emergency stop.

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

Enter manual control mode, allowing direct heater and fan commands.

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

Exit manual control mode and return to safe idle state.

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

Set heater power level in manual control mode.

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

Set cooling fan speed in manual control mode.

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

Return current temperature readings from all sensors.

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

Return mains voltage and ZCD status.

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

Return a list of stored reflow profiles.

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

Return a single profile identified by the request payload.

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

Create a new reflow profile from the request payload.

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

Update an existing profile with data from the request payload.

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

Delete the profile identified by the request payload.

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

Return the device configuration.

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

Replace the device configuration with the request payload.

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

Return a list of stored log files.

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

Return the contents of a single log file.

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

Delete a single log file identified by the request payload.

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

Return file system status and free space.

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

Format the storage partition.

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

Return system status flags, uptime, and firmware version.

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

Return the current RTC date and time.

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

Set the RTC date and time from the request payload.

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

Perform a software reset of the MCU.

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

Return USB PD contract details and live voltage/current readings.

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

Set the status indicator light state from the request payload.

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

Play a buzzer melody or tone specified in the request payload.

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
| `const char *` | `pattern` | sscanf-compatible pattern, e.g. "GET /profiles/s". |
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

### Types

#### `FSGeometry`

Physical geometry of a block device.

| Type | Field | Description |
|------|-------|-------------|
| `uint32_t` | `blockSize` | Bytes per erasable block (e.g. 4096). |
| `uint32_t` | `blockCount` | Total number of blocks on the device. |
| `uint32_t` | `readSize` | Minimum readable unit in bytes (typically 1). |
| `uint32_t` | `progSize` | Minimum programmable unit in bytes (e.g. 256). |

#### `BDOps`

Block device operations vtable.

| Type | Field | Description |
|------|-------|-------------|
| `FSResult(*` | `read` | Read size bytes from block at byte offset off into buf. |
| `FSResult(*` | `prog` | Program size bytes from buf into block at byte offset off. |
| `FSResult(*` | `erase` | Erase block entirely, setting all bytes to 0xFF. |
| `FSResult(*` | `sync` | Flush any internal cache and confirm the device is idle. |

### Functions

#### `BDRegister`

```c
BDRef BDRegister( const BDOps * ops, void * context, FSGeometry geometry )
```

Register a block device and return an opaque handle.

Stores a reference to ops (must remain valid for the lifetime of the block device), the device-private context, and the physical geometry. Returns NULL if the device pool is full.

| Parameter | Description |
|-----------|-------------|
| `ops` | Pointer to a statically allocated BDOps vtable. |
| `context` | Device-private state (e.g. a FlashRef). |
| `geometry` | Physical geometry of the device. |

**Returns:** Handle, or NULL if the device pool is exhausted.

#### `BDGetGeometry`

```c
FSGeometry BDGetGeometry( BDRef bd )
```

Return the geometry of a registered block device.

| Parameter | Description |
|-----------|-------------|
| `bd` | Handle returned by BDRegister(). |

#### `BDGetContext`

```c
void * BDGetContext( BDRef bd )
```

Return the device-private context pointer.

| Parameter | Description |
|-----------|-------------|
| `bd` | Handle returned by BDRegister(). |

#### `BDRead`

```c
FSResult BDRead( BDRef bd, uint32_t block, uint32_t off, void * buf, uint32_t size )
```

#### `BDProg`

```c
FSResult BDProg( BDRef bd, uint32_t block, uint32_t off, const void * buf, uint32_t size )
```

#### `BDErase`

```c
FSResult BDErase( BDRef bd, uint32_t block )
```

#### `BDSync`

```c
FSResult BDSync( BDRef bd )
```

---

## Buzzer

*Piezo buzzer driver with melodic sequencer.*

### Types

#### `BuzzerFrequency`

Full chromatic scale for the buzzer's optimal frequency range (7th–8th octave). Frequencies are in Hz. The suffix 's' denotes Sharp (#).

| Value | Description |
|-------|-------------|
| `NoteRest` | Silent rest (no gate pulses). |
| `NoteC7` |  |
| `NoteCs7` |  |
| `NoteD7` |  |
| `NoteDs7` |  |
| `NoteE7` |  |
| `NoteF7` |  |
| `NoteFs7` |  |
| `NoteG7` |  |
| `NoteGs7` |  |
| `NoteA7` |  |
| `NoteAs7` |  |
| `NoteB7` |  |
| `NoteC8` |  |
| `NoteCs8` |  |
| `NoteD8` |  |
| `NoteDs8` |  |
| `NoteE8` |  |

#### `BuzzerPattern`

Identifiers for the pre-defined system notification melodies.

| Value | Description |
|-------|-------------|
| `BuzzerPatternPowerOn` | Three-note ascending tone played at startup. |
| `BuzzerPatternSuccess` | Two-note ascending confirmation tone. |
| `BuzzerPatternError` | Double low-frequency error beep. |
| `BuzzerPatternCritical` | Rapid high-frequency double pulse. |
| `BuzzerPatternLevelComplete` | Extended celebratory melody. |

#### `BuzzerStatusBit`

Fault and status flag bit positions for the buzzer module. These map 1:1 to the bits in the private buzzerStatus event flag group.

| Value | Description |
|-------|-------------|
| `FlagBuzzerStatusReady` | Hardware / timer initialised. |
| `FlagBuzzerStatusActive` | Currently playing a note or melody. |
| `FlagBuzzerStatusMuted` | Software mute is active. |
| `FlagBuzzerStatusHardwareFault` | Placeholder for future diagnostic hardware. |
| `BuzzerFlagsCount` |  |

#### `BuzzerTone`

A single note in a melody: a frequency and a hold duration.

| Type | Field | Description |
|------|-------|-------------|
| `BuzzerFrequency` | `tone` | Note frequency (use NoteRest for silence). |
| `uint16_t` | `durationMs` | Duration to hold the note in milliseconds. |

#### `Melody`

A melodic sequence with a fixed-length note array.

| Type | Field | Description |
|------|-------|-------------|
| `uint8_t` | `length` | Actual number of notes in the sequence. |
| `BuzzerTone` | `tones` | Flexible array member for the tone sequence. |

### Functions

#### `BuzzerInitModule`

```c
void BuzzerInitModule( void )
```

Initialise the timer peripheral and GPIO for PWM output and prime the sequencer.

Initialise the timer peripheral and GPIO, create the private status flags group.

Initialise the timer peripheral and GPIO for PWM output and prime the sequencer.

Resets the sequencer state, creates the buzzerStatus event group, configures the BUZZER_EN_N GPIO to its idle-high (off) state, and registers the TIM7 period-elapsed callback. Signals DeviceStatusFlagsHandle on completion.

#### `BuzzerStart`

```c
void BuzzerStart( BuzzerFrequency frequency )
```

Start the buzzer immediately at the specified note frequency.

Start the buzzer at a specific frequency, bypassing the sequencer.

Configures TIM7 for the requested frequency and starts it. The active flag is set atomically inside a critical section.

| Parameter | Description |
|-----------|-------------|
| `frequency` | Frequency in Hz; pass NoteRest to silence without stopping the timer. |

#### `BuzzerStop`

```c
void BuzzerStop( void )
```

Immediately stop the PWM signal and de-assert the output GPIO.

Immediately stop the PWM signal and de-assert the BUZZER_EN_N GPIO.

Immediately stop the PWM signal and de-assert the output GPIO.

The active status flag is cleared atomically inside a critical section.

#### `BuzzerPlay`

```c
void BuzzerPlay( const BuzzerPattern pattern )
```

Queue a pre-defined melodic pattern for asynchronous playback.

Queue a pre-defined melodic pattern for asynchronous ISR-driven playback.

| Parameter | Description |
|-----------|-------------|
| `pattern` | Pattern identifier from BuzzerPattern. |

#### `BuzzerPlayMelody`

```c
void BuzzerPlayMelody( const Melody * melody )
```

Queue a custom melody sequence for asynchronous playback.

Queue a custom melody for asynchronous ISR-driven playback.

| Parameter | Description |
|-----------|-------------|
| `melody` | Pointer to a Melody struct with at least one tone entry. |

#### `BuzzerGetStatus`

```c
uint32_t BuzzerGetStatus( void )
```

Return the current bitmask from the internal buzzer status flags.

Return the current status bitmask from the private buzzerStatus flags.

**Returns:** Bitmask of BuzzerStatusBit flags; safe to call from any task context.

#### `BuzzerProcess`

```c
void BuzzerProcess( void )
```

Task-loop tick for the buzzer module (currently a no-op; sequencing is ISR-driven).

Task-loop tick for the buzzer module.

Task-loop tick for the buzzer module (currently a no-op; sequencing is ISR-driven).

---

## DCFan

*EMC2101 DC fan controller driver.*

### Types

#### `DCFanID`

Logical identifiers for DC fan channels managed by this driver.

| Value | Description |
|-------|-------------|
| `BoardCoolingFan` | Onboard EMC2101-controlled cooling fan. |
| `DCFanCount` |  |

#### `DCFanStatusBit`

Status, diagnostic, and internal phase flag bit positions for the DC fan module. These map 1:1 to the bits in the per-instance statusHandle event flag group.

| Value | Description |
|-------|-------------|
| `FlagDCFanStatusReady` | Fan controller initialised and communicating. |
| `FlagDCFanStatusSpinning` | Tachometer confirms rotation above threshold. |
| `FlagDCFanStatusStall` | Speed > 0 was requested but measured RPM is ~0. |
| `FlagDCFanStatusNoTach` | Open circuit or failed tachometer sensor. |
| `FlagDCFanStatusUnderSpeed` | Measured RPM significantly below requested target. |
| `FlagDCFanStatusOverTemp` | EMC2101 internal or external temperature over limit. |
| `FlagDCFanStatusHardwareFault` | I2C communication failure with the EMC2101. |
| `FlagDCFanSpeedPending` | Speed command queued; not yet written to hardware. |
| `FlagDCFanIODone` | Most recent async I2C read completed successfully. |
| `FlagDCFanIOError` | Most recent async I2C read failed. |
| `FlagDCFanPhaseReadIntTemp` | Async internal-temperature read in progress. |
| `FlagDCFanPhaseReadExtTemp` | Async external-temperature read in progress. |
| `FlagDCFanPhaseReadTach` | Async tachometer read in progress. |
| `FlagDCFanPhaseProcessing` | All reads complete; evaluating thresholds this tick. |
| `DCFanFlagsCount` |  |

### Functions

#### `DCFanInitModule`

```c
void DCFanInitModule( void )
```

Allocate per-instance resources. Does not access I2C hardware.

#### `DCFanOpen`

```c
DCFanRef DCFanOpen( DCFanID fanID, I2CRef i2c )
```

Open a handle to the specified fan channel and configure the EMC2101.

On first call for a given ID: stores i2c, checks mains presence, writes the fan configuration register, and signals DeviceStatusFlagsHandle on success. Subsequent calls with the same ID return the existing instance without re-configuring.

| Parameter | Description |
|-----------|-------------|
| `fanID` | Fan channel identifier. |
| `i2c` | I2C bus handle returned by I2COpen(). |

**Returns:** Handle to the fan instance, or NULL if fanID is out of range.

#### `DCFanSetSpeed`

```c
void DCFanSetSpeed( DCFanRef fan, Permille speed )
```

Queue a fan speed request; the I2C write is applied by DCFanProcess() on the next tick.

Queue a fan speed update; the I2C write is applied by DCFanProcess() on the next tick.

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by DCFanOpen(). |
| `speed` | Target duty cycle in permille (0 = off, 1000 = 100%). |

#### `DCFanGetSpeed`

```c
Rpm DCFanGetSpeed( DCFanRef fan )
```

Return the most recently measured fan speed.

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by DCFanOpen(). |

**Returns:** Current speed in RPM; 0 if fan is NULL.

#### `DCFanGetInternalTemp`

```c
Temperature DCFanGetInternalTemp( DCFanRef fan )
```

Return the EMC2101 internal die temperature.

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by DCFanOpen(). |

**Returns:** Temperature in milli-degrees Celsius; 0 if fan is NULL.

#### `DCFanGetExternalTemp`

```c
Temperature DCFanGetExternalTemp( DCFanRef fan )
```

Return the EMC2101 external (remote) thermistor temperature.

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by DCFanOpen(). |

**Returns:** Temperature in milli-degrees Celsius; 0 if fan is NULL.

#### `DCFanGetStatus`

```c
uint32_t DCFanGetStatus( DCFanRef fan )
```

Return the full status bitmask for this fan instance.

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by DCFanOpen(). |

**Returns:** Bitmask of DCFanStatusBit flags; BIT(FlagDCFanStatusHardwareFault) if fan is NULL.

#### `DCFanCalibrate`

```c
void DCFanCalibrate( DCFanRef fan )
```

Run a calibration sweep (factory use; not called during normal operation).

| Parameter | Description |
|-----------|-------------|
| `fan` | Handle returned by DCFanOpen(). |

#### `DCFanProcess`

```c
void DCFanProcess( void )
```

Drive the I2C state machine, apply pending speed commands, and update status flags.

---

## DeviceTask

*Device task — periodic driver process loop.*

### Functions

#### `DeviceTaskInit`

```c
void DeviceTaskInit( void )
```

Initialise the Device task — waits for system initialisation signal.

Initialise the Device task — waits for system initialisation before proceeding.

Blocks on FlagSystemInitialised in SystemStatusFlagsHandle before returning. Called once by app_freertos.c at startup.

Initialise the Device task — waits for system initialisation signal.

Blocks indefinitely on FlagSystemInitialised so that no driver Process() functions run until all drivers have been initialised by ManagerTask.

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

### Functions

#### `FileOpen`

```c
FileRef FileOpen( const char * path, FSOpenFlags flags, FSUid uid )
```

Open a file by absolute path.

Resolves the path to the appropriate mounted volume, checks caller permissions, and opens the underlying filesystem file. If FSOpenCreate is set and the file does not exist, it is created with FSModeDefault and owner uid. Returns NULL on error.

| Parameter | Description |
|-----------|-------------|
| `path` | Absolute path (e.g. "/system/SysConfig.ini"). |
| `flags` | Access mode and option flags. |
| `uid` | Caller's UID. UID 0 bypasses permission checks. |

**Returns:** File handle, or NULL on error.

#### `FileClose`

```c
FSResult FileClose( FileRef file )
```

Close an open file, flushing any pending writes.

| Parameter | Description |
|-----------|-------------|
| `file` | Handle returned by FileOpen(). |

**Returns:** FSResultOk on success.

#### `FileRead`

```c
FSResult FileRead( FileRef file, void * buf, size_t size, size_t * read )
```

Read up to size bytes from the current file position.

| Parameter | Description |
|-----------|-------------|
| `file` | Handle returned by FileOpen(). |
| `buf` | Destination buffer; must be at least size bytes. |
| `size` | Maximum bytes to read. |
| `read` | Set to the number of bytes actually read; 0 at end of file. |

**Returns:** FSResultOk, or an error code. A short read is not an error.

#### `FileWrite`

```c
FSResult FileWrite( FileRef file, const void * buf, size_t size, size_t * written )
```

Write size bytes to the current file position.

| Parameter | Description |
|-----------|-------------|
| `file` | Handle returned by FileOpen(). |
| `buf` | Source buffer; must be at least size bytes. |
| `size` | Number of bytes to write. |
| `written` | Set to the number of bytes actually written. |

**Returns:** FSResultOk, or an error code.

#### `FileSeek`

```c
FSResult FileSeek( FileRef file, int32_t offset, FSSeekOrigin origin, uint32_t * pos )
```

Move the file position.

| Parameter | Description |
|-----------|-------------|
| `file` | Handle returned by FileOpen(). |
| `offset` | Byte offset relative to origin. |
| `origin` | One of FSSeekSet, FSSeekCur, FSSeekEnd. |
| `pos` | Set to the resulting absolute byte position. |

**Returns:** FSResultOk, or FSResultInvalid if the resulting position is negative.

#### `FileTell`

```c
FSResult FileTell( FileRef file, uint32_t * pos )
```

Return the current byte position within a file.

| Parameter | Description |
|-----------|-------------|
| `file` | Handle returned by FileOpen(). |
| `pos` | Set to the current absolute position. |

**Returns:** FSResultOk.

#### `FileStat`

```c
FSResult FileStat( const char * path, FSUid uid, FSStatPtr stat )
```

Return file or directory metadata without opening the file.

| Parameter | Description |
|-----------|-------------|
| `path` | Absolute path. |
| `uid` | Caller's UID. |
| `stat` | Struct to populate. |

**Returns:** FSResultOk, FSResultNotFound, or FSResultPermission.

#### `FileDelete`

```c
FSResult FileDelete( const char * path, FSUid uid )
```

Delete a file or empty directory.

| Parameter | Description |
|-----------|-------------|
| `path` | Absolute path. |
| `uid` | Caller's UID. Only the owner (or UID 0) may delete. |

**Returns:** FSResultOk, FSResultPermission, FSResultNotFound, or FSResultNotEmpty.

#### `FileMkdir`

```c
FSResult FileMkdir( const char * path, FSUid uid, FSMode mode )
```

Create a directory.

| Parameter | Description |
|-----------|-------------|
| `path` | Absolute path of the new directory. |
| `uid` | Caller's UID (becomes the directory owner). |
| `mode` | Permission mode for the new directory. |

**Returns:** FSResultOk, FSResultExists, FSResultPermission, or FSResultNoSpace.

#### `FileReadAsync`

```c
FSResult FileReadAsync( FileRef file, void * buf, size_t size, FSCallback callback, void * userData )
```

Asynchronous variant of FileRead().

Returns immediately. callback is invoked from task context on completion. buf must remain valid until the callback fires.

| Parameter | Description |
|-----------|-------------|
| `file` | Handle returned by FileOpen(). |
| `buf` | Destination buffer. |
| `size` | Maximum bytes to read. |
| `callback` | Completion callback. |
| `userData` | Passed unchanged to callback. |

**Returns:** FSResultOk if the operation was accepted, or an error if the handle is invalid or a previous async operation is still in flight.

#### `FileWriteAsync`

```c
FSResult FileWriteAsync( FileRef file, const void * buf, size_t size, FSCallback callback, void * userData )
```

Asynchronous variant of FileWrite().

Returns immediately. callback is invoked from task context on completion. buf must remain valid until the callback fires.

| Parameter | Description |
|-----------|-------------|
| `file` | Handle returned by FileOpen(). |
| `buf` | Source buffer. |
| `size` | Number of bytes to write. |
| `callback` | Completion callback. |
| `userData` | Passed unchanged to callback. |

**Returns:** FSResultOk if the operation was accepted, or an error if the handle is invalid or a previous async operation is still in flight.

#### `FileGetLastError`

```c
FSResult FileGetLastError( FileRef file )
```

Return the last error recorded on a file handle.

| Parameter | Description |
|-----------|-------------|
| `file` | Handle returned by FileOpen(). |

**Returns:** The most recent non-OK FSResult, or FSResultOk if none.

---

## FSInternal

*Package-private declarations shared within the FileSystem module.*

### Functions

#### `FSMapLFSError`

```c
FSResult FSMapLFSError( int lfsErr )
```

Map a LittleFS negative error code to the equivalent FSResult.

#### `VolGetLFS`

```c
lfs_t * VolGetLFS( VolRef vol )
```

Return the lfs_t instance for a mounted volume.

Used by FSFile.c to call lfs_file_* functions without exposing the lfs_t type in the public Volume.h header.

| Parameter | Description |
|-----------|-------------|
| `vol` | Handle returned by VolMount(). |

**Returns:** Pointer to the internal lfs_t, or NULL if vol is NULL or unmounted.

#### `VolGetOpenFileCount`

```c
uint8_t VolGetOpenFileCount( VolRef vol )
```

Return the open-file reference count for a volume.

Used by VolUnmount() to refuse to unmount while files are still open. Maintained exclusively by FSFile.c.

#### `VolIncrementOpenFiles`

```c
void VolIncrementOpenFiles( VolRef vol )
```

Increment the open-file reference count for a volume.

#### `VolDecrementOpenFiles`

```c
void VolDecrementOpenFiles( VolRef vol )
```

Decrement the open-file reference count for a volume.

#### `VolResolve`

```c
VolRef VolResolve( const char * absPath, const char ** relPath )
```

Resolve an absolute path to the volume that owns it.

Matches the leading path component against mounted volume mount points. On success, relPath is set to the remainder of the path after the mount point prefix (never starts with '/').

| Parameter | Description |
|-----------|-------------|
| `absPath` | Absolute path, e.g. "/system/SysConfig.ini". |
| `relPath` | Set to the volume-relative path on success. |

**Returns:** Mounted volume handle, or NULL if no mounted volume matches.

---

## FSTypes

*Shared types, error codes, and callback signature for the FileSystem subsystem.*

### Types

#### `FSResult`

Result codes returned by all FileSystem operations.

| Value | Description |
|-------|-------------|
| `FSResultOk` | Success. |
| `FSResultIO` | Hardware I/O error. |
| `FSResultCorrupt` | Filesystem data is corrupted. |
| `FSResultNotFound` | File or directory not found. |
| `FSResultExists` | File or directory already exists. |
| `FSResultNotDir` | Path component is not a directory. |
| `FSResultIsDir` | Target is a directory, not a file. |
| `FSResultNotEmpty` | Directory is not empty. |
| `FSResultNoSpace` | No space remaining on the volume. |
| `FSResultInvalid` | Invalid argument. |
| `FSResultBusy` | Resource is busy (e.g. volume has open files). |
| `FSResultNoMemory` | Static resource pool exhausted. |
| `FSResultPermission` | Caller lacks the required permission. |
| `FSResultNotMounted` | Volume is not currently mounted. |
| `FSResultNotReady` | Block device has not been initialised. |

#### `FSOpenFlags`

File open flags. Combine access mode with option flags using bitwise OR.

| Value | Description |
|-------|-------------|
| `FSOpenReadOnly` | Open for reading only. |
| `FSOpenWriteOnly` | Open for writing only. |
| `FSOpenReadWrite` | Open for reading and writing. |
| `FSOpenCreate` | Create the file if it does not exist. |
| `FSOpenTruncate` | Truncate the file to zero length on open. |
| `FSOpenAppend` | Move to end of file before every write. |

#### `FSSeekOrigin`

Seek origin, matching POSIX whence constants.

| Value | Description |
|-------|-------------|
| `FSSeekSet` | Offset is from the start of the file. |
| `FSSeekCur` | Offset is from the current position. |
| `FSSeekEnd` | Offset is from the end of the file. |

#### `FSStat`

File or directory metadata returned by FileStat().

| Type | Field | Description |
|------|-------|-------------|
| `uint32_t` | `size` | File size in bytes (0 for directories). |
| `FSMode` | `mode` | Permission bits. |
| `FSUid` | `uid` | Owner UID. |
| `bool` | `isDir` | True if this entry is a directory. |

---

## Flash

*External NOR flash driver public interface — MX25L51245GZ2I-10G.*

### Types

#### `FlashID`

Logical identifiers for NOR flash devices managed by this driver.

| Value | Description |
|-------|-------------|
| `Flash1` | Primary NOR flash — MX25L51245GZ2I-10G on SPI1. |
| `FlashCount` |  |

#### `FlashStatusBit`

Diagnostic flag bit positions for the flash driver status event group. Returned by FlashGetStatus(). Maps 1:1 to bits in the private per-instance event flags.

| Value | Description |
|-------|-------------|
| `FlagFlashStatusReady` | JEDEC verified and 4-byte mode entered. |
| `FlagFlashStatusBusy` | Device WIP bit is currently set. |
| `FlagFlashStatusJEDECError` | JEDEC ID did not match MX25L51245G. |
| `FlagFlashStatusSPIError` | SPI transfer failure. |
| `FlagFlashStatusTimeout` | WIP poll timed out. |
| `FlashStatusFlagsCount` | Must stay <= 24. |

### Functions

#### `FlashOpen`

```c
FlashRef FlashOpen( FlashID id, SPIRef spi )
```

Open a handle to a flash device and perform one-time hardware initialisation.

Verifies the JEDEC ID, enters 4-byte address mode, and sets the Ready flag. On first call for a given ID the device is configured; subsequent calls with the same ID return the existing instance without re-initialising.

Sets FlagFlashReady in DeviceStatusFlagsHandle on success, or FlagFlashFault in FaultFlagsHandle on JEDEC mismatch or SPI error.

| Parameter | Description |
|-----------|-------------|
| `id` | Flash device identifier. |
| `spi` | SPI bus handle returned by SPIOpen(). |

**Returns:** Handle to the instance; always non-NULL (faults are in status flags).

#### `FlashGetRef`

```c
FlashRef FlashGetRef( FlashID id )
```

Return a handle to a previously opened flash device without re-initialising.

| Parameter | Description |
|-----------|-------------|
| `id` | Flash device identifier. |

**Returns:** Handle, or NULL if id has not been opened yet.

#### `FlashRead`

```c
bool FlashRead( FlashRef flash, uint32_t addr, void * buf, uint32_t len )
```

Read len bytes from byte address addr into buf.

Read len bytes from byte address addr.

No alignment restriction on addr or len.

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |
| `addr` | Absolute byte address (0 to NOR_TOTAL_SIZE - 1). |
| `buf` | Destination buffer; must be at least len bytes. |
| `len` | Number of bytes to read. |

**Returns:** true on success, false on SPI error.

#### `FlashProgram`

```c
bool FlashProgram( FlashRef flash, uint32_t addr, const void * buf, uint32_t len )
```

Program up to NOR_PAGE_SIZE bytes starting at addr.

Program up to kPageSize bytes at page-aligned address addr.

addr must be page-aligned (multiple of NOR_PAGE_SIZE) and len must not exceed NOR_PAGE_SIZE. The sector containing addr must be erased before calling. Blocks until the hardware write cycle completes.

Sends Write Enable, transmits the page program command with address and data in a single CS-asserted burst, then polls WIP until the device is idle.

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |
| `addr` | Page-aligned byte address. |
| `buf` | Source buffer; must be at least len bytes. |
| `len` | Number of bytes to program (1 to NOR_PAGE_SIZE). |

**Returns:** true on success, false on SPI error or program timeout.

#### `FlashEraseSector`

```c
bool FlashEraseSector( FlashRef flash, uint32_t addr )
```

Erase the 4 KB sector that contains addr.

Erase the 4 KB sector containing addr (must be sector-aligned).

addr must be sector-aligned (multiple of NOR_SECTOR_SIZE). Blocks until erase completes (up to ~400 ms).

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |
| `addr` | Sector-aligned byte address. |

**Returns:** true on success, false on SPI error or erase timeout.

#### `FlashEraseChip`

```c
bool FlashEraseChip( FlashRef flash )
```

Erase the entire chip — all bytes set to 0xFF.

Erase the entire chip.

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |

**Returns:** true on success, false on SPI error or erase timeout.

#### `FlashSync`

```c
bool FlashSync( FlashRef flash )
```

Confirm that no write or erase operation is in progress.

Verify the device is not busy — used as the LittleFS sync callback.

Used as the LittleFS sync callback.

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |

**Returns:** true if the device is idle, false on SPI error or timeout.

#### `FlashGetPageSize`

```c
uint32_t FlashGetPageSize( FlashRef flash )
```

Return the page size in bytes for this flash device.

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |

**Returns:** Page size in bytes; 0 if flash is NULL.

#### `FlashGetSectorSize`

```c
uint32_t FlashGetSectorSize( FlashRef flash )
```

Return the sector size in bytes for this flash device.

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |

**Returns:** Sector size in bytes; 0 if flash is NULL.

#### `FlashGetTotalSize`

```c
uint32_t FlashGetTotalSize( FlashRef flash )
```

Return the total capacity in bytes for this flash device.

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |

**Returns:** Total size in bytes; 0 if flash is NULL.

#### `FlashGetSectorCount`

```c
uint32_t FlashGetSectorCount( FlashRef flash )
```

Return the number of erasable sectors for this flash device.

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |

**Returns:** Sector count; 0 if flash is NULL.

#### `FlashGetStatus`

```c
uint32_t FlashGetStatus( FlashRef flash )
```

Return the raw flash status event flag bits for diagnostics and system snapshots.

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |

**Returns:** Bitmask of FlashStatusBit values; 0 if flash is NULL.

---

## FlashBlockDevice

*NOR flash concrete block device adapter for the FileSystem subsystem.*

### Functions

#### `FBDRegister`

```c
BDRef FBDRegister( FlashRef flash )
```

Register the NOR flash chip as a block device.

Wraps the Flash.c read / program / erase / sync functions in a BDOps vtable and derives the geometry from the flash driver's own accessors. The returned BDRef is the entry point for PartInitModule().

| Parameter | Description |
|-----------|-------------|
| `flash` | Handle returned by FlashOpen(). |

**Returns:** Block device handle, or NULL if flash is NULL or the device pool is full.

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

*STM32G0 MCU peripheral driver (internal ADC, RTC, power monitoring).*

### Types

#### `MCUID`

Logical identifiers for MCU instances managed by this driver.

| Value | Description |
|-------|-------------|
| `MCU0` | STM32G0 on-chip peripherals. |
| `MCUCount` |  |

#### `MCUStatusBit`

Status and diagnostic flag bit positions for the MCU module. These map 1:1 to the bits in the private per-instance status event flag group.

| Value | Description |
|-------|-------------|
| `FlagMCUStatusReady` | MCU peripherals initialised. |
| `FlagMCUStatusLowBattery` | Battery level below 15%. |
| `FlagMCUStatusCriticalBattery` | Battery level below 5%. |
| `FlagMCUStatusOverTemp` | Internal junction temperature above 80°C. |
| `FlagMCUStatusVoltageUnstable` | VCC outside the expected 3.0–3.6 V range. |
| `FlagMCUStatusRtcInvalid` | RTC set or read failed. |
| `FlagMCUStatusAdcError` | ADC DMA peripheral error. |
| `FlagMCUStatusTimePending` | RTC time write queued; not yet applied by MCUProcess(). |
| `MCUFlagsCount` |  |

#### `MCUTime`

Wall-clock time structure used for RTC get/set operations.

| Type | Field | Description |
|------|-------|-------------|
| `uint8_t` | `Hours` | Hour of the day (0–23). |
| `uint8_t` | `Minutes` | Minutes (0–59). |
| `uint8_t` | `Seconds` | Seconds (0–59). |
| `uint8_t` | `Day` | Day of the month (1–31). |
| `uint8_t` | `Month` | Month (1–12). |
| `uint16_t` | `Year` | Full year (e.g. 2026). |

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

| Parameter | Description |
|-----------|-------------|
| `id` | MCU instance identifier. |

**Returns:** Handle to the instance, or NULL if id is out of range.

#### `MCUGetVcc`

```c
Voltage MCUGetVcc( MCURef mcu )
```

Return the supply voltage computed from the VREFINT ADC channel.

Compute VCC from the VREFINT ADC reading using the factory calibration constant.

| Parameter | Description |
|-----------|-------------|
| `mcu` | Handle returned by MCUOpen(). |

**Returns:** VCC in millivolts (e.g. 3300 = 3.3 V); returns the last cached value on zero input.

#### `MCUGetInternalTemp`

```c
Temperature MCUGetInternalTemp( MCURef mcu )
```

Return the MCU junction temperature computed from the internal ADC channel.

Compute the internal junction temperature from the temperature-sensor ADC channel.

| Parameter | Description |
|-----------|-------------|
| `mcu` | Handle returned by MCUOpen(). |

**Returns:** Temperature in milli-degrees Celsius; returns the last cached value if VCC is zero.

#### `MCUGetBatteryVoltage`

```c
Voltage MCUGetBatteryVoltage( MCURef mcu )
```

Return the battery voltage from the VBAT ADC channel.

Compute the battery voltage from the VBAT ADC channel.

| Parameter | Description |
|-----------|-------------|
| `mcu` | Handle returned by MCUOpen(). |

**Returns:** Battery voltage in millivolts; uses the last cached VCC for the scaling factor.

#### `MCUGetBatteryLevel`

```c
Permille MCUGetBatteryLevel( MCURef mcu )
```

Return the battery charge level as a permille value.

Compute the battery charge level as a permille fraction.

| Parameter | Description |
|-----------|-------------|
| `mcu` | Handle returned by MCUOpen(). |

**Returns:** 0 (empty, ≤3.0 V) to 1000 (full, ≥4.2 V).

#### `MCUGetStatus`

```c
uint32_t MCUGetStatus( MCURef mcu )
```

Return the full status bitmask from the private instance status flags.

| Parameter | Description |
|-----------|-------------|
| `mcu` | Handle returned by MCUOpen(). |

**Returns:** Bitmask of MCUStatusBit flags, or 0 if mcu is NULL.

#### `MCUGetTime`

```c
void MCUGetTime( MCURef mcu, MCUTimePtr time )
```

Read the current wall-clock time from the RTC.

Read the current wall-clock time from the RTC hardware registers.

| Parameter | Description |
|-----------|-------------|
| `mcu` | Handle returned by MCUOpen(). |
| `time` | Pointer to an MCUTime struct to populate. |

#### `MCUSetTime`

```c
void MCUSetTime( MCURef mcu, const MCUTimePtr time )
```

Queue a wall-clock time update; applied by MCUProcess() on the next tick.

Queue a wall-clock time update for deferred application by MCUProcess().

Copies time into pendingTime inside a critical section to ensure the multi-field write is atomic with respect to the task scheduler.

| Parameter | Description |
|-----------|-------------|
| `mcu` | Handle returned by MCUOpen(). |
| `time` | Pointer to the new time to apply. |

#### `MCUProcess`

```c
void MCUProcess( void )
```

Refresh cached ADC readings, apply pending RTC writes, and update status flags.

Refresh cached ADC readings, apply any pending RTC write, and update status flags.

Evaluates VCC, temperature, and battery level thresholds and maps them to status flag bits. Applies a pending time set if one was queued by MCUSetTime(). Propagates active faults to FaultFlagsHandle.

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

Phase 1 — bus managers: SPIInitModule() and I2CInitModule() create bus semaphores. Phase 2 — bus open: SPIOpen() and I2COpen() return refs used by all peripheral drivers. Phase 3 — alloc-only InitModule() calls: create per-driver event flag groups and assign static GPIO/pin mappings; no hardware I/O at this stage. Phase 4 — device Open() calls: bind bus refs, write hardware config registers, and set per-device Ready bits in DeviceStatusFlagsHandle. Phase 5 — waits for DEVICE_ALL_READY, enables interrupts, and broadcasts FlagSystemInitialised.

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
| `uint8_t` | `thermistor` | Include the top-cavity thermistor in temperature averaging. |
| `uint8_t` | `tc1` | Include TC1 (board surface) in temperature averaging. |
| `uint8_t` | `tc2` | Include TC2 (free air at board level) in temperature averaging. |
| `uint8_t` | `resources` | All resource bits as a single byte; 0xFF enables all. |
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

### Types

#### `FSPartType`

Filesystem type for a partition entry.

| Value | Description |
|-------|-------------|
| `FSPartLittleFS` | LittleFS filesystem. |
| `FSPartRaw` | Raw byte-addressable storage (no filesystem layer). |
| `FSPartReserved` | Reserved block — not to be mounted or exposed. |

#### `FSPartFlags`

Partition flag bits. Combine with bitwise OR.

| Value | Description |
|-------|-------------|
| `FSPartFlagReadOnly` | Volume always mounts read-only. |
| `FSPartFlagUsbVisible` | Exposed to USB mass storage host. |
| `FSPartFlagHidden` | Not enumerated to User (non-System) contexts. |

#### `FSPartEntry`

A single partition table entry.

| Type | Field | Description |
|------|-------|-------------|
| `char` | `name` | Null-terminated partition name. |
| `uint32_t` | `startBlock` | Index of the first block of this partition. |
| `uint32_t` | `blockCount` | Number of blocks in this partition. |
| `uint8_t` | `type` | FSPartType. |
| `uint8_t` | `flags` | FSPartFlags bitmask. |
| `FSUid` | `uid` | Owner UID. |
| `uint8_t` | `reserved` | Explicit padding — must be 0. |
| `FSMode` | `mode` | Unix-style permission mode. |
| `uint8_t` | `pad` | Align struct to 32 bytes. |

### Functions

#### `PartInitModule`

```c
FSResult PartInitModule( BDRef bd )
```

Initialise the partition subsystem against a block device.

Reads block 0 of bd. If a valid partition table (correct magic, version, and checksum) is found it is loaded into the in-memory table. Otherwise the compiled-in default layout is written to block 0 and used. Must be called before any other Partition function.

| Parameter | Description |
|-----------|-------------|
| `bd` | Block device handle returned by BDRegister(). |

**Returns:** FSResultOk on success, or FSResultIO if block 0 is unreadable.

#### `PartGetCount`

```c
uint8_t PartGetCount( void )
```

Return the number of valid partition entries currently loaded.

#### `PartGetByIndex`

```c
PartRef PartGetByIndex( uint8_t index )
```

Return a handle to the partition at the given zero-based index.

**Returns:** Handle, or NULL if index is out of range.

#### `PartGetByName`

```c
PartRef PartGetByName( const char * name )
```

Return a handle to the first partition with the given name.

**Returns:** Handle, or NULL if no partition with that name exists.

#### `PartGetEntry`

```c
FSResult PartGetEntry( PartRef part, FSPartEntryPtr out )
```

Copy the entry descriptor for a partition into out.

| Parameter | Description |
|-----------|-------------|
| `part` | Handle returned by PartGetByIndex() or PartGetByName(). |
| `out` | Struct to populate. |

**Returns:** FSResultOk, or FSResultInvalid if part or out is NULL.

#### `PartGetDevice`

```c
BDRef PartGetDevice( PartRef part )
```

Return the block device on which this partition resides.

| Parameter | Description |
|-----------|-------------|
| `part` | Handle returned by PartGetByIndex() or PartGetByName(). |

**Returns:** BDRef, or NULL if part is NULL.

#### `PartCommit`

```c
FSResult PartCommit( void )
```

Write the in-memory partition table back to block 0 of the device.

Erases block 0, then programs the full table record (header + entries + checksum). Should be called after any modification to the partition layout.

**Returns:** FSResultOk on success, or FSResultIO on erase/program failure.

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

## RotaryEncoder

*AS5600 magnetic rotary encoder driver for oven fan speed measurement.*

### Types

#### `RotaryEncoderID`

Logical identifiers for rotary encoder instances managed by this driver.

| Value | Description |
|-------|-------------|
| `RotaryEncoder1` | AS5600 encoder on the oven fan shaft. |
| `RotaryEncoderCount` |  |

#### `REStatusBit`

Status and diagnostic flag bit positions for the rotary encoder / oven fan module. These map 1:1 to the bits in the private ovenFanStatus event flag group.

| Value | Description |
|-------|-------------|
| `FlagREStatusReady` | Encoder communicating and providing valid data. |
| `FlagREStatusSpinning` | Absolute velocity above RE_SPINNING_THRESHOLD_RPM. |
| `FlagREStatusStall` | Reserved — not currently set by the driver. |
| `FlagREStatusMagWeak` | AS5600 reports magnet too far (AGC high). |
| `FlagREStatusMagStrong` | AS5600 reports magnet too close (AGC low). |
| `FlagREStatusMagMissing` | AS5600 MD bit clear — no magnet detected. |
| `FlagREStatusHardwareFault` | I2C communication failure. |
| `FlagREIODone` | Most recent async I2C read completed successfully. |
| `FlagREIOError` | Most recent async I2C read failed. |
| `REFlagsCount` |  |

### Functions

#### `REInitModule`

```c
void REInitModule( void )
```

Allocate per-instance resources and initialise the tick counter.

Allocate the status event flag group and reset internal state.

Allocate per-instance resources and initialise the tick counter.

#### `REOpen`

```c
RotaryEncoderRef REOpen( RotaryEncoderID id, I2CRef i2c )
```

Open a handle to a specific encoder instance.

On first call for a given ID: stores i2c in the instance. Subsequent calls with the same ID return the existing instance without re-configuring.

| Parameter | Description |
|-----------|-------------|
| `id` | Encoder identifier. |
| `i2c` | I2C bus handle returned by I2COpen(). |

**Returns:** Handle to the instance, or NULL if id is out of range.

#### `REGetRef`

```c
RotaryEncoderRef REGetRef( RotaryEncoderID id )
```

Return a handle to a previously opened encoder instance without re-initialising.

| Parameter | Description |
|-----------|-------------|
| `id` | Encoder identifier. |

**Returns:** Handle, or NULL if id has not been opened yet or is out of range.

#### `REGetVelocity`

```c
Rpm REGetVelocity( RotaryEncoderRef encoder )
```

Return the most recently computed rotational velocity.

Return the most recently computed velocity.

| Parameter | Description |
|-----------|-------------|
| `encoder` | Handle returned by REOpen(). |

**Returns:** Velocity in RPM (signed; positive = forward). Returns 0 if encoder is NULL.

#### `REGetStatus`

```c
uint32_t REGetStatus( void )
```

Return the full status bitmask from the private ovenFanStatus flags.

**Returns:** Bitmask of REStatusBit flags; safe to call from any task context.

#### `REProcess`

```c
void REProcess( void )
```

Drive the I2C state machine, update velocity, and update status flags.

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
| `FlagSystemInitialised` | All drivers are ready; tasks may proceed. |
| `FlagInterruptsEnabled` | GPIO edge interrupts have been unmasked. |
| `FlagSupervisorServiceRequest` | ManagerTask: a supervisor action is requested. |
| `SystemFlagsCount` | Number of flags — must stay <= 24. |

#### `DeviceFlagsBit`

Per-device ready flags, stored in DeviceStatusFlagsHandle.

| Value | Description |
|-------|-------------|
| `FlagMCUReady` | MCU peripheral init complete. |
| `FlagUSBPDReady` | USB-PD controller init complete. |
| `FlagUSBPDContractReady` | USB-PD power contract negotiated. |
| `FlagThermocouple1Ready` | MAX31856 channel 1 ready. |
| `FlagThermocouple2Ready` | MAX31856 channel 2 ready. |
| `FlagThermistorCJT1Ready` | Cold-junction thermistor 1 (MCP3221) ready. |
| `FlagThermistorCJT2Ready` | Cold-junction thermistor 2 (MCP3221) ready. |
| `FlagThermistorOvenReady` | Oven cavity thermistor (MCP3221) ready. |
| `FlagThermistorHeatsinkReady` | Heatsink thermistor (EMC2101 I2C) ready. |
| `FlagPowerManagerReady` | Power manager init complete. |
| `FlagBuzzerReady` | Buzzer driver init complete. |
| `FlagOvenFanReady` | Oven fan (AC) driver ready. |
| `FlagOvenFanSpinning` | Oven fan rotor is currently rotating. |
| `FlagBoardFanReady` | Board cooling fan (DC, EMC2101) ready. |
| `FlagTriacReady` | TRIAC phase-angle driver ready. |
| `FlagFlashReady` | External NOR flash init and verified. |
| `FlagHeaterTopReady` | Top heating element TRIAC channel ready. |
| `FlagHeaterRearReady` | Rear heating element TRIAC channel ready. |
| `FlagHeaterBottomReady` | Bottom heating element TRIAC channel ready. |
| `FlagOvenLightReady` | Oven interior light TRIAC channel ready. |
| `FlagOvenControllerReady` | Oven controller initialised and all device refs valid. |
| `DeviceFlagsCount` | Number of flags — must stay <= 24. |

#### `ReflowStatusBit`

Reflow profile execution phase flags, stored in ReflowStatusFlagsHandle.

| Value | Description |
|-------|-------------|
| `FlagReflowInProgress` | A reflow cycle is actively running. |
| `FlagReflowHeating` | Oven is in the preheat/ramp-up phase. |
| `FlagReflowSoaking` | Oven is in the thermal soak phase. |
| `FlagReflowCooling` | Oven is in the forced-cool phase. |
| `FlagReflowDone` | Reflow cycle completed successfully. |
| `ReflowFlagsCount` | Number of flags — must stay <= 24. |

#### `FaultFlagsBit`

Active fault flags, stored in FaultFlagsHandle.

| Value | Description |
|-------|-------------|
| `FlagESTOP` | Emergency stop was triggered. |
| `FlagMCUFault` | MCU peripheral or watchdog fault. |
| `FlagPowerManagerFault` | Power supply out of range. |
| `FlagUSBPDFault` | USB-PD negotiation or hardware fault. |
| `FlagThermocouple1Fault` | Thermocouple 1 open-circuit or CRC error. |
| `FlagThermocouple2Fault` | Thermocouple 2 open-circuit or CRC error. |
| `FlagThermistorCJT1Fault` | Cold-junction thermistor 1 read failure. |
| `FlagThermistorCJT2Fault` | Cold-junction thermistor 2 read failure. |
| `FlagThermistorOvenFault` | Oven cavity thermistor read failure. |
| `FlagThermistorHeatsinkFault` | Heatsink thermistor read failure. |
| `FlagOvenFanFault` | Oven fan stall or speed error. |
| `FlagBoardFanFault` | Board fan stall or speed error. |
| `FlagBuzzerFault` | Buzzer hardware fault. |
| `FlagReflowFault` | Reflow profile logic error. |
| `FlagI2CFault` | I2C bus error or timeout. |
| `FlagSPIFault` | SPI bus error or timeout. |
| `FlagTriacFault` | TRIAC gate or ZCD fault. |
| `FlagFlashFault` | NOR flash SPI error or JEDEC ID mismatch. |
| `FlagHeaterTopFault` | Top heating element TRIAC configuration error. |
| `FlagHeaterRearFault` | Rear heating element TRIAC configuration error. |
| `FlagHeaterBottomFault` | Bottom heating element TRIAC configuration error. |
| `FlagOvenLightFault` | Oven interior light TRIAC configuration error. |
| `FlagOvenControllerFault` | Oven controller regulation fault or required sensor failure. |
| `FaultFlagsCount` | Number of flags — must stay <= 24. |

---

## TaskUtils

*FreeRTOS / bare-metal portability layer.*

---

## Thermistor

*NTC thermistor driver using the STM32 internal ADC DMA buffer.*

### Types

#### `TMStatusBit`

Status and diagnostic flag bit positions for an NTC thermistor channel. These map 1:1 to the bits in each instance's private event flag group.

| Value | Description |
|-------|-------------|
| `FlagTMStatusReady` | Module initialised and ADC DMA is running. |
| `FlagTMStatusLowTemp` | Temperature below the plausible range (< −30°C). |
| `FlagTMStatusHighTemp` | Temperature above the safety limit (> 280°C). |
| `FlagTMStatusOpenCircuit` | ADC value near the supply rail — probe disconnected. |
| `FlagTMStatusShortCircuit` | ADC value near ground — probe shorted. |
| `FlagTMStatusHardwareFault` | Internal ADC or DMA peripheral error. |
| `TMFlagsCount` |  |

#### `ThermistorID`

Logical identifiers for the three NTC thermistor channels.

| Value | Description |
|-------|-------------|
| `ThermistorCJT1` | Cold-junction thermistor for Thermocouple1. |
| `ThermistorCJT2` | Cold-junction thermistor for Thermocouple2. |
| `ThermistorOven` | Oven cavity NTC (inverted divider network). |
| `ThermistorCount` |  |

### Functions

#### `TMInitModule`

```c
void TMInitModule( void )
```

Initialise the NTC module, create per-instance flag groups, and start the ADC DMA.

Initialise the NTC module, create per-instance flag groups, and start ADC DMA.

Initialise the NTC module, create per-instance flag groups, and start the ADC DMA.

Each instance gets its own osEventFlagsId_t created via osEventFlagsNew(). The ADC calibration is run and DMA conversion is started. DeviceStatusFlagsHandle signals are intentionally not set here — thermistors are internal CJT sensors that are considered implicitly ready when ADC DMA is running.

#### `TMOpen`

```c
ThermistorRef TMOpen( ThermistorID thermistorID )
```

Open a handle to a specific NTC thermistor channel.

Open a handle to a specific thermistor channel.

| Parameter | Description |
|-----------|-------------|
| `thermistorID` | Channel identifier. |

**Returns:** Handle to the instance, or NULL if thermistorID is out of range.

#### `TMGetTemperature`

```c
Temperature TMGetTemperature( ThermistorRef thermistor )
```

Return the interpolated temperature for a thermistor channel.

Interpolate the temperature from the current ADC DMA reading.

Reads the current ADC DMA value and interpolates between two lookup table entries using the fractional position within the 128-count step.

| Parameter | Description |
|-----------|-------------|
| `thermistor` | Handle returned by TMOpen(). |

**Returns:** Temperature in milli-degrees Celsius; 0 if thermistor is NULL.

#### `TMGetStatus`

```c
uint32_t TMGetStatus( ThermistorRef thermistor )
```

Return the current status bitmask for a specific thermistor instance.

| Parameter | Description |
|-----------|-------------|
| `thermistor` | Handle returned by TMOpen(). |

**Returns:** Bitmask of TMStatusBit flags; 0 if thermistor is NULL.

#### `TMProcess`

```c
void TMProcess( void )
```

Process all thermistor channels: evaluate fault thresholds and update status flags.

Evaluate fault thresholds for all channels and update status and global fault flags.

Reads each channel's current ADC value, checks open/short circuit margins and temperature plausibility limits, and maps results to per-instance status flags. Propagates channel-specific faults to FaultFlagsHandle.

For each instance: checks the ADC state for hardware error, evaluates open/short circuit margins (note the polarity is inverted for the oven channel), checks temperature plausibility limits, and sets/clears the appropriate per-instance flags. Propagates any active fault to the FaultFlagsHandle global bus.

---

## ThermistorI2C

*MCP3221 I2C ADC heatsink thermistor driver.*

### Types

#### `ThermistorI2CID`

Logical identifiers for I2C thermistor instances managed by this driver.

| Value | Description |
|-------|-------------|
| `ThermistorI2C1` | MCP3221 heatsink thermistor. |
| `ThermistorI2CCount` |  |

#### `TMI2CStatusBit`

Status and diagnostic flag bit positions for the I2C heatsink thermistor. These map 1:1 to the bits in the private per-instance status event flag group.

| Value | Description |
|-------|-------------|
| `FlagTMI2CStatusReady` | Module initialised and I2C communication successful. |
| `FlagTMI2CStatusOverTemp` | Heatsink temperature exceeds the 95°C safety limit. |
| `FlagTMI2CStatusOpenCircuit` | ADC reading near maximum — thermistor probe disconnected. |
| `FlagTMI2CStatusShortCircuit` | ADC reading near zero — thermistor probe shorted. |
| `FlagTMI2CStatusHardwareFault` | I2C communication failure (NACK or timeout). |
| `FlagTMI2CIODone` | Most recent async I2C read completed successfully. |
| `FlagTMI2CIOError` | Most recent async I2C read failed. |
| `TMI2CFlagsCount` |  |

### Functions

#### `TMI2CInitModule`

```c
void TMI2CInitModule( void )
```

Allocate per-instance resources. Does not access I2C hardware.

#### `TMI2COpen`

```c
ThermistorI2CRef TMI2COpen( ThermistorI2CID id, I2CRef i2c, NTCEntryPtr ntcTable )
```

Open a handle to a specific thermistor instance.

On first call for a given ID: stores i2c and ntcTable in the instance. Subsequent calls with the same ID return the existing instance without re-configuring.

| Parameter | Description |
|-----------|-------------|
| `id` | Thermistor instance identifier. |
| `i2c` | I2C bus handle returned by I2COpen(). |
| `ntcTable` | 33-entry NTC lookup table (128 ADC counts/step), or NULL for linear approx. |

**Returns:** Handle to the instance, or NULL if id is out of range.

#### `TMI2CGetTemperature`

```c
Temperature TMI2CGetTemperature( ThermistorI2CRef thermistor )
```

Return the most recently computed heatsink temperature.

Compute heatsink temperature from the latest raw ADC value.

| Parameter | Description |
|-----------|-------------|
| `thermistor` | Handle returned by TMI2COpen(). |

**Returns:** Temperature in milli-degrees Celsius; 0 if thermistor is NULL.

#### `TMI2CGetRaw`

```c
AdcRaw TMI2CGetRaw( ThermistorI2CRef thermistor )
```

Return the most recently read raw 12-bit ADC value from the MCP3221.

Return the most recently read raw 12-bit ADC value.

| Parameter | Description |
|-----------|-------------|
| `thermistor` | Handle returned by TMI2COpen(). |

**Returns:** Raw ADC value (0–4095); 0 if thermistor is NULL.

#### `TMI2CGetStatus`

```c
uint32_t TMI2CGetStatus( ThermistorI2CRef thermistor )
```

Return the full status bitmask for this thermistor instance.

| Parameter | Description |
|-----------|-------------|
| `thermistor` | Handle returned by TMI2COpen(). |

**Returns:** Bitmask of TMI2CStatusBit flags; safe to call from any task context.

#### `TMI2CProcess`

```c
void TMI2CProcess( void )
```

Drive the I2C state machine, evaluate fault thresholds, and update status flags.

Error recovery: on I2C failure, sets FaultFlagsHandle and FlagTMI2CStatusHardwareFault, then resets the state machine to Idle.

---

## Thermocouple

*MAX31856 dual thermocouple driver.*

### Types

#### `ThermocoupleID`

Identifiers for the two thermocouple channels.

| Value | Description |
|-------|-------------|
| `Thermocouple1` | Primary thermocouple (inside oven, top position). |
| `Thermocouple2` | Secondary thermocouple (inside oven, bottom position). |

#### `ThermocoupleStatusBit`

Status and fault flag bit positions for a MAX31856 channel.

| Value | Description |
|-------|-------------|
| `FlagTCStatusOK` | No fault conditions active. |
| `FlagTCStatusDataReady` | DRDY pin asserted; new conversion result available. |
| `FlagTCStatusOpenCircuit` | MAX31856 OC fault bit — thermocouple wire is broken. |
| `FlagTCStatusShortToGND` | MAX31856 SCG fault bit — thermocouple shorted to ground. |
| `FlagTCStatusShortToVCC` | MAX31856 SCV fault bit — thermocouple shorted to supply. |
| `FlagTCStatusCJTRangeLow` | Cold junction temperature below the configured lower limit. |
| `FlagTCStatusCJTRangeHigh` | Cold junction temperature above the configured upper limit. |
| `FlagTCStatusRangeLow` | Thermocouple temperature below the configured lower limit. |
| `FlagTCStatusRangeHigh` | Thermocouple temperature above the configured upper limit. |
| `FlagTCStatusCJTMismatch` | Injected CJT differs significantly from internal measurement. |
| `FlagTCStatusHardwareFault` | SPI communication failure or internal IC error. |
| `FlagTCStatusSamplePending` | CJT injection and conversion cycle queued; not yet applied. |
| `FlagTCStatusFaultPending` | FAULT ISR fired; status register not yet read by TCProcess(). |
| `TCFlagsCount` |  |

### Functions

#### `TCInitModule`

```c
void TCInitModule( void )
```

Initialise both thermocouple instances, configure the MAX31856 registers over SPI.

Allocate per-instance resources and assign static GPIO/pin mappings.

Creates per-instance private event flag groups via osEventFlagsNew(). Configures Type-K thermocouple mode, auto-conversion, and open-circuit detection.

Initialise both thermocouple instances, configure the MAX31856 registers over SPI.

Creates per-instance private event flag groups (osEventFlagsNew) and records the GPIO/pin configuration from CubeMX-generated defines. Does NOT access SPI hardware — that happens in TCOpen() once a bus reference is available. Safe to call before SPIInitModule() runs.

#### `TCOpen`

```c
ThermocoupleRef TCOpen( ThermocoupleID thermocoupleID, SPIRef spi )
```

Open a handle to a specific thermocouple instance and configure the MAX31856.

Open a handle to a thermocouple instance and configure the MAX31856 hardware.

On first call for a given ID: stores the SPI bus reference, writes CR0/CR1 to the MAX31856 hardware, resolves the CJT thermistor reference, and signals DeviceStatusFlagsHandle. Subsequent calls with the same ID return the existing instance without re-configuring hardware.

On first call for a given ID: stores spi, writes CR0/CR1 to the MAX31856, resolves the CJT thermistor reference, and signals DeviceStatusFlagsHandle. Subsequent calls with the same ID return the existing instance without re-configuring.

| Parameter | Description |
|-----------|-------------|
| `thermocoupleID` | Channel identifier (Thermocouple1 or Thermocouple2). |
| `spi` | SPI bus handle returned by SPIOpen(). |

**Returns:** Handle to the instance.

#### `TCRequestSample`

```c
void TCRequestSample( ThermocoupleRef tc )
```

Signal that a new conversion should begin on the next TCProcess() tick.

Signal that a new sample cycle should begin; hardware I/O happens in TCProcess().

| Parameter | Description |
|-----------|-------------|
| `tc` | Handle returned by TCOpen(). |

#### `TCIsReady`

```c
bool TCIsReady( ThermocoupleRef tc )
```

Return true if a new sample has been decoded since the last call or flag clear.

Return true if a new sample has been decoded since the last check.

| Parameter | Description |
|-----------|-------------|
| `tc` | Handle returned by TCOpen(). |

**Returns:** true if FlagTCStatusDataReady is set in the instance status flags.

#### `TCGetTemperature`

```c
Temperature TCGetTemperature( ThermocoupleRef tc )
```

Return the most recently decoded thermocouple temperature.

| Parameter | Description |
|-----------|-------------|
| `tc` | Handle returned by TCOpen(). |

**Returns:** Temperature in milli-degrees Celsius; 0 if tc is NULL.

#### `TCGetCJT`

```c
Temperature TCGetCJT( ThermocoupleRef tc )
```

Return the cold-junction temperature for this thermocouple instance.

Return the cold-junction temperature for this instance.

Delegates to TMGetTemperature() on the associated external NTC thermistor.

| Parameter | Description |
|-----------|-------------|
| `tc` | Handle returned by TCOpen(). |

**Returns:** CJT in milli-degrees Celsius; 0 if either tc or its CJT reference is NULL.

#### `TCGetStatus`

```c
uint32_t TCGetStatus( ThermocoupleRef tc )
```

Return the full status bitmask for this thermocouple instance.

| Parameter | Description |
|-----------|-------------|
| `tc` | Handle returned by TCOpen(). |

**Returns:** Bitmask of ThermocoupleStatusBit flags; returns FlagTCStatusHardwareFault if NULL.

#### `TCProcess`

```c
void TCProcess( void )
```

Service both thermocouple instances: inject CJT, decode temperature, read fault register.

Iterates both instances each call. For each instance:
If a sample was requested, writes the CJT to the MAX31856 via SPI.
If DRDY is set (by ISR), reads and decodes the temperature registers.
If a fault is pending (set by ISR), reads the status register.
Propagates hardware faults to FaultFlagsHandle.

#### `TCHandleDRDYInterrupt`

```c
void TCHandleDRDYInterrupt( uint16_t GPIO_Pin )
```

ISR handler for the DRDY pin — sets FlagTCStatusDataReady in the matching instance.

DRDY rising-edge ISR handler — sets FlagTCStatusDataReady on the matching instance.

| Parameter | Description |
|-----------|-------------|
| `GPIO_Pin` | HAL pin mask; compared against each instance's drdyPin. |

#### `TCHandleFaultInterrupt`

```c
void TCHandleFaultInterrupt( uint16_t GPIO_Pin )
```

ISR handler for the FAULT pin — sets FlagTCStatusFaultPending in the matching instance.

FAULT rising-edge ISR handler — sets FlagTCStatusFaultPending on the matching instance.

| Parameter | Description |
|-----------|-------------|
| `GPIO_Pin` | HAL pin mask; compared against each instance's faultPin. |

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

Logical identifiers for the five AC load channels.

| Value | Description |
|-------|-------------|
| `TriacHeaterTop` | Top heating element. |
| `TriacHeaterRear` | Rear heating element. |
| `TriacHeaterBottom` | Bottom heating element. |
| `TriacOvenFan` | AC oven circulation fan. |
| `TriacLight` | Oven interior light. |
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

### Functions

#### `USBPDTaskInit`

```c
void USBPDTaskInit( void )
```

Waits for FlagSystemInitialised before allowing the process loop to run.

Waits for system initialisation before starting the PD process loop.

Waits for FlagSystemInitialised before allowing the process loop to run.

#### `USBPDTaskLoop`

```c
void USBPDTaskLoop( void )
```

Call USBPDProcess() every 10 ms.

Drive the PD policy engine at a fixed 10 ms tick.

Call USBPDProcess() every 10 ms.

---

## USBPowerDelivery

*USB Power Delivery driver for STPD01 buck converter and TCPP03 protection IC.*

### Types

#### `USBPDID`

Logical identifiers for USBPD instances managed by this driver.

| Value | Description |
|-------|-------------|
| `USBPD1` | USB-C port with STPD01 + TCPP03. |
| `USBPDCount` |  |

#### `USBPDStatusBit`

Diagnostic flag bit positions for the USBPD driver status event group.

| Value | Description |
|-------|-------------|
| `FlagUSBPDModuleReady` | InitModule completed successfully. |
| `FlagUSBPDConnected` | A USB-C cable or device is present. |
| `FlagUSBPDContractActive` | PD contract established; power is flowing. |
| `FlagUSBPDRoleSource` | Port is operating as a Source (0 = Sink when connected). |
| `FlagUSBPDFaultDetected` | OVP, short, or PD protocol fault is active. |
| `FlagUSBPDVoltagePending` | A voltage change request is queued for the next tick. |
| `FlagUSBPDSourceFaultPending` | STPD01 fault ISR fired; I2C autopsy pending in USBPDProcess(). |
| `USBPDStatusFlagsCount` | Must stay <= 24. |

#### `USBPDPowerProfile`

Power profile entry describing a single voltage/current capability.

| Type | Field | Description |
|------|-------|-------------|
| `Voltage` | `voltage` | Profile voltage in millivolts. |
| `Current` | `maxCurrent` | Maximum current in milliamps. |
| `Power` | `maxPower` | Maximum power in milliwatts. |

### Functions

#### `USBPDInitModule`

```c
void USBPDInitModule( void )
```

Allocate per-instance resources and enable the UCPD peripheral. Does not access I2C hardware — that happens in USBPDOpen().

Allocate per-instance resources and enable the UCPD peripheral.

Allocate per-instance resources and enable the UCPD peripheral. Does not access I2C hardware — that happens in USBPDOpen().

Creates per-instance status event flag groups, enables the UCPD1 peripheral, and de-asserts the PD_SRC_PON pin (source output off). Does not access I2C hardware — that happens in USBPDOpen() once a bus reference is available.

#### `USBPDOpen`

```c
USBPDRef USBPDOpen( USBPDID id, I2CRef i2c )
```

Open a handle to a specific USBPD instance and configure the TCPP03.

On first call for a given ID: stores i2c, writes the TCPP03 configuration and OVP threshold, and signals DeviceStatusFlagsHandle. Subsequent calls with the same ID return the existing instance without re-configuring.

| Parameter | Description |
|-----------|-------------|
| `id` | USBPD instance identifier. |
| `i2c` | I2C bus handle returned by I2COpen(). |

**Returns:** Handle to the instance, or NULL if id is out of range.

#### `USBPDGetStatus`

```c
uint32_t USBPDGetStatus( USBPDRef pd )
```

Return the raw diagnostic flag bits for system snapshots and status queries.

Return the raw diagnostic flag bits for this USBPD instance.

| Parameter | Description |
|-----------|-------------|
| `pd` | Handle returned by USBPDOpen(). |

**Returns:** Bitmask of USBPDStatusBit values; 0 if pd is NULL.

#### `USBPDGetProfileCount`

```c
uint8_t USBPDGetProfileCount( USBPDRef pd )
```

Return the number of power profiles available.

Return the number of power profiles currently available.

Returns 4 when operating as a Source (locally defined profiles), or the number of profiles advertised by the partner when operating as a Sink.

| Parameter | Description |
|-----------|-------------|
| `pd` | Handle returned by USBPDOpen(). |

**Returns:** Profile count; safe to call from any task context.

#### `USBPDGetProfile`

```c
USBPDPowerProfile USBPDGetProfile( USBPDRef pd, uint8_t index )
```

Return a specific power profile by index.

| Parameter | Description |
|-----------|-------------|
| `pd` | Handle returned by USBPDOpen(). |
| `index` | Zero-based index into the profile list. |

**Returns:** The requested profile, or a zero-initialised struct if out of range.

#### `USBPDRequestVoltage`

```c
void USBPDRequestVoltage( USBPDRef pd, Voltage target )
```

Queue a voltage request; I2C writes are applied by USBPDProcess() on the next tick.

Queue a voltage request for deferred application by USBPDProcess().

Writes pendingVoltage then sets FlagUSBPDVoltagePending. Sequential execution on Cortex-M0+ guarantees the value is visible before the flag is observed.

| Parameter | Description |
|-----------|-------------|
| `pd` | Handle returned by USBPDOpen(). |
| `target` | Requested voltage in millivolts. |

#### `USBPDGetLiveVoltage`

```c
Voltage USBPDGetLiveVoltage( USBPDRef pd )
```

Return the most recently measured VBUS voltage.

| Parameter | Description |
|-----------|-------------|
| `pd` | Handle returned by USBPDOpen(). |

**Returns:** Cached VBUS voltage in millivolts; refreshed by USBPDProcess() each tick.

#### `USBPDGetLiveCurrent`

```c
Current USBPDGetLiveCurrent( USBPDRef pd )
```

Return the most recently measured VBUS current.

| Parameter | Description |
|-----------|-------------|
| `pd` | Handle returned by USBPDOpen(). |

**Returns:** Cached VBUS current in milliamps; refreshed by USBPDProcess() each tick.

#### `USBPDProcess`

```c
void USBPDProcess( void )
```

Drive the PD policy engine, apply voltage requests, and refresh telemetry.

Determines the role from the MAINS_PWR_N GPIO, handles deferred fault autopsy, applies any pending voltage request, refreshes cached VBUS telemetry, and synchronises the private diagnostic event flags.

#### `USBPDHandleFLGNInterrupt`

```c
void USBPDHandleFLGNInterrupt( uint16_t GPIO_Pin )
```

FLAG_N falling-edge ISR handler — disables the buck immediately on fault.

FLAG_N falling-edge ISR handler — disable the buck immediately on fault.

| Parameter | Description |
|-----------|-------------|
| `GPIO_Pin` | HAL pin mask (unused). |

#### `USBPDHandleSourceInterrupt`

```c
void USBPDHandleSourceInterrupt( uint16_t GPIO_Pin )
```

STPD01 source-interrupt falling-edge ISR handler — flags a fault for deferred handling.

STPD01 source-interrupt falling-edge ISR handler — flag for deferred I2C read.

| Parameter | Description |
|-----------|-------------|
| `GPIO_Pin` | HAL pin mask (unused). |

---

## Volume

*Volume (mounted filesystem instance) management.*

### Types

#### `FSMountFlags`

Options that modify how a partition is mounted.

| Value | Description |
|-------|-------------|
| `FSMountDefault` | Normal read-write mount. |
| `FSMountReadOnly` | Force read-only regardless of partition flags. |
| `FSMountNoExec` | Disallow execute permission on all files in this volume. |

#### `VolStatusBit`

Status bit positions for a mounted volume.

| Value | Description |
|-------|-------------|
| `FlagVolMounted` | Volume is currently mounted. |
| `FlagVolReadOnly` | Volume was mounted or forced read-only. |
| `FlagVolFormatted` | Partition was formatted at mount time (no prior filesystem found). |
| `FlagVolError` | Last operation encountered a filesystem error. |
| `VolStatusFlagsCount` |  |

### Functions

#### `VolMount`

```c
VolRef VolMount( PartRef part, FSMountFlags flags )
```

Mount a partition and return a volume handle.

Selects the filesystem driver based on the partition type. For LittleFS partitions: attempts lfs_mount(); on failure (no valid filesystem found) runs lfs_format() then lfs_mount(). FlagVolFormatted is set if the partition was formatted.

Mount flags are unioned with any read-only flag already set in the partition entry, so FSPartFlagReadOnly always wins.

| Parameter | Description |
|-----------|-------------|
| `part` | Partition handle returned by PartGetByIndex() or PartGetByName(). |
| `flags` | Mount options. |

**Returns:** Volume handle, or NULL if the pool is full or the mount fails.

#### `VolUnmount`

```c
FSResult VolUnmount( VolRef vol )
```

Unmount a volume, flushing all pending writes.

| Parameter | Description |
|-----------|-------------|
| `vol` | Handle returned by VolMount(). |

**Returns:** FSResultOk, or FSResultBusy if one or more files are still open.

#### `VolFormat`

```c
FSResult VolFormat( VolRef vol, FSUid uid )
```

Erase and reinitialise the filesystem on a mounted volume.

All data on the partition is permanently destroyed. Requires UID 0 (System) — FSResultPermission is returned for any other caller.

| Parameter | Description |
|-----------|-------------|
| `vol` | Handle returned by VolMount(). |
| `uid` | Caller's UID; must be 0. |

**Returns:** FSResultOk, FSResultPermission, or FSResultIO.

#### `VolGetStatus`

```c
uint32_t VolGetStatus( VolRef vol )
```

Return the status bitmask for a volume.

| Parameter | Description |
|-----------|-------------|
| `vol` | Handle returned by VolMount(). |

**Returns:** Bitmask of VolStatusBit values, or 0 if vol is NULL.

#### `VolGetPartition`

```c
PartRef VolGetPartition( VolRef vol )
```

Return the partition backing a mounted volume.

| Parameter | Description |
|-----------|-------------|
| `vol` | Handle returned by VolMount(). |

**Returns:** Partition handle, or NULL if vol is NULL.

---

