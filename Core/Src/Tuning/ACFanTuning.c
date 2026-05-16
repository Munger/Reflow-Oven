// ============================================================================
// ACFanTuning.c
//
// AC Fan Calibration and Runtime Drive Engine — Implementation
//
// See ACFanTuning.h for full module documentation.
//
// Design notes:
//   - All blocking delays use osDelay() — this file assumes a FreeRTOS task
//     context throughout.
//   - REProcess() is called immediately before every REGetVelocity() read,
//     followed by osDelay(10) to allow the async I2C transaction to complete.
//     This is conservative but safe given FreeRTOS tick resolution.
//   - REGetVelocity() returns Rpm (uint16_t), already scaled to RPM inside
//     the driver. Spuriously high values from corrupt I2C reads are clamped
//     to zero. No sign reinterpretation is needed.
//   - ACFan_DriveParams is the internal strategy type. Conversion to
//     TriacDriveParams occurs only at the point of calling TriacRun().
//     burstWindow = burstOn + N_off — the TRIAC driver expects total window
//     size, not a separate off count.
//   - MeasureTriplet() bails out immediately on the first stall rather than
//     grinding through all REPEAT_COUNT trials. This protects the motor
//     windings from repeated full-power stall current.
//   - Stall memory: a 2-bit saturating counter per (alpha, N_on) pair tracks
//     stalls across steps. Pairs with counter >= STALL_COUNT_SKIP are skipped
//     entirely; pairs with counter >= STALL_COUNT_REDUCE use only 1 trial.
//     RotorStop() before every trial means stalls are genuine, not artefacts
//     of a still-spinning rotor, so counters are meaningful.
//   - Previous-step winner seeding: the Phase 2 winner from the previous step
//     is tried first at the start of each new step. If it lands within Phase 1
//     tolerance the coarse grid is skipped entirely for that step.
//   - PERIODIC_FLUSH: when enabled, the motor is run at full speed for a short
//     burst every FLUSH_INTERVAL_TRIPLETS triplets during calibration. This
//     promotes airflow-based cooling of the windings. Disabled by default —
//     enable only if thermal testing shows it is necessary.
//   - No dynamic memory allocation is used anywhere in this file.
//   - No flash writes are performed. The caller owns ACFanProfileMap.
// ============================================================================

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h> // abs()
#include <string.h>

#include "ACFanTuning.h"
#include "cmsis_os.h"
#include "rotaryencoder.h"
#include "triac.h"

// ============================================================================
// HARDWARE SELECTION
// ============================================================================

// The TRIAC channel used by this module.
// Change here if the fan is ever moved to a different channel.
#define AC_FAN_TRIAC_ID          TriacOvenFan

// ============================================================================
// CALIBRATION CONSTANTS
// ============================================================================

// TRIAC drive parameter bounds for the fan channel.
#define AC_FAN_MIN_ALPHA_US      1000
#define AC_FAN_MAX_ALPHA_US      7500
#define AC_FAN_MAX_BURST_WINDOW  30

// Number of independent measurement trials averaged per triplet under normal
// conditions. Reduced to REPEAT_COUNT_REDUCED for suspicious pairs (stall
// counter >= STALL_COUNT_REDUCE). On the first stall, MeasureTriplet() bails
// immediately regardless of this value.
#define REPEAT_COUNT             3
#define REPEAT_COUNT_REDUCED     1

// Spinup: full-power kick timeout before we declare a stall.
#define SPINUP_TIMEOUT_MS        600

// Spinup: if the rotor has not moved at all within this window, it is stalled.
#define STALL_TIMEOUT_MS         100

// Settle: hard timeout. If RPM has not stabilised by this point, the result
// is recorded as unsettled and an oscillation penalty is applied to its stress.
#define SETTLE_TIMEOUT_MS        10000

// Settle: peak-to-peak RPM spread across the rolling window that we consider
// stable enough to record.
#define SETTLE_P2P_THRESHOLD     20

// Settle: rolling window depth. Samples are taken 100ms apart, so this
// window spans 1 second of rotor velocity data.
#define SETTLE_WINDOW_SIZE       10

// Jerk measurement: inter-sample interval in milliseconds.
#define JERK_POLL_MS             5

// Jerk measurement: duration of the peak-jerk sampling window per trial.
#define JERK_WINDOW_MS           1000

// Spindown: polling interval while waiting for the rotor to stop after TriacOff.
#define SPINDOWN_POLL_MS         50

// Spindown: RPM below which the rotor is considered stopped.
#define SPINDOWN_RPM_THRESHOLD   2

// Full-power soak time before sampling MOTOR_MAX_RPM at the start of
// calibration. Gives the rotor time to reach its natural ceiling.
#define MAX_RPM_SETTLE_MS        5000

// ============================================================================
// STRESS MODEL CONSTANTS
// ============================================================================

// Jerk ceiling: the peak jerk value (in RPM/interval² units as computed by
// MeasurePeakJerk) that maps to full-scale jerk stress (1000 permille).
// ** THIS VALUE MUST BE DETERMINED EMPIRICALLY for the specific motor. **
// The default is a conservative starting point only.
#define AC_FAN_JERK_CEILING      1000

// Stress factor cap. Any result at or above this is treated as unfeasible.
#define AC_FAN_STRESS_FACTOR_MAX 1000000UL

// Stress model weights (permille). Must sum to 1000.
#define WEIGHT_JERK_PM           600 // 60% — jerk (mechanical shock)
#define WEIGHT_SLIP_PM           400 // 40% — synchronous slip (thermal)

// Oscillation penalty per RPM of peak-to-peak spread when the motor did not
// settle within SETTLE_TIMEOUT_MS.
#define OSCILLATION_PENALTY      50

// Synchronous speed for a 2-pole motor on 50 Hz mains (RPM).
#define SYNC_SPEED_RPM           3000

// ============================================================================
// SEARCH GRID CONSTANTS
// ============================================================================

// Phase 1 coarse grid:
//   Alpha: PHASE1_ALPHA_STEPS evenly spaced across [MIN_ALPHA_US, MAX_ALPHA_US]
//   N_on:  PHASE1_NON_STEPS   evenly spaced across [1, MAX_BURST_WINDOW]
//   N_off: fixed values from kPhase1NoffValues[]
// Total triplets per step (worst case): 5 * 5 * 3 = 75
#define PHASE1_ALPHA_STEPS       5
#define PHASE1_NON_STEPS         5
#define PHASE1_NOFF_COUNT        3

// kPhase1NoffValues must remain in ascending order — the stall bail-out in the
// N_off loop depends on higher indices meaning less duty (more N_off = more gap
// = worse for a stalling motor). Do not reorder.
static const uint8_t kPhase1NoffValues[ PHASE1_NOFF_COUNT ] = { 0, 7, 15 };

// RPM tolerance for Phase 1: accept candidates within ±5% of motorMaxRPM.
#define PHASE1_RPM_TOLERANCE_PM  50

// Phase 2 fine neighbourhood:
//   Alpha: ±PHASE2_ALPHA_RADIUS_US in PHASE2_ALPHA_STEP_US increments
//   N_on:  ±PHASE2_NON_RADIUS in steps of 1
//   N_off: ±PHASE2_NOFF_RADIUS in steps of 1
// Worst case per step: 9 * 7 * 7 = 441 triplets (many clamped to duplicates)
#define PHASE2_ALPHA_RADIUS_US   1000
#define PHASE2_ALPHA_STEP_US     250
#define PHASE2_NON_RADIUS        3
#define PHASE2_NOFF_RADIUS       3

// RPM tolerance for Phase 2: tightened to ±2% of motorMaxRPM.
#define PHASE2_RPM_TOLERANCE_PM  20

// Step increment between profile slots (permille).
#define AC_FAN_STEP_INCREMENT_PM 100

// ============================================================================
// STALL MEMORY CONSTANTS
// ============================================================================

// The stall map is a 2D array of 2-bit saturating counters indexed by
// (alpha_index, N_on_index) over the Phase 1 grid dimensions. Each counter
// tracks how many steps have produced a stall for that pair across the entire
// calibration run.
//
// Layout: PHASE1_ALPHA_STEPS * PHASE1_NON_STEPS = 25 pairs.
// Each counter is 2 bits, saturating at 3.
// Total storage: 25 * 2 = 50 bits, packed into two uint32_t values (64 bits).
// Bit position for pair (ai, ni): (ai * PHASE1_NON_STEPS + ni) * 2
//
// Thresholds:
//   >= STALL_COUNT_REDUCE: use REPEAT_COUNT_REDUCED trials (1 instead of 3)
//   >= STALL_COUNT_SKIP:   skip the pair entirely without measuring
//
// These are deliberately conservative to avoid false negatives corrupting
// the profile. A single stall never permanently excludes a pair.
#define STALL_COUNT_REDUCE       2 // 2+ stalls across steps → reduced trials
#define STALL_COUNT_SKIP         3 // 3  stalls across steps → skip entirely

#define STALL_MAP_PAIRS          ( PHASE1_ALPHA_STEPS * PHASE1_NON_STEPS ) // 25
#define STALL_MAP_WORDS          2                                         // ceil(25 * 2 / 32) = 2 uint32_t words

// Extract the 2-bit saturating counter for pair index p from the map.
static inline uint8_t StallMap_Get( const uint32_t map[ STALL_MAP_WORDS ], uint8_t p ) {
    uint8_t bit = (uint8_t)( p * 2u );
    uint8_t word = bit >> 5u;  // divide by 32
    uint8_t shift = bit & 31u; // modulo 32
    return (uint8_t)( ( map[ word ] >> shift ) & 0x3u );
}

// Increment the 2-bit saturating counter for pair index p, clamping at 3.
static inline void StallMap_Increment( uint32_t map[ STALL_MAP_WORDS ], uint8_t p ) {
    uint8_t bit = (uint8_t)( p * 2u );
    uint8_t word = bit >> 5u;
    uint8_t shift = bit & 31u;
    uint8_t val = (uint8_t)( ( map[ word ] >> shift ) & 0x3u );
    if ( val < 3u ) {
        map[ word ] &= ~( 0x3u << shift );                  // clear the 2 bits
        map[ word ] |= ( (uint32_t)( val + 1u ) << shift ); // write incremented value
    }
}

// Compute the flat pair index from alpha_index and N_on_index.
static inline uint8_t StallMap_Index( uint8_t ai, uint8_t ni ) {
    return (uint8_t)( ai * PHASE1_NON_STEPS + ni );
}

// ============================================================================
// PERIODIC FLUSH FEATURE
// ============================================================================

// Set to 1 to enable periodic full-speed cooling flushes during calibration.
// When enabled, the motor is run at full power for FLUSH_DURATION_MS every
// FLUSH_INTERVAL_TRIPLETS triplets, promoting airflow-based winding cooling.
//
// Enable only if thermal testing shows the motor is accumulating heat during
// the calibration run. The flush adds time but does not affect profile quality.
#define PERIODIC_FLUSH 0

#if PERIODIC_FLUSH
// How many triplet measurements to take between cooling flushes.
#define FLUSH_INTERVAL_TRIPLETS 20

// How long to run at full speed during each flush (milliseconds).
#define FLUSH_DURATION_MS       3000
#endif // PERIODIC_FLUSH

// ============================================================================
// MODULE STATE
// ============================================================================

static TriacRef         s_triac = NULL;
static RotaryEncoderRef s_encoder = NULL;

// ============================================================================
// INTERNAL — RPM READING
// ============================================================================

// Trigger one I2C state-machine cycle, wait for the async transaction to
// complete, then return a clamped RPM reading.
//
// The 10 ms delay is conservative. The AS5600 angle register read at 400 kHz
// takes ~45 µs on the wire; a 10 ms osDelay() gives the DMA/interrupt ample
// time to complete and the callback to fire before we read the result.
//
// Any reading above 10000 RPM is implausible for a domestic oven fan and is
// treated as a corrupt I2C result — clamped to zero.
static Rpm ReadRPM( void ) {
    REProcess();
    osDelay( 10 );

    Rpm v = REGetVelocity( s_encoder );
    return ( v > 10000u ) ? (Rpm)0 : v;
}

// ============================================================================
// INTERNAL — ROTOR CONTROL
// ============================================================================

// Stop the TRIAC and block until the rotor velocity drops below
// SPINDOWN_RPM_THRESHOLD. No timeout — assumes the motor will coast to rest.
static void RotorStop( void ) {
    TriacOff( s_triac );

    while ( ReadRPM() > SPINDOWN_RPM_THRESHOLD ) {
        osDelay( SPINDOWN_POLL_MS );
    }
}

// Apply full power and wait for the rotor to reach 90% of targetRPM.
// Returns true if intercept velocity was reached within SPINUP_TIMEOUT_MS.
// Returns false if the motor stalls (no movement within STALL_TIMEOUT_MS).
//
// Full power: phaseDelayUs=0, burstOn=1, burstWindow=1.
static bool StartupKick( Rpm targetRPM ) {
    TriacDriveParams fullPower = { .phaseDelayUs = 0, .burstOn = 1, .burstWindow = 1 };
    TriacRun( s_triac, fullPower );

    // uint32_t intermediate prevents overflow before division.
    Rpm        interceptRPM = (Rpm)( ( (uint32_t)targetRPM * 900UL ) / 1000UL );

    DurationMs elapsed = 0;

    while ( elapsed < SPINUP_TIMEOUT_MS ) {
        osDelay( 2 );
        elapsed += 2;

        Rpm current = ReadRPM();

        if ( elapsed >= STALL_TIMEOUT_MS && current < SPINDOWN_RPM_THRESHOLD ) {
            TriacOff( s_triac );
            return false;
        }

        if ( current >= interceptRPM ) {
            return true;
        }
    }

    // Timed out without reaching intercept but motor is moving — treat as
    // partial success and let the caller evaluate the measured RPM.
    return true;
}

// ============================================================================
// INTERNAL — PERIODIC FLUSH
// ============================================================================

#if PERIODIC_FLUSH
// Run the motor at full speed for FLUSH_DURATION_MS to promote airflow cooling
// of the windings. The rotor is stopped cleanly before returning so the next
// measurement starts from a known state.
static void PeriodicFlush( void ) {
    TriacDriveParams fullPower = { .phaseDelayUs = 0, .burstOn = 1, .burstWindow = 1 };
    TriacRun( s_triac, fullPower );
    osDelay( FLUSH_DURATION_MS );
    RotorStop();
}
#endif // PERIODIC_FLUSH

// ============================================================================
// INTERNAL — SETTLE DETECTION
// ============================================================================

// Wait for the rotor RPM to stabilise within SETTLE_P2P_THRESHOLD RPM across
// a rolling window of SETTLE_WINDOW_SIZE samples taken 100 ms apart.
//
// On success: settledRPM = midpoint of [min, max] in the window, returns true.
// On timeout: settledRPM = most recent raw reading, returns false.
// p2pSpread is always written with the most recent window spread.
static bool WaitForSettle( Rpm* settledRPM, uint16_t* p2pSpread ) {
    Rpm        window[ SETTLE_WINDOW_SIZE ];
    uint8_t    wIdx = 0;
    DurationMs elapsed = 0;

    memset( window, 0, sizeof( window ) );
    *p2pSpread = 0xFFFFU;
    *settledRPM = 0;

    while ( elapsed < SETTLE_TIMEOUT_MS ) {
        window[ wIdx % SETTLE_WINDOW_SIZE ] = ReadRPM();
        wIdx++;

        // Evaluate only once the window is fully populated (1 second of data).
        if ( elapsed >= (DurationMs)( SETTLE_WINDOW_SIZE * 100U ) ) {
            Rpm minR = 0xFFFFU, maxR = 0;

            for ( uint8_t i = 0; i < SETTLE_WINDOW_SIZE; i++ ) {
                if ( window[ i ] < minR )
                    minR = window[ i ];
                if ( window[ i ] > maxR )
                    maxR = window[ i ];
            }

            *p2pSpread = maxR - minR;

            if ( *p2pSpread <= SETTLE_P2P_THRESHOLD ) {
                *settledRPM = ( minR + maxR ) / 2U;
                return true;
            }
        }

        osDelay( 100 );
        elapsed += 100;
    }

    *settledRPM = ReadRPM();
    return false;
}

// ============================================================================
// INTERNAL — JERK MEASUREMENT
// ============================================================================

// Sample rotor velocity every JERK_POLL_MS over durationMs and return the
// peak absolute jerk observed.
//
// Jerk is the discrete second derivative of velocity:
//   acceleration[n] = (velocity[n] - velocity[n-1]) / dt
//   jerk[n]         = (acceleration[n] - acceleration[n-1]) / dt
//
// Units are RPM/interval² (where interval = JERK_POLL_MS ms). The absolute
// scale is absorbed into AC_FAN_JERK_CEILING, which must be tuned empirically.
// Direction is discarded — we care only about magnitude of mechanical shock.
static uint32_t MeasurePeakJerk( DurationMs durationMs ) {
    uint32_t   peakJerk = 0;
    int32_t    lastVelocity = (int32_t)ReadRPM();
    int32_t    lastAccel = 0;
    DurationMs elapsed = 0;

    while ( elapsed < durationMs ) {
        osDelay( JERK_POLL_MS );
        elapsed += JERK_POLL_MS;

        int32_t  currentVelocity = (int32_t)ReadRPM();
        int32_t  currentAccel = ( currentVelocity - lastVelocity ) / (int32_t)JERK_POLL_MS;
        int32_t  jerkRaw = ( currentAccel - lastAccel ) / (int32_t)JERK_POLL_MS;
        uint32_t currentJerk = (uint32_t)abs( jerkRaw );

        if ( currentJerk > peakJerk ) {
            peakJerk = currentJerk;
        }

        lastVelocity = currentVelocity;
        lastAccel = currentAccel;
    }

    return peakJerk;
}

// ============================================================================
// INTERNAL — STRESS CALCULATION
// ============================================================================

// Compute a dimensionless stress factor for a given operating point.
//
// Two components, weighted and combined:
//
//   Slip stress (40%): fraction of synchronous speed lost to slip.
//                      Higher slip → more rotor heating and torque ripple.
//
//   Jerk stress (60%): peak |d²ω/dt²| as a fraction of AC_FAN_JERK_CEILING.
//                      Higher jerk → more mechanical shock on bearings.
//
// If the motor did not settle, an oscillation penalty proportional to the
// peak-to-peak RPM spread is added, making unstable operating points strongly
// unfavourable regardless of their average RPM.
//
// Result range: 0 to AC_FAN_STRESS_FACTOR_MAX (100,000,000).
// Intermediate products are kept in uint32_t throughout to prevent overflow.
static StressFactor CalculateStress( Rpm rpm, uint32_t peakJerk, uint16_t p2pSpread, bool settled ) {
    // Slip: distance below synchronous speed, clamped to zero if above.
    int32_t slipVal = (int32_t)SYNC_SPEED_RPM - (int32_t)rpm;
    if ( slipVal < 0 )
        slipVal = 0;
    Permille slipPm = (Permille)( ( (uint32_t)slipVal * 1000UL ) / SYNC_SPEED_RPM );

    // Jerk: clamp to ceiling before scaling to prevent overflow.
    if ( peakJerk > AC_FAN_JERK_CEILING )
        peakJerk = AC_FAN_JERK_CEILING;
    Permille jerkPm = (Permille)( ( peakJerk * 1000UL ) / AC_FAN_JERK_CEILING );

    // Weighted sum: 0–1,000,000. Scaled by 100 gives 0–100,000,000.
    uint32_t weightedSum = ( (uint32_t)jerkPm * WEIGHT_JERK_PM ) + ( (uint32_t)slipPm * WEIGHT_SLIP_PM );
    uint32_t baseStress = weightedSum * 100UL;

    if ( !settled ) {
        baseStress += (uint32_t)p2pSpread * OSCILLATION_PENALTY;
    }

    return (StressFactor)baseStress;
}

// ============================================================================
// INTERNAL — TRIPLET APPLICATION
// ============================================================================

// Convert internal (phaseDelayUs, nOn, nOff) to TriacDriveParams and
// apply it. This is the only point in the file where the internal parameter
// representation is translated to the driver's own type.
static void ApplyParams( uint16_t phaseDelayUs, uint8_t nOn, uint8_t nOff ) {
    TriacDriveParams p;
    p.phaseDelayUs = phaseDelayUs;
    p.burstOn = nOn;
    p.burstWindow = nOn + nOff; // driver expects total window, not separate off count
    TriacRun( s_triac, p );
}

// ============================================================================
// INTERNAL — TRIPLET MEASUREMENT
// ============================================================================

// Internal result type for a single triplet evaluation.
typedef struct {
    uint16_t     phaseDelayUs;
    uint8_t      nOn;
    uint8_t      nOff;
    Rpm          measuredRPM;
    StressFactor stress;
} TripletResult;

// Measure a triplet by running up to repeatCount trials and averaging RPM
// and stress across successful trials.
//
// On the first stall (StartupKick returns false), returns immediately with
// measuredRPM=0 and stress=AC_FAN_STRESS_FACTOR_MAX, protecting the motor
// from repeated full-power stall current.
//
// repeatCount is passed explicitly so the caller can reduce it for pairs
// with a suspicious stall history without changing the global default.
static TripletResult MeasureTriplet( uint16_t phaseDelayUs, uint8_t nOn, uint8_t nOff, Rpm targetRPM,
                                     uint8_t repeatCount ) {
    TripletResult res;
    res.phaseDelayUs = phaseDelayUs;
    res.nOn = nOn;
    res.nOff = nOff;
    res.measuredRPM = 0;
    res.stress = AC_FAN_STRESS_FACTOR_MAX;

    uint32_t sumRPM = 0;
    uint32_t sumStress = 0;
    uint8_t  valid = 0;

    for ( uint8_t r = 0; r < repeatCount; r++ ) {
        RotorStop();

        if ( !StartupKick( targetRPM ) ) {
            // First stall — bail immediately. This triplet cannot sustain
            // rotation. Further trials would only pump more stall current
            // into the windings without any prospect of a useful result.
            return res;
        }

        ApplyParams( phaseDelayUs, nOn, nOff );

        Rpm          settledRPM = 0;
        uint16_t     p2p = 0xFFFFU;
        bool         settled = WaitForSettle( &settledRPM, &p2p );

        uint32_t     peakJerk = MeasurePeakJerk( JERK_WINDOW_MS );

        StressFactor s = CalculateStress( settledRPM, peakJerk, p2p, settled );

        sumRPM += settledRPM;
        sumStress += s;
        valid++;
    }

    if ( valid > 0 ) {
        res.measuredRPM = (Rpm)( sumRPM / valid );
        res.stress = (StressFactor)( sumStress / valid );
    }

    return res;
}

// ============================================================================
// INTERNAL — CLAMPING HELPERS
// ============================================================================

static uint16_t ClampAlpha( int32_t v ) {
    if ( v < AC_FAN_MIN_ALPHA_US )
        return (uint16_t)AC_FAN_MIN_ALPHA_US;
    if ( v > AC_FAN_MAX_ALPHA_US )
        return (uint16_t)AC_FAN_MAX_ALPHA_US;
    return (uint16_t)v;
}

static uint8_t ClampNon( int32_t v ) {
    if ( v < 1 )
        return 1;
    if ( v > AC_FAN_MAX_BURST_WINDOW )
        return (uint8_t)AC_FAN_MAX_BURST_WINDOW;
    return (uint8_t)v;
}

static uint8_t ClampNoff( int32_t v ) {
    if ( v < 0 )
        return 0;
    if ( v > AC_FAN_MAX_BURST_WINDOW )
        return (uint8_t)AC_FAN_MAX_BURST_WINDOW;
    return (uint8_t)v;
}

// ============================================================================
// INTERNAL — LINEAR INTERPOLATION
// ============================================================================

static int32_t InterpLinear( int32_t low, int32_t high, Permille fractionPm ) {
    return low + ( ( ( high - low ) * (int32_t)fractionPm ) / 1000 );
}

// ============================================================================
// INTERNAL — OPERATIONAL FLOOR ENFORCEMENT
// ============================================================================

// Back-fill unfeasible low-speed slots with the lowest feasible strategy, so
// the runtime interpolator always has a valid drive configuration to apply.
//
// Slot 0 is unconditionally set to motor-off:
//   phaseDelayUs = MAX  (most restrictive phase angle)
//   burstOn      = 0    (gate never fires)
//   burstWindow  = 1    (non-zero to avoid division-by-zero in the driver)
static void EnforceOperationalFloor( ACFanProfileMapPtr map ) {
    int8_t lowest = -1;

    for ( uint8_t step = 1; step <= AC_FAN_NUM_STEPS; step++ ) {
        if ( map->slots[ step ].isFeasible ) {
            lowest = (int8_t)step;
            break;
        }
    }

    if ( lowest > 1 ) {
        for ( uint8_t step = 1; step < (uint8_t)lowest; step++ ) {
            map->slots[ step ].strategy = map->slots[ lowest ].strategy;
            map->slots[ step ].actualRPM = map->slots[ lowest ].actualRPM;
            map->slots[ step ].isFeasible = true;
        }
    }

    map->slots[ 0 ].strategy.phaseDelayUs = AC_FAN_MAX_ALPHA_US;
    map->slots[ 0 ].strategy.burstOn = 0;
    map->slots[ 0 ].strategy.burstWindow = 1;
    map->slots[ 0 ].actualRPM = 0;
    map->slots[ 0 ].isFeasible = true;
}

// ============================================================================
// PUBLIC API — INIT
// ============================================================================

void ACFan_Init( void ) {
    s_triac = TriacOpen( AC_FAN_TRIAC_ID );
    s_encoder = REOpen();
}

// ============================================================================
// PUBLIC API — CALIBRATION
// ============================================================================

// Run the full calibration sequence and populate mapOut.
//
// Algorithm:
//   1. Measure MOTOR_MAX_RPM at full power.
//   2. For each speed step 1..AC_FAN_NUM_STEPS:
//      a. Try the previous step's Phase 2 winner. If it lands within Phase 1
//         tolerance, use it as the seed and skip the coarse grid entirely.
//      b. Phase 1 (coarse): sparse grid of up to 75 triplets, ±5% tolerance.
//         Pairs are skipped or reduced based on their cross-step stall counter.
//         N_off loop bails on stall since higher N_off cannot help.
//      c. Phase 2 (fine): dense neighbourhood around Phase 1 best, ±2%.
//         N_off loop bails on stall for the same reason.
//      d. Update the cross-step stall counters for any pair that stalled.
//      e. Record the Phase 2 winner as the profile entry for this step.
//   3. Enforce the operational floor.
//   4. Return. The caller saves mapOut to non-volatile storage.
void ACFan_RunCalibration( ACFanProfileMapPtr mapOut ) {
    if ( !mapOut || !s_triac || !s_encoder ) {
        return;
    }

    memset( mapOut, 0, sizeof( ACFanProfileMap ) );

    // Cross-step stall memory: 2-bit saturating counters for each
    // (alpha_index, N_on_index) pair in the Phase 1 grid.
    // Persists across all steps; cleared only at the start of calibration.
    uint32_t      stallMap[ STALL_MAP_WORDS ] = { 0, 0 };

    // Previous step's Phase 2 winner. Used to seed the next step's search.
    // Initialised to an invalid sentinel (stress == MAX) so the first step
    // always falls through to the full coarse grid.
    TripletResult prevWinner;
    prevWinner.phaseDelayUs = AC_FAN_MIN_ALPHA_US;
    prevWinner.nOn = 1;
    prevWinner.nOff = 0;
    prevWinner.measuredRPM = 0;
    prevWinner.stress = AC_FAN_STRESS_FACTOR_MAX;

#if PERIODIC_FLUSH
    uint32_t tripletCount = 0;
#endif

    // -------------------------------------------------------------------------
    // Step 1: Measure unloaded maximum RPM.
    // -------------------------------------------------------------------------
    RotorStop();

    {
        TriacDriveParams fullPower = { .phaseDelayUs = 0, .burstOn = 1, .burstWindow = 1 };
        TriacRun( s_triac, fullPower );
        osDelay( MAX_RPM_SETTLE_MS );
        mapOut->motorMaxRPM = ReadRPM();
        TriacOff( s_triac );
    }

    Rpm motorMax = mapOut->motorMaxRPM;

    if ( motorMax < 10 ) {
        return; // Could not measure a meaningful max RPM — abort.
    }

    // -------------------------------------------------------------------------
    // Step 2: Per-step calibration loop.
    // -------------------------------------------------------------------------
    for ( uint8_t step = 1; step <= AC_FAN_NUM_STEPS; step++ ) {
        Rpm           targetRPM = (Rpm)( ( (uint32_t)motorMax * step * AC_FAN_STEP_INCREMENT_PM ) / 1000UL );

        Rpm           tolP1 = (Rpm)( ( (uint32_t)motorMax * PHASE1_RPM_TOLERANCE_PM ) / 1000UL );
        Rpm           tolP2 = (Rpm)( ( (uint32_t)motorMax * PHASE2_RPM_TOLERANCE_PM ) / 1000UL );

        // Stall flags for Phase 1 pairs on this step only. Used to update
        // the cross-step stallMap after Phase 1 completes.
        // One bit per pair; bit set means this step produced a stall.
        uint32_t      stepStallFlags[ STALL_MAP_WORDS ] = { 0, 0 };

        // Initialise best candidates with worst-case sentinel values.
        TripletResult bestP1;
        bestP1.phaseDelayUs = AC_FAN_MIN_ALPHA_US;
        bestP1.nOn = 1;
        bestP1.nOff = 0;
        bestP1.measuredRPM = 0;
        bestP1.stress = AC_FAN_STRESS_FACTOR_MAX;

        // ---------------------------------------------------------------------
        // Previous-winner seed attempt.
        //
        // Try the Phase 2 winner from the previous step first. Neighbouring
        // speed steps tend to have similar optimal triplets, so this often
        // finds a good candidate immediately and allows us to skip the coarse
        // grid entirely. If it misses the Phase 1 tolerance window, we fall
        // through to the full grid as normal.
        //
        // The seed is not attempted on step 1 (prevWinner.stress == MAX
        // sentinel) or if the previous step produced no feasible result.
        // ---------------------------------------------------------------------
        bool skippedCoarseGrid = false;

        if ( prevWinner.stress < AC_FAN_STRESS_FACTOR_MAX ) {
            TripletResult seed =
                MeasureTriplet( prevWinner.phaseDelayUs, prevWinner.nOn, prevWinner.nOff, targetRPM, REPEAT_COUNT );

            Rpm lo = ( targetRPM > tolP1 ) ? ( targetRPM - tolP1 ) : 0;
            Rpm hi = targetRPM + tolP1;

            if ( seed.measuredRPM >= lo && seed.measuredRPM <= hi && seed.stress < AC_FAN_STRESS_FACTOR_MAX ) {
                // Previous winner is still valid for this step. Use it as the
                // Phase 1 seed and skip the coarse grid.
                bestP1 = seed;
                skippedCoarseGrid = true;
            }
        }

        // ---------------------------------------------------------------------
        // Phase 1: Coarse grid search — up to 75 triplets per step.
        //
        // Skipped entirely if the previous-winner seed succeeded.
        //
        // Alpha: 5 values evenly spaced across [MIN_ALPHA_US, MAX_ALPHA_US].
        //        With 5 steps: 1000, 2625, 4250, 5875, 7500 µs.
        // N_on:  5 values evenly spaced across [1, MAX_BURST_WINDOW].
        //        With 5 steps: 1, 8, 15, 22, 30.
        // N_off: 3 fixed values in ascending order: 0, 7, 15.
        //
        // Cross-step stall counters govern how each pair is treated:
        //   counter == 0 or 1  → full REPEAT_COUNT trials
        //   counter >= STALL_COUNT_REDUCE → REPEAT_COUNT_REDUCED trials
        //   counter >= STALL_COUNT_SKIP   → skip entirely, no measurement
        //
        // N_off loop bails on stall since kPhase1NoffValues is ascending —
        // higher N_off means less duty, which can only worsen a stall.
        // ---------------------------------------------------------------------
        if ( !skippedCoarseGrid ) {
            for ( uint8_t ai = 0; ai < PHASE1_ALPHA_STEPS; ai++ ) {
                uint16_t alpha =
                    (uint16_t)( AC_FAN_MIN_ALPHA_US + ( (uint32_t)ai * ( AC_FAN_MAX_ALPHA_US - AC_FAN_MIN_ALPHA_US ) ) /
                                                          ( PHASE1_ALPHA_STEPS - 1 ) );

                for ( uint8_t ni = 0; ni < PHASE1_NON_STEPS; ni++ ) {
                    uint8_t nOn =
                        (uint8_t)( 1 + ( (uint32_t)ni * ( AC_FAN_MAX_BURST_WINDOW - 1 ) ) / ( PHASE1_NON_STEPS - 1 ) );

                    uint8_t pairIdx = StallMap_Index( ai, ni );
                    uint8_t stallCount = StallMap_Get( stallMap, pairIdx );

                    // Skip pairs that have stalled on every previous step.
                    if ( stallCount >= STALL_COUNT_SKIP ) {
                        continue;
                    }

                    // Use reduced trial count for suspicious pairs.
                    uint8_t trials = ( stallCount >= STALL_COUNT_REDUCE ) ? REPEAT_COUNT_REDUCED : REPEAT_COUNT;

                    bool    pairStalled = false;

                    for ( uint8_t fi = 0; fi < PHASE1_NOFF_COUNT; fi++ ) {
                        uint8_t nOff = kPhase1NoffValues[ fi ];

#if PERIODIC_FLUSH
                        if ( tripletCount > 0 && ( tripletCount % FLUSH_INTERVAL_TRIPLETS ) == 0 ) {
                            PeriodicFlush();
                        }
                        tripletCount++;
#endif

                        TripletResult res = MeasureTriplet( alpha, nOn, nOff, targetRPM, trials );

                        // Stall detected: record it and bail the N_off loop.
                        // kPhase1NoffValues is ascending so remaining entries
                        // will only reduce duty further — pointless to continue.
                        if ( res.measuredRPM == 0 && res.stress == AC_FAN_STRESS_FACTOR_MAX ) {
                            pairStalled = true;
                            break;
                        }

                        Rpm lo = ( targetRPM > tolP1 ) ? ( targetRPM - tolP1 ) : 0;
                        Rpm hi = targetRPM + tolP1;

                        if ( res.measuredRPM >= lo && res.measuredRPM <= hi && res.stress < bestP1.stress ) {
                            bestP1 = res;
                        }
                    }

                    // Record this step's stall outcome for later counter update.
                    if ( pairStalled ) {
                        uint8_t bit = pairIdx; // 1 bit per pair for step flags
                        uint8_t word = bit >> 5u;
                        uint8_t shift = bit & 31u;
                        stepStallFlags[ word ] |= ( 1u << shift );
                    }
                }
            }

            // Update cross-step stall counters from this step's results.
            // Only pairs that stalled on this step get their counter incremented.
            for ( uint8_t p = 0; p < STALL_MAP_PAIRS; p++ ) {
                uint8_t word = p >> 5u;
                uint8_t shift = p & 31u;
                if ( stepStallFlags[ word ] & ( 1u << shift ) ) {
                    StallMap_Increment( stallMap, p );
                }
            }
        }

        // ---------------------------------------------------------------------
        // Phase 2: Fine neighbourhood search around the Phase 1 best.
        //
        // If Phase 1 found nothing (stress == STRESS_FACTOR_MAX), bestP1
        // holds the initial defaults — Phase 2 still runs around those, giving
        // each step one additional chance to find a usable configuration.
        //
        // Phase 2 does not consult the stall map — it operates in a tight
        // neighbourhood and stalls here are handled by the N_off bail-out only.
        // The stall map is a Phase 1 concept; Phase 2 is fine-grained enough
        // that false negatives from skipping would be more costly than the
        // measurements themselves.
        // ---------------------------------------------------------------------
        TripletResult bestP2 = bestP1;

        int32_t       aStart = (int32_t)bestP1.phaseDelayUs - PHASE2_ALPHA_RADIUS_US;
        int32_t       aEnd = (int32_t)bestP1.phaseDelayUs + PHASE2_ALPHA_RADIUS_US;
        int32_t       onStart = (int32_t)bestP1.nOn - PHASE2_NON_RADIUS;
        int32_t       onEnd = (int32_t)bestP1.nOn + PHASE2_NON_RADIUS;
        int32_t       offStart = (int32_t)bestP1.nOff - PHASE2_NOFF_RADIUS;
        int32_t       offEnd = (int32_t)bestP1.nOff + PHASE2_NOFF_RADIUS;

        for ( int32_t aRaw = aStart; aRaw <= aEnd; aRaw += PHASE2_ALPHA_STEP_US ) {
            uint16_t alpha = ClampAlpha( aRaw );

            for ( int32_t onRaw = onStart; onRaw <= onEnd; onRaw++ ) {
                uint8_t nOn = ClampNon( onRaw );

                for ( int32_t offRaw = offStart; offRaw <= offEnd; offRaw++ ) {
                    uint8_t nOff = ClampNoff( offRaw );

#if PERIODIC_FLUSH
                    if ( tripletCount > 0 && ( tripletCount % FLUSH_INTERVAL_TRIPLETS ) == 0 ) {
                        PeriodicFlush();
                    }
                    tripletCount++;
#endif

                    TripletResult res = MeasureTriplet( alpha, nOn, nOff, targetRPM, REPEAT_COUNT );

                    // Stall detected: bail out of the N_off loop. Increasing
                    // N_off further reduces duty and can only make this worse.
                    if ( res.measuredRPM == 0 && res.stress == AC_FAN_STRESS_FACTOR_MAX ) {
                        break;
                    }

                    Rpm lo = ( targetRPM > tolP2 ) ? ( targetRPM - tolP2 ) : 0;
                    Rpm hi = targetRPM + tolP2;

                    if ( res.measuredRPM >= lo && res.measuredRPM <= hi && res.stress < bestP2.stress ) {
                        bestP2 = res;
                    }
                }
            }
        }

        // ---------------------------------------------------------------------
        // Record the result. A slot is feasible only if stress < STRESS_FACTOR_MAX,
        // meaning at least one valid trial completed successfully.
        // Save bestP2 as the seed for the next step.
        // ---------------------------------------------------------------------
        bool feasible = ( bestP2.stress < AC_FAN_STRESS_FACTOR_MAX );

        mapOut->slots[ step ].isFeasible = feasible;
        mapOut->slots[ step ].actualRPM = bestP2.measuredRPM;
        mapOut->slots[ step ].strategy.phaseDelayUs = bestP2.phaseDelayUs;
        mapOut->slots[ step ].strategy.burstOn = bestP2.nOn;
        mapOut->slots[ step ].strategy.burstWindow = bestP2.nOn + bestP2.nOff;

        // Carry the winner forward as the seed for the next step, but only if
        // this step produced a feasible result. If it didn't, keep the previous
        // winner — it may still be a better seed than the unfeasible default.
        if ( feasible ) {
            prevWinner = bestP2;
        }
    }

    // -------------------------------------------------------------------------
    // Step 3: Enforce operational floor and assign slot 0.
    // -------------------------------------------------------------------------
    RotorStop();
    EnforceOperationalFloor( mapOut );
}

// ============================================================================
// PUBLIC API — RUNTIME DRIVE
// ============================================================================

// Drive the fan at requestedPm (0–1000 permille of motorMaxRPM).
//
// The profile map divides the speed range into AC_FAN_NUM_STEPS equal bands.
// Requests that fall exactly on a band boundary are applied directly.
// Requests between two boundaries are linearly interpolated across all three
// drive parameters (phaseDelayUs, burstOn, burstWindow).
//
// Special case: if one neighbour slot has burstOn=0 (motor off) and the other
// does not, interpolation would produce meaningless fractional values. We snap
// to whichever slot is nearer instead.
//
// Guard: after interpolation, burstOn is clamped to burstWindow. Independent
// interpolation of both fields can produce a rounding inconsistency where
// burstOn > burstWindow, which the TRIAC driver would reject.
void ACFanDrive( const ACFanProfileMapPtr map, Permille requestedPm ) {
    if ( !map || !s_triac ) {
        return;
    }

    if ( requestedPm > 1000 )
        requestedPm = 1000;

    // Zero — apply slot 0 (motor off).
    if ( requestedPm == 0 ) {
        TriacDriveParams p;
        p.phaseDelayUs = map->slots[ 0 ].strategy.phaseDelayUs;
        p.burstOn = map->slots[ 0 ].strategy.burstOn;
        p.burstWindow = map->slots[ 0 ].strategy.burstWindow;
        TriacRun( s_triac, p );
        return;
    }

    uint8_t lowerIdx = (uint8_t)( requestedPm / AC_FAN_STEP_INCREMENT_PM );
    uint8_t upperIdx = lowerIdx + 1;

    if ( lowerIdx > AC_FAN_NUM_STEPS )
        lowerIdx = AC_FAN_NUM_STEPS;
    if ( upperIdx > AC_FAN_NUM_STEPS )
        upperIdx = AC_FAN_NUM_STEPS;

    // Exact boundary — no interpolation needed.
    if ( lowerIdx == upperIdx || ( requestedPm % AC_FAN_STEP_INCREMENT_PM ) == 0 ) {
        TriacDriveParams p;
        p.phaseDelayUs = map->slots[ lowerIdx ].strategy.phaseDelayUs;
        p.burstOn = map->slots[ lowerIdx ].strategy.burstOn;
        p.burstWindow = map->slots[ lowerIdx ].strategy.burstWindow;
        TriacRun( s_triac, p );
        return;
    }

    const ACFanDriveParamsPtr low = &map->slots[ lowerIdx ].strategy;
    const ACFanDriveParamsPtr high = &map->slots[ upperIdx ].strategy;

    // Fractional position within the band in permille (0–1000).
    // e.g. requestedPm=150 with step=100 → lower=1, fraction=500.
    Permille                  fraction = (Permille)( ( requestedPm % AC_FAN_STEP_INCREMENT_PM ) * 10U );

    // Snap rather than interpolate across the on/off boundary.
    if ( ( low->burstOn == 0 ) != ( high->burstOn == 0 ) ) {
        const ACFanDriveParamsPtr chosen = ( fraction < 500 ) ? low : high;
        TriacDriveParams          p;
        p.phaseDelayUs = chosen->phaseDelayUs;
        p.burstOn = chosen->burstOn;
        p.burstWindow = chosen->burstWindow;
        TriacRun( s_triac, p );
        return;
    }

    // Standard linear interpolation.
    TriacDriveParams p;
    p.phaseDelayUs = (uint16_t)InterpLinear( (int32_t)low->phaseDelayUs, (int32_t)high->phaseDelayUs, fraction );
    p.burstOn = (uint8_t)InterpLinear( (int32_t)low->burstOn, (int32_t)high->burstOn, fraction );
    p.burstWindow = (uint8_t)InterpLinear( (int32_t)low->burstWindow, (int32_t)high->burstWindow, fraction );

    // Guard against burstOn > burstWindow due to independent rounding.
    if ( p.burstOn > p.burstWindow ) {
        p.burstOn = p.burstWindow;
    }

    TriacRun( s_triac, p );
}