# ACFanTuning — Design Document

## Purpose

`ACFanTuning` characterises an AC fan motor driven by a TRIAC and measured by
an AS5600 magnetic rotary encoder. It produces a **tuning profile** — a
calibrated map of TRIAC drive parameters indexed by target speed — that allows
the fan to be driven at any requested percentage of its maximum RPM with
minimal mechanical and thermal stress.

The module is split into two distinct operational phases:

- **Calibration** — a one-shot offline process, typically run once after
  installation or motor replacement. It systematically explores the TRIAC
  parameter space, measures the motor's response, scores each operating point
  for stress, and selects the best parameters for each speed step. The result
  is an `ACFanProfileMap` structure that the caller persists to non-volatile
  storage.

- **Runtime drive** — a lightweight lookup and interpolation function that
  consumes a previously saved tuning profile and applies the correct TRIAC
  parameters for any requested speed. No measurement or motor characterisation
  occurs at runtime.

---

## Hardware Assumptions

| Component | Detail |
|-----------|--------|
| Microcontroller | STM32G0, running FreeRTOS (CubeMX-generated) |
| TRIAC channel | `TriacOvenFan` — phase-angle and burst-mode control via `triac.h` |
| Rotary encoder | AS5600 magnetic encoder on I2C, accessed via `rotaryencoder.h` |
| Mains supply | 50 Hz AC |
| Motor type | Single-phase induction motor, assumed 2-pole (synchronous speed 3000 RPM) |

The module opens its own handles to the TRIAC and encoder internally via
`TriacOpen()` and `REOpen()`. The caller does not need to manage these.

---

## TRIAC Drive Parameters

The TRIAC driver accepts three parameters bundled in `TriacDriveParams`:

| Parameter | Type | Description |
|-----------|------|-------------|
| `phaseDelayUs` | `uint16_t` | Delay from zero-cross detection to gate fire (microseconds). Higher values = later firing = less power per half-cycle. Range: 1000–7500 µs. |
| `burstOn` | `uint8_t` | Number of half-cycles in the burst window during which the gate fires. |
| `burstWindow` | `uint8_t` | Total size of the burst window in half-cycles. Duty cycle = burstOn / burstWindow. |

These three values together form what the calibration engine calls a **triplet**.
The parameter space of all valid triplets defines the search space that
calibration must explore.

The `ACFan_DriveParams` type in `ACFanTuning.h` is structurally identical to
`TriacDriveParams` but is owned by this module. Conversion to `TriacDriveParams`
occurs only at the point of calling `TriacRun()`, keeping the driver dependency
confined to `ACFanTuning.c`.

---

## The Tuning Profile

The tuning profile is an `ACFanProfileMap` structure. It is the sole output of
calibration and the sole input to the runtime drive function.

```c
typedef struct {
    ACFanProfileSlot slots[ AC_FAN_NUM_STEPS + 1 ];
    Rpm               motor_max_rpm;
} ACFanProfileMap;
```

The profile contains `AC_FAN_NUM_STEPS + 1` slots (11 by default). Each slot
corresponds to a speed step:

| Slot | Target speed |
|------|-------------|
| 0 | Motor off (unconditional) |
| 1 | 10% of `motor_max_rpm` |
| 2 | 20% of `motor_max_rpm` |
| ... | ... |
| 10 | 100% of `motor_max_rpm` |

Each slot records:

```c
typedef struct {
    bool              is_feasible;  // FALSE if no valid strategy was found
    ACFan_DriveParams strategy;     // TRIAC parameters to apply
    Rpm               actual_rpm;   // Measured RPM achieved
} ACFanProfileSlot;
```

The profile also records `motor_max_rpm` — the measured unloaded maximum speed
of the motor. All target RPM calculations are relative to this value, making
the profile specific to the motor and installation rather than relying on
nameplate data.

### Persistence

The caller owns the `ACFanProfileMap`. After calibration completes, the caller
is responsible for saving it to non-volatile storage (external flash, EEPROM,
or similar). At runtime, the caller loads the saved profile and passes it to
`ACFan_Drive()`.

This division of responsibility is deliberate. The calibration module has no
knowledge of the storage medium, filesystem, or wear-levelling scheme in use.
The profile is a plain C structure with no pointers and can be written and read
back with a simple `memcpy`-equivalent operation.

---

## Calibration Algorithm

Calibration is initiated by calling `ACFan_RunCalibration()` from a FreeRTOS
task. It blocks for the duration of the calibration run — typically 20–45
minutes depending on motor characteristics and how quickly stall memory prunes
the search space.

### Step 1 — Maximum RPM Measurement

The motor is run at full power (phaseDelayUs=0, burstOn=1, burstWindow=1) for
`MAX_RPM_SETTLE_MS` (5 seconds) to allow it to reach its natural ceiling. The
RPM is then sampled and stored as `motor_max_rpm`. All subsequent target RPM
values are computed as fractions of this measurement, anchoring the profile to
the actual motor rather than nominal specifications.

### Step 2 — Per-Step Search

For each of the 10 speed steps, a two-phase search finds the lowest-stress
triplet that achieves the target RPM within tolerance.

#### Previous-Winner Seeding

Before the coarse grid runs, the Phase 2 winner from the previous step is
measured against the current step's target. Neighbouring speed steps tend to
have similar optimal triplets. If the previous winner lands within Phase 1
tolerance (±5% of `motor_max_rpm`), it is used as the Phase 1 seed and the
coarse grid is skipped entirely for this step. This is the fastest path through
calibration and becomes increasingly effective as steps progress.

If the seed misses — either because no previous winner exists (step 1) or
because the optimal triplet has shifted significantly — the algorithm falls
through to the full coarse grid.

#### Phase 1 — Coarse Grid Search

A sparse grid of triplets is evaluated across the parameter space:

| Axis | Values | Spacing |
|------|--------|---------|
| `phaseDelayUs` | 5 values | Evenly spaced: 1000, 2625, 4250, 5875, 7500 µs |
| `burstOn` (N_on) | 5 values | Evenly spaced: 1, 8, 15, 22, 30 |
| N_off | 3 fixed values | 0, 7, 15 (ascending) |

Maximum triplets per step: 5 × 5 × 3 = **75**.

For each (alpha, N_on) pair, the cross-step stall memory (see below) governs
whether the pair is measured at all, and with how many trials. The N_off loop
bails immediately on a stall, since `kPhase1NoffValues` is ordered ascending
— higher N_off reduces duty further and cannot help a motor that is already
stalling.

The best triplet (lowest stress, within ±5% RPM tolerance of target) is
carried forward as the Phase 1 seed for Phase 2.

#### Phase 2 — Fine Neighbourhood Search

A dense neighbourhood around the Phase 1 best is evaluated:

| Axis | Range | Step |
|------|-------|------|
| `phaseDelayUs` | ±1000 µs | 250 µs |
| N_on | ±3 | 1 |
| N_off | ±3 | 1 |

All values are clamped to valid ranges. The RPM tolerance is tightened to ±2%
of `motor_max_rpm`. The N_off loop bails on stall for the same reason as
Phase 1. Phase 2 does not consult the stall memory map — its neighbourhood is
tight enough that false negatives from skipping would be more costly than the
measurements themselves.

The Phase 2 winner for each step is recorded in the profile and carried forward
as the seed for the next step.

### Step 3 — Operational Floor Enforcement

After all steps are searched, some low-speed steps may be marked unfeasible —
the motor cannot run that slowly without stalling or producing unacceptable
stress. `EnforceOperationalFloor()` back-fills these slots with the strategy
from the lowest feasible step. A request for 10% speed on a motor that cannot
run below 30% will apply the 30% strategy rather than producing an invalid
output.

Slot 0 is unconditionally set to motor-off: phaseDelayUs=MAX, burstOn=0,
burstWindow=1. The gate never fires.

---

## Triplet Measurement

Each triplet is measured by `MeasureTriplet()`, which runs up to `REPEAT_COUNT`
(default: 3) trials and averages the resulting RPM and stress scores.

Each trial follows this sequence:

1. **RotorStop** — `TriacOff()` is called and the function blocks until
   `REGetVelocity()` reads below `SPINDOWN_RPM_THRESHOLD` (2 RPM). Every
   trial begins from a stationary rotor. This is non-negotiable: it means
   stall readings are genuine and not artefacts of a still-spinning rotor.

2. **StartupKick** — Full power is applied and the function waits for the
   rotor to reach 90% of the target RPM (the intercept velocity). If no
   movement is detected within `STALL_TIMEOUT_MS` (100 ms), a stall is
   declared, the TRIAC is turned off, and `MeasureTriplet()` returns
   immediately with `measured_rpm=0` and `stress=STRESS_FACTOR_MAX`. **No
   further trials are attempted.** Grinding through repeated stall attempts
   would pump sustained full-power current into stationary windings — the
   single most damaging thing you can do to an AC induction motor.

3. **Apply triplet** — `TriacRun()` is called with the test parameters.

4. **WaitForSettle** — RPM is sampled every 100 ms into a 10-sample rolling
   window. The motor is considered settled when the peak-to-peak spread
   across the window is ≤ 20 RPM. Hard timeout: 10 seconds. If the motor
   does not settle, the result is still recorded but an oscillation penalty
   is applied to its stress score.

5. **MeasurePeakJerk** — The rotor velocity is sampled every 5 ms for 1
   second. Jerk (the second derivative of velocity with respect to time) is
   computed at each sample and the peak absolute value is returned.

6. **CalculateStress** — The stress score is computed from the settled RPM
   and peak jerk (see Stress Model below).

---

## Stress Model

Each triplet is assigned a dimensionless stress factor. Lower stress is better.
The stress model combines two physical contributors:

### Slip Stress (weight: 40%)

An AC induction motor runs below its synchronous speed due to rotor slip. For
a 2-pole motor on 50 Hz mains, synchronous speed is 3000 RPM. Slip is the
fraction of synchronous speed lost:

```
slip_pm = (3000 - measured_rpm) / 3000 * 1000   [permille, 0–1000]
```

Higher slip means the rotor is lagging further behind the rotating magnetic
field. This increases rotor current, which increases resistive heating.
Operating points with high slip are thermally unfavourable.

### Jerk Stress (weight: 60%)

Jerk is the rate of change of acceleration (|d²ω/dt²|). It is measured
during the settled operating window, so it captures the roughness of the
motor's steady-state running at this triplet — not the transient from startup.

Phase-angle and burst-mode control both produce torque pulsations at multiples
of the mains frequency. Triplets that produce high jerk are mechanically
abusive: they create repetitive impact loading on bearings, stress the rotor
laminations, and produce audible noise. Jerk stress is the dominant term in the
model (60%) because mechanical damage accumulates faster than thermal damage
for this class of motor.

```
jerk_pm = min(peak_jerk, JERK_CEILING) / JERK_CEILING * 1000   [permille, 0–1000]
```

### Combined Stress

```
stress = ((jerk_pm * 600) + (slip_pm * 400)) * 100
```

This gives a result in the range [0, 100,000,000]. Any result at or above
`AC_FAN_STRESS_FACTOR_MAX` (1,000,000) is treated as unfeasible — in practice
this sentinel value is only reached when `MeasureTriplet()` returns from a stall
with no valid trials.

### Oscillation Penalty

If the motor did not settle within `SETTLE_TIMEOUT_MS`, an additional penalty
is added proportional to the peak-to-peak RPM spread observed:

```
stress += p2p_spread * OSCILLATION_PENALTY   (OSCILLATION_PENALTY = 50)
```

This makes oscillating operating points strongly unfavourable regardless of
their average RPM or jerk characteristics.

### Tuning JERK_CEILING

`AC_FAN_JERK_CEILING` is the jerk value (in RPM/interval² units, where
interval = `JERK_POLL_MS` = 5 ms) that maps to full-scale jerk stress. It must
be determined empirically for the specific motor and load in use.

**Procedure:**

1. Set `AC_FAN_JERK_CEILING` to a large value (e.g. 100000) so jerk stress is
   effectively disabled.
2. Run a calibration pass and log the raw peak jerk values reported by
   `MeasurePeakJerk()` for a range of triplets — particularly the worst-sounding
   ones and the smoothest ones.
3. Choose `AC_FAN_JERK_CEILING` as the jerk value observed at the boundary
   between "acceptable" and "unacceptable" motor behaviour.
4. Re-run calibration with the tuned value.

A value that is too low will cause the jerk stress term to saturate for most
triplets, making the model insensitive to differences between them. A value
that is too high will give jerk too little influence and allow mechanically
rough operating points to be selected.

---

## Stall Memory

Calibration accumulates knowledge of which (alpha, N_on) pairs consistently
fail to sustain motor rotation across multiple speed steps. This knowledge is
used to skip or reduce measurements on pairs that are unlikely to be viable,
protecting the motor from unnecessary stall attempts and reducing calibration
time.

### Data Structure

The stall map is an array of two `uint32_t` words containing 2-bit saturating
counters — one per (alpha_index, N_on_index) pair in the Phase 1 grid.

```
PHASE1_ALPHA_STEPS × PHASE1_NON_STEPS = 5 × 5 = 25 pairs
25 pairs × 2 bits = 50 bits → fits in 2 × uint32_t (64 bits total)
```

Bit position for pair (ai, ni): `(ai * PHASE1_NON_STEPS + ni) * 2`

Each counter saturates at 3. It is incremented once per calibration step on
which that pair produced a stall. It is never decremented.

### Thresholds

| Counter value | Action |
|---------------|--------|
| 0–1 | Full `REPEAT_COUNT` trials (default: 3) |
| ≥ `STALL_COUNT_REDUCE` (2) | Reduced to `REPEAT_COUNT_REDUCED` (1) trial |
| ≥ `STALL_COUNT_SKIP` (3) | Pair skipped entirely — no measurement |

### Design Rationale

The thresholds are deliberately conservative. A single stall never permanently
excludes a pair. Two stalls reduce measurement effort while still giving the
pair a chance. Only three consecutive stalls across different steps — a strong
signal that this pair is genuinely unfeasible — results in skipping.

This guards against false negatives. Stalls can occur due to I2C noise or
unlucky mains phase timing, not only genuine motor incapability. Since every
trial begins from a stationary rotor (`RotorStop()` is called before every
`StartupKick()`), the risk of a stall being caused by residual rotor motion is
eliminated.

---

## RPM Measurement

`REGetVelocity()` returns `Rpm` (`uint16_t`), already scaled to revolutions per
minute inside the rotary encoder driver. It is driven by an async I2C state
machine (`REProcess()`) that must be called regularly to keep readings fresh.

During calibration, `REProcess()` is called immediately before every
`REGetVelocity()` call, followed by `osDelay(10)` to allow the I2C transaction
to complete. This is conservative but safe given FreeRTOS tick resolution and
the AS5600's read latency at 400 kHz.

Any reading above 10,000 RPM is treated as a corrupt I2C result and clamped to
zero. This threshold is well above any physically plausible speed for a domestic
oven fan.

---

## Runtime Drive

`ACFan_Drive()` takes a previously populated `ACFanProfileMap` and a requested
speed in permille (0–1000, where 1000 = 100% of `motor_max_rpm`).

### Lookup

The profile divides the speed range into `AC_FAN_NUM_STEPS` equal bands of
`AC_FAN_STEP_INCREMENT_PM` permille each (default: 10 bands of 100 permille).
The two slots that bracket the requested speed are identified as `lower_idx` and
`upper_idx`.

### Exact Boundary

If the request falls exactly on a band boundary (e.g. 300 permille = slot 3),
that slot's strategy is applied directly with no interpolation.

### Interpolation

For requests between boundaries, all three drive parameters are linearly
interpolated between the two bracketing slots:

```
fraction = (requested_pm % STEP_INCREMENT) * 10   [0–1000 permille within band]

phaseDelayUs = interp(low.phaseDelayUs, high.phaseDelayUs, fraction)
burstOn      = interp(low.burstOn,      high.burstOn,      fraction)
burstWindow  = interp(low.burstWindow,  high.burstWindow,  fraction)
```

After interpolation, `burstOn` is clamped to `burstWindow` to guard against
rounding inconsistencies from independently interpolating both fields.

### On/Off Boundary Special Case

If one neighbouring slot has `burstOn=0` (motor off) and the other does not,
linear interpolation would produce a meaningless fractional gate firing count.
In this case the function snaps to whichever slot is nearer:

- fraction < 500 permille → apply lower slot
- fraction ≥ 500 permille → apply upper slot

---

## FreeRTOS Considerations

- All blocking waits use `osDelay()`. The calibration function is cooperative
  and will yield at every delay point.
- `REProcess()` is called from within the calibration task. If the rotary
  encoder is also serviced from another task or timer, concurrent calls to
  `REProcess()` are not safe — the driver uses a single static instance with
  no locking. Ensure `REProcess()` is called from one context only during
  calibration.
- Calibration is long-running by design. Stack depth for the calibration task
  should be sized to accommodate the local variables in `ACFan_RunCalibration()`
  and its callees. The largest frame is `WaitForSettle()` with its 10-element
  `Rpm` window — negligible on any reasonable stack.

---

## Constants Reference

### Calibration Timing

| Constant | Default | Description |
|----------|---------|-------------|
| `REPEAT_COUNT` | 3 | Trials per triplet under normal conditions |
| `REPEAT_COUNT_REDUCED` | 1 | Trials for pairs with stall counter ≥ `STALL_COUNT_REDUCE` |
| `SPINUP_TIMEOUT_MS` | 600 | Maximum time to reach intercept velocity from standstill |
| `STALL_TIMEOUT_MS` | 100 | Time with no movement before stall is declared |
| `SETTLE_TIMEOUT_MS` | 10000 | Hard timeout for RPM stabilisation |
| `SETTLE_P2P_THRESHOLD` | 20 | Peak-to-peak RPM spread considered settled |
| `SETTLE_WINDOW_SIZE` | 10 | Rolling window depth (samples at 100 ms intervals) |
| `JERK_POLL_MS` | 5 | Jerk measurement sampling interval |
| `JERK_WINDOW_MS` | 1000 | Jerk measurement duration per trial |
| `SPINDOWN_POLL_MS` | 50 | Polling interval during rotor coastdown |
| `SPINDOWN_RPM_THRESHOLD` | 2 | RPM below which rotor is considered stopped |
| `MAX_RPM_SETTLE_MS` | 5000 | Full-power soak before max RPM measurement |

### Stress Model

| Constant | Default | Description |
|----------|---------|-------------|
| `AC_FAN_JERK_CEILING` | 1000 | **Must be tuned empirically.** See Tuning JERK_CEILING. |
| `AC_FAN_STRESS_FACTOR_MAX` | 1,000,000 | Infeasibility sentinel value |
| `WEIGHT_JERK_PM` | 600 | Jerk stress weight (permille) |
| `WEIGHT_SLIP_PM` | 400 | Slip stress weight (permille) |
| `OSCILLATION_PENALTY` | 50 | Added stress per RPM of p2p spread when unsettled |
| `SYNC_SPEED_RPM` | 3000 | Synchronous speed for slip calculation (2-pole, 50 Hz) |

### Search Grid

| Constant | Default | Description |
|----------|---------|-------------|
| `PHASE1_ALPHA_STEPS` | 5 | Alpha values in Phase 1 coarse grid |
| `PHASE1_NON_STEPS` | 5 | N_on values in Phase 1 coarse grid |
| `PHASE1_NOFF_COUNT` | 3 | N_off fixed values: {0, 7, 15} — must remain ascending |
| `PHASE1_RPM_TOLERANCE_PM` | 50 | Phase 1 RPM acceptance window (±5% of max RPM) |
| `PHASE2_ALPHA_RADIUS_US` | 1000 | Phase 2 alpha search radius (µs) |
| `PHASE2_ALPHA_STEP_US` | 250 | Phase 2 alpha step size (µs) |
| `PHASE2_NON_RADIUS` | 3 | Phase 2 N_on search radius |
| `PHASE2_NOFF_RADIUS` | 3 | Phase 2 N_off search radius |
| `PHASE2_RPM_TOLERANCE_PM` | 20 | Phase 2 RPM acceptance window (±2% of max RPM) |

### Stall Memory

| Constant | Default | Description |
|----------|---------|-------------|
| `STALL_COUNT_REDUCE` | 2 | Counter threshold for reduced-trial mode |
| `STALL_COUNT_SKIP` | 3 | Counter threshold for skip-entirely mode |

### Hardware

| Constant | Default | Description |
|----------|---------|-------------|
| `AC_FAN_MIN_ALPHA_US` | 1000 | Minimum phase delay (µs) |
| `AC_FAN_MAX_ALPHA_US` | 7500 | Maximum phase delay (µs) |
| `AC_FAN_MAX_BURST_WINDOW` | 30 | Maximum burst window half-cycles |
| `AC_FAN_NUM_STEPS` | 10 | Speed steps in the profile |
| `AC_FAN_STEP_INCREMENT_PM` | 100 | Permille per step |

---

## Periodic Flush (Optional)

`PERIODIC_FLUSH` (default: 0) enables a thermal protection feature that runs
the motor at full speed for `FLUSH_DURATION_MS` every `FLUSH_INTERVAL_TRIPLETS`
triplets during calibration. Full-speed running maximises airflow across the
windings, promoting convective cooling.

This feature is disabled by default. The stall bail-out on first failure already
limits exposure to sustained stall current significantly. Enable `PERIODIC_FLUSH`
only if thermal testing with a contact thermometer or thermal camera shows the
motor accumulating heat during a calibration run.

```c
#define PERIODIC_FLUSH          1   // Enable
#define FLUSH_INTERVAL_TRIPLETS 20  // Every 20 measurements
#define FLUSH_DURATION_MS       3000
```

---

## Usage

### Initialisation

```c
ACFan_Init();
```

Call once at startup, before any other function. Opens the TRIAC and encoder
handles internally.

### Calibration

```c
ACFanProfileMap profile;
ACFan_RunCalibration( &profile );
// Save profile to non-volatile storage here
MyStorage_Write( PROFILE_ADDRESS, &profile, sizeof( profile ) );
```

Run from a dedicated FreeRTOS task. The task may be deleted or suspended after
calibration completes. The caller is responsible for saving the populated
profile to non-volatile storage. The structure contains no pointers and can be
written with a single block write.

### Runtime Drive

```c
// Restore saved profile at boot
ACFanProfileMap profile;
MyStorage_Read( PROFILE_ADDRESS, &profile, sizeof( profile ) );

// Drive at 65% of maximum RPM
ACFan_Drive( &profile, 650 );

// Stop the fan
ACFan_Drive( &profile, 0 );
```

`ACFan_Drive()` is lightweight — it performs a table lookup and at most three
integer interpolations before calling `TriacRun()`. It is safe to call
repeatedly from a control loop.

---

## Thermal Considerations

### Calibration Temperature

Calibration should be performed at the **highest anticipated operating
temperature** — for an oven fan, this means the oven cavity at its maximum
working temperature with airflow conditions representative of that state. This
is important for two reasons.

First, winding resistance increases with temperature, which affects slip and
therefore the RPM achieved by any given triplet. A profile built at a lower
temperature will assign triplets that produce slightly higher RPM than the motor
will actually deliver at peak operating temperature, meaning the profile is
optimistic about what each step can achieve under the most demanding conditions.
Building the profile at the thermal ceiling ensures it is valid across the
entire operating range — a motor that performs acceptably at maximum temperature
will perform at least as well at any lower temperature.

Second, and more valuably, a profile built at the thermal ceiling establishes a
**worst-case thermal baseline**. At runtime, if the same triplet is delivering
consistently lower RPM than `actual_rpm` records in the profile, the gap is a
direct, measurable indicator that the motor is running hotter than its
calibration condition — a non-contact proxy for winding temperature derived
purely from the RPM measurement already available. Any RPM shortfall relative
to a worst-case baseline is a genuine anomaly, not normal thermal drift.

### Runtime Thermal Monitoring

The profile makes thermal monitoring straightforward without any additional
sensors. A supervisor consuming both the profile and live RPM readings can
compare `REGetVelocity()` against the `actual_rpm` recorded for the currently
active slot. A persistent downward deviation — the same triplet delivering
consistently less RPM than the profile predicts — is direct evidence of
increased winding resistance due to elevated temperature.

The physical mechanism is unambiguous: as winding temperature rises, resistance
increases, rotor current for a given slip falls, torque drops, and the motor
slows. The slip gap widens. The profile, built at known thermal conditions,
provides the reference against which this drift is measured.

This signal can drive a graduated response:

- **Small deviation** — log and monitor. Normal variation from load changes or
  ambient temperature.
- **Sustained moderate deviation** — reduce requested speed to lower power
  dissipation and give the motor thermal headroom.
- **Large or rapidly growing deviation** — raise a thermal warning flag or
  trigger a safety shutdown.

If calibration is ever repeated after a component change or service, performing
it under the same thermal conditions as the original run ensures the profiles
are directly comparable, which is valuable for diagnostic purposes.