/// @file ACFanTuning.c
///
/// @brief AC fan calibration and runtime drive engine — implementation.
///
/// Implements a two-phase grid search that maps evenly-spaced speed targets
/// (10 %..100 % of `motorMaxRPM`) to the lowest-stress TRIAC drive parameters
/// that achieve each target. See `ACFanTuning.h` for the public API.
///
/// Design notes:
///   - All blocking delays use `osDelay()` — this file assumes a FreeRTOS task
///     context throughout.
///   - `REProcess()` is called immediately before every `REGetVelocity()` read,
///     followed by `osDelay(10)` to allow the async I2C transaction to complete.
///     This is conservative but safe given FreeRTOS tick resolution.
///   - `REGetVelocity()` returns `Rpm` (uint16_t), already scaled to RPM inside
///     the driver. Spuriously high values from corrupt I2C reads are clamped to
///     zero. No sign reinterpretation is needed.
///   - `ACFanDriveParams` is the internal strategy type. Conversion to
///     `TriacDriveParams` occurs only at the point of calling `TriacRun()`.
///     `burstWindow = burstOn + nOff` — the TRIAC driver expects the total
///     window size, not a separate off count.
///   - `MeasureTriplet()` bails out immediately on the first stall rather than
///     grinding through all REPEAT_COUNT trials. This protects the motor
///     windings from repeated full-power stall current.
///   - Stall memory: a 2-bit saturating counter per (alpha, nOn) pair tracks
///     stalls across steps. Pairs with counter >= STALL_COUNT_SKIP are skipped
///     entirely; pairs with counter >= STALL_COUNT_REDUCE use only 1 trial.
///     `RotorStop()` before every trial means stalls are genuine, not artefacts
///     of a still-spinning rotor, so counters are meaningful.
///   - Previous-step winner seeding: the Phase 2 winner from the previous step
///     is tried first at the start of each new step. If it lands within Phase 1
///     tolerance the coarse grid is skipped entirely for that step.
///   - PERIODIC_FLUSH: when enabled, the motor is run at full speed for a short
///     burst every FLUSH_INTERVAL_TRIPLETS triplets during calibration. This
///     promotes airflow-based cooling of the windings. Disabled by default —
///     enable only if thermal testing shows it is necessary.
///   - No dynamic memory allocation is used anywhere in this file.
///   - No flash writes are performed. The caller owns `ACFanProfileMap`.
///
/// @copyright Copyright (c) 2026 Tim Hosking
/// @see https://github.com/munger
/// @par Licence: MIT

#include "Features.h"

#if FEATURE_AC_FAN_CALIBRATION

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>  // abs()
#include <string.h>

#include "cmsis_os.h"

#include "ACFanTuning.h"
#include "RotaryEncoder.h"
#include "Triac.h"

// ============================================================================
// HARDWARE SELECTION
// ============================================================================

/// @brief TRIAC channel used by this module.
static const TriacID kAcFanTriacId = TriacOvenFan;

// ============================================================================
// CALIBRATION CONSTANTS
// ============================================================================

// ============================================================================
// Calibration timing and threshold constants
// ============================================================================

static const uint16_t   kAcFanMinAlphaUs    = 1000U;  ///< TRIAC delay at maximum power (minimum delay).
static const uint16_t   kAcFanMaxAlphaUs    = 7500U;  ///< TRIAC delay at minimum power (maximum delay).
static const uint8_t    kAcFanMaxBurstWindow = 30U;   ///< Maximum burst window depth in half-cycles.
static const uint8_t    kRepeatCount        = 3U;     ///< Trial repetitions per triplet under normal conditions.
static const uint8_t    kRepeatCountReduced = 1U;     ///< Reduced trials for pairs with elevated stall history.
static const DurationMs kSpinupTimeoutMs    = 600U;   ///< Full-power spinup timeout before stall is declared.
static const DurationMs kStallTimeoutMs     = 100U;   ///< No-movement window within spinup before stall is declared.
static const DurationMs kSettleTimeoutMs    = 10000U; ///< Hard settle timeout; unsettled → oscillation penalty.
static const uint16_t   kSettleP2pThreshold = 20U;   ///< Peak-to-peak RPM spread considered stable.
enum { kSettleWindowSize = 10 };                       ///< Rolling settle window depth (samples 100 ms apart).
static const DurationMs kJerkPollMs         = 5U;    ///< Inter-sample interval for jerk measurement.
static const DurationMs kJerkWindowMs       = 1000U; ///< Duration of the peak-jerk sampling window.
static const DurationMs kSpindownPollMs     = 50U;   ///< Poll interval while waiting for rotor to coast to rest.
static const Rpm        kSpindownRpmThreshold = 2U;  ///< RPM below which rotor is considered fully stopped.
static const DurationMs kMaxRpmSettleMs     = 5000U; ///< Full-power soak before sampling motorMaxRPM.

// ============================================================================
// STRESS MODEL CONSTANTS
// ============================================================================

// ============================================================================
// Stress model constants
// ============================================================================

/// @warning kAcFanJerkCeiling must be determined empirically. Default is a conservative starting point.
static const uint32_t kAcFanJerkCeiling     = 1000U;      ///< Jerk ceiling in RPM/interval² — maps to full-scale stress.
static const uint32_t kAcFanStressFactorMax = 1000000UL;  ///< Stress cap; any result at or above this is unfeasible.
static const uint32_t kWeightJerkPm         = 600U;       ///< Jerk stress weight in permille (must sum to 1000 with kWeightSlipPm).
static const uint32_t kWeightSlipPm         = 400U;       ///< Slip stress weight in permille (must sum to 1000 with kWeightJerkPm).
static const uint32_t kOscillationPenalty   = 50U;        ///< Extra stress per RPM of p2p spread when motor does not settle.
static const Rpm      kSyncSpeedRpm         = 3000U;      ///< Synchronous speed for a 2-pole motor on 50 Hz mains.

// ============================================================================
// SEARCH GRID CONSTANTS
// ============================================================================

// ============================================================================
// Search grid constants
// ============================================================================

enum { kPhase1AlphaSteps = 5 }; ///< Alpha (phase-angle) values in the Phase 1 coarse grid.
enum { kPhase1NonSteps   = 5 }; ///< nOn (burst duty) values in the Phase 1 coarse grid.
enum { kPhase1NoffCount  = 3 }; ///< nOff (burst gap) values in the Phase 1 coarse grid.

/// @brief Fixed nOff values for Phase 1, in ascending order.
///
/// The nOff loop bails on stall since kPhase1NoffValues is ascending —
/// higher indices mean less duty, which can only worsen a stall.
/// Do not reorder.
static const uint8_t kPhase1NoffValues[ kPhase1NoffCount ] = { 0, 7, 15 };

static const uint16_t kPhase1RpmTolerancePm = 50U;  ///< Phase 1 RPM acceptance tolerance in permille (±5%).
static const uint32_t kPhase2AlphaRadiusUs  = 1000U; ///< Phase 2 fine-search alpha radius in microseconds.
static const uint32_t kPhase2AlphaStepUs    = 250U;  ///< Phase 2 alpha step size in microseconds.
static const uint8_t  kPhase2NonRadius      = 3U;   ///< Phase 2 nOn radius in half-cycle steps.
static const uint8_t  kPhase2NoffRadius     = 3U;   ///< Phase 2 nOff radius in half-cycle steps.
static const uint16_t kPhase2RpmTolerancePm = 20U;  ///< Phase 2 RPM acceptance tolerance in permille (±2%).
static const uint16_t kAcFanStepIncrementPm = 100U; ///< Speed increment between profile slots in permille.

// ============================================================================
// STALL MEMORY CONSTANTS
// ============================================================================

// ============================================================================
// Stall memory constants
// ============================================================================

static const uint8_t kStallCountReduce = 2U; ///< Stall count at which a pair switches to reduced trial count.
static const uint8_t kStallCountSkip   = 3U; ///< Stall count at which a pair is skipped entirely.

/// @brief Total (alpha, nOn) pairs tracked in the stall map (= kPhase1AlphaSteps * kPhase1NonSteps).
enum { kStallMapPairs = kPhase1AlphaSteps * kPhase1NonSteps };

/// @brief uint32_t words needed to store kStallMapPairs 2-bit saturating counters.
enum { kStallMapWords = 2 };

/// @brief Read the 2-bit saturating stall counter for pair index @p p.
/// @param[in] map  Packed counter array (kStallMapWords words).
/// @param[in] p    Flat pair index, range [0, STALL_MAP_PAIRS).
/// @return Counter value in range [0, 3].
static inline uint8_t StallMapGet( const uint32_t map[ kStallMapWords ], uint8_t p ) {
    uint8_t bit   = (uint8_t)( p * 2u );
    uint8_t word  = bit >> 5u;   // divide by 32
    uint8_t shift = bit & 31u;   // modulo 32
    return (uint8_t)( ( map[ word ] >> shift ) & 0x3u );
}

/// @brief Increment the 2-bit saturating stall counter for pair index @p p, clamping at 3.
/// @param[in,out] map  Packed counter array (kStallMapWords words).
/// @param[in]     p    Flat pair index, range [0, STALL_MAP_PAIRS).
static inline void StallMapIncrement( uint32_t map[ kStallMapWords ], uint8_t p ) {
    uint8_t bit   = (uint8_t)( p * 2u );
    uint8_t word  = bit >> 5u;
    uint8_t shift = bit & 31u;
    uint8_t val   = (uint8_t)( ( map[ word ] >> shift ) & 0x3u );
    if ( val < 3u ) {
        map[ word ] &= ~( 0x3u << shift );                        // clear the 2 bits
        map[ word ] |= ( (uint32_t)( val + 1u ) << shift );       // write incremented value
    }
}

/// @brief Compute the flat pair index from a (alphaIndex, nOnIndex) coordinate.
/// @param[in] ai  Alpha grid index [0, PHASE1_ALPHA_STEPS).
/// @param[in] ni  nOn grid index [0, PHASE1_NON_STEPS).
/// @return Flat pair index for use with StallMapGet() / StallMapIncrement().
static inline uint8_t StallMapIndex( uint8_t ai, uint8_t ni ) {
    return (uint8_t)( ai * kPhase1NonSteps + ni );
}

// ============================================================================
// PERIODIC FLUSH FEATURE
// ============================================================================

/// @brief Set to 1 to enable periodic full-speed cooling flushes during calibration.
///
/// When enabled, the motor is run at full power for FLUSH_DURATION_MS every
/// FLUSH_INTERVAL_TRIPLETS triplets, promoting airflow-based winding cooling.
/// Enable only if thermal testing shows the motor is accumulating heat during
/// the calibration run. The flush adds time but does not affect profile quality.
#define PERIODIC_FLUSH 0

#if PERIODIC_FLUSH
/// @brief Number of triplet measurements between cooling flushes.
#define FLUSH_INTERVAL_TRIPLETS 20

/// @brief Duration of each full-speed cooling flush in milliseconds.
#define FLUSH_DURATION_MS 3000
#endif  // PERIODIC_FLUSH

// ============================================================================
// MODULE STATE
// ============================================================================

/// @brief TRIAC channel handle acquired in ACFanInitCalibration().
static TriacRef         s_triac   = NULL;

/// @brief Rotary encoder handle acquired in ACFanInitCalibration().
static RotaryEncoderRef s_encoder = NULL;

// ============================================================================
// INTERNAL — RPM READING
// ============================================================================

/// @brief Trigger one I2C state-machine cycle and return a clamped RPM reading.
///
/// Calls `REProcess()` to initiate the AS5600 angle register read, then waits
/// 10 ms for the async DMA/interrupt transaction to complete before reading
/// the cached velocity. Any value above 10000 RPM is implausible for a domestic
/// oven fan and is clamped to zero to discard corrupt I2C results.
///
/// @return Measured rotor velocity in RPM, or 0 if the reading was implausible.
static Rpm ReadRPM( void ) {
    REProcess();
    osDelay( 10 );
    Rpm v = REGetVelocity( s_encoder );
    return ( v > 10000u ) ? (Rpm)0 : v;
}

// ============================================================================
// INTERNAL — ROTOR CONTROL
// ============================================================================

/// @brief Cut TRIAC power and block until the rotor velocity drops below SPINDOWN_RPM_THRESHOLD.
///
/// Polls at SPINDOWN_POLL_MS intervals. No timeout is applied — the motor must
/// physically coast to rest before calibration of the next triplet can begin.
static void RotorStop( void ) {
    TriacOff( s_triac );
    while ( ReadRPM() > kSpindownRpmThreshold ) {
        osDelay( kSpindownPollMs );
    }
}

/// @brief Apply full gate power and wait for the rotor to reach 90 % of @p targetRPM.
///
/// Used at the start of every trial to bring the motor to speed before switching
/// to the candidate drive parameters. Bails out immediately if no movement is
/// detected within STALL_TIMEOUT_MS to protect the windings from stall current.
///
/// @param[in] targetRPM  The speed target for the current calibration step.
/// @return True if the intercept velocity (90 % of targetRPM) was reached or
///         if the motor is moving at timeout; false if the motor stalled.
static bool StartupKick( Rpm targetRPM ) {
    TriacDriveParams fullPower = { .phaseDelayUs = 0, .burstOn = 1, .burstWindow = 1 };
    TriacRun( s_triac, fullPower );

    // uint32_t intermediate prevents overflow before division.
    Rpm        interceptRPM = (Rpm)( ( (uint32_t)targetRPM * 900UL ) / 1000UL );
    DurationMs elapsed      = 0;

    while ( elapsed < kSpinupTimeoutMs ) {
        osDelay( 2 );
        elapsed += 2;

        Rpm current = ReadRPM();

        if ( elapsed >= kStallTimeoutMs && current < kSpindownRpmThreshold ) {
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
/// @brief Run the motor at full speed for FLUSH_DURATION_MS to promote winding cooling.
///
/// Stops the rotor cleanly before returning so the next measurement starts
/// from a known rest state.
static void PeriodicFlush( void ) {
    TriacDriveParams fullPower = { .phaseDelayUs = 0, .burstOn = 1, .burstWindow = 1 };
    TriacRun( s_triac, fullPower );
    osDelay( FLUSH_DURATION_MS );
    RotorStop();
}
#endif  // PERIODIC_FLUSH

// ============================================================================
// INTERNAL — SETTLE DETECTION
// ============================================================================

/// @brief Wait for the rotor RPM to stabilise within SETTLE_P2P_THRESHOLD.
///
/// Accumulates samples into a rolling window of SETTLE_WINDOW_SIZE entries taken
/// 100 ms apart. The window is considered settled when the peak-to-peak spread
/// across all entries falls below SETTLE_P2P_THRESHOLD.
///
/// @param[out] settledRPM  On success: midpoint of the settled window.
///                         On timeout: most recent raw reading.
/// @param[out] p2pSpread   Most recent peak-to-peak spread across the window.
/// @return True if RPM settled within SETTLE_TIMEOUT_MS; false on timeout.
static bool WaitForSettle( Rpm* settledRPM, uint16_t* p2pSpread ) {
    Rpm        window[ kSettleWindowSize ];
    uint8_t    wIdx    = 0;
    DurationMs elapsed = 0;

    memset( window, 0, sizeof( window ) );
    *p2pSpread  = 0xFFFFU;
    *settledRPM = 0;

    while ( elapsed < kSettleTimeoutMs ) {
        window[ wIdx % kSettleWindowSize ] = ReadRPM();
        wIdx++;

        // Evaluate only once the window is fully populated (1 second of data).
        if ( elapsed >= (DurationMs)( kSettleWindowSize * 100U ) ) {
            Rpm minR = 0xFFFFU, maxR = 0;

            for ( uint8_t i = 0; i < kSettleWindowSize; i++ ) {
                if ( window[ i ] < minR ) minR = window[ i ];
                if ( window[ i ] > maxR ) maxR = window[ i ];
            }

            *p2pSpread = maxR - minR;

            if ( *p2pSpread <= kSettleP2pThreshold ) {
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

/// @brief Sample rotor velocity every JERK_POLL_MS over @p durationMs and return peak jerk.
///
/// Jerk is the discrete second derivative of velocity:
/// @code
///   acceleration[n] = (velocity[n] - velocity[n-1]) / dt
///   jerk[n]         = (acceleration[n] - acceleration[n-1]) / dt
/// @endcode
/// Units are RPM/interval² (where interval = JERK_POLL_MS ms). Direction is
/// discarded — only the magnitude of mechanical shock is relevant. The absolute
/// scale is absorbed into AC_FAN_JERK_CEILING, which must be tuned empirically.
///
/// @param[in] durationMs  Sampling window length in milliseconds.
/// @return Peak absolute jerk value observed over the window.
static uint32_t MeasurePeakJerk( DurationMs durationMs ) {
    uint32_t   peakJerk     = 0;
    int32_t    lastVelocity = (int32_t)ReadRPM();
    int32_t    lastAccel    = 0;
    DurationMs elapsed      = 0;

    while ( elapsed < durationMs ) {
        osDelay( kJerkPollMs );
        elapsed += kJerkPollMs;

        int32_t  currentVelocity = (int32_t)ReadRPM();
        int32_t  currentAccel    = ( currentVelocity - lastVelocity ) / (int32_t)kJerkPollMs;
        int32_t  jerkRaw         = ( currentAccel - lastAccel ) / (int32_t)kJerkPollMs;
        uint32_t currentJerk     = (uint32_t)abs( jerkRaw );

        if ( currentJerk > peakJerk ) {
            peakJerk = currentJerk;
        }

        lastVelocity = currentVelocity;
        lastAccel    = currentAccel;
    }

    return peakJerk;
}

// ============================================================================
// INTERNAL — STRESS CALCULATION
// ============================================================================

/// @brief Compute a dimensionless stress factor for a given operating point.
///
/// Two components are combined with fixed weights:
///   - Slip stress (40 %): fraction of synchronous speed lost to slip.
///     Higher slip means more rotor heating and torque ripple.
///   - Jerk stress (60 %): peak |d²ω/dt²| as a fraction of AC_FAN_JERK_CEILING.
///     Higher jerk means more mechanical shock on bearings.
///
/// If the motor did not settle, an oscillation penalty proportional to the
/// peak-to-peak RPM spread is added, making unstable operating points strongly
/// unfavourable regardless of their average RPM.
///
/// @param[in] rpm        Settled rotor velocity in RPM.
/// @param[in] peakJerk   Peak jerk measured by MeasurePeakJerk().
/// @param[in] p2pSpread  Peak-to-peak RPM spread from the settle window.
/// @param[in] settled    True if RPM settled within the timeout; false if it oscillated.
/// @return Stress factor in range [0, kAcFanStressFactorMax].
static StressFactor CalculateStress( Rpm rpm, uint32_t peakJerk, uint16_t p2pSpread, bool settled ) {
    // Slip: distance below synchronous speed, clamped to zero if above.
    int32_t slipVal = (int32_t)kSyncSpeedRpm - (int32_t)rpm;
    if ( slipVal < 0 ) slipVal = 0;
    Permille slipPm = (Permille)( ( (uint32_t)slipVal * 1000UL ) / kSyncSpeedRpm );

    // Jerk: clamp to ceiling before scaling to prevent overflow.
    if ( peakJerk > kAcFanJerkCeiling ) peakJerk = kAcFanJerkCeiling;
    Permille jerkPm = (Permille)( ( peakJerk * 1000UL ) / kAcFanJerkCeiling );

    // Weighted sum: 0–1,000,000. Scaled by 100 gives 0–100,000,000.
    uint32_t weightedSum = ( (uint32_t)jerkPm * kWeightJerkPm ) + ( (uint32_t)slipPm * kWeightSlipPm );
    uint32_t baseStress  = weightedSum * 100UL;

    if ( !settled ) {
        baseStress += (uint32_t)p2pSpread * kOscillationPenalty;
    }

    return (StressFactor)baseStress;
}

// ============================================================================
// INTERNAL — TRIPLET APPLICATION
// ============================================================================

/// @brief Convert an internal (phaseDelayUs, nOn, nOff) triplet to TriacDriveParams and apply it.
///
/// This is the single translation point in the file between the internal parameter
/// representation and the TRIAC driver's own type. The driver expects `burstWindow`
/// to be the total window size (nOn + nOff), not a separate off count.
///
/// @param[in] phaseDelayUs  Phase-angle delay in microseconds.
/// @param[in] nOn           Number of firing half-cycles per burst.
/// @param[in] nOff          Number of idle half-cycles per burst.
static void ApplyParams( uint16_t phaseDelayUs, uint8_t nOn, uint8_t nOff ) {
    TriacDriveParams p;
    p.phaseDelayUs = phaseDelayUs;
    p.burstOn      = nOn;
    p.burstWindow  = nOn + nOff;  // driver expects total window, not separate off count
    TriacRun( s_triac, p );
}

// ============================================================================
// INTERNAL — TRIPLET MEASUREMENT
// ============================================================================

/// @brief Internal result of a single triplet evaluation.
typedef struct {
    uint16_t     phaseDelayUs; ///< Phase-angle delay tested.
    uint8_t      nOn;          ///< Burst on-count tested.
    uint8_t      nOff;         ///< Burst off-count tested.
    Rpm          measuredRPM;  ///< Average settled RPM across valid trials.
    StressFactor stress;       ///< Average stress across valid trials.
} TripletResult;

/// @brief Measure a triplet by running up to @p repeatCount trials and averaging the results.
///
/// On the first stall (`StartupKick()` returns false), the function returns
/// immediately with `measuredRPM=0` and `stress=kAcFanStressFactorMax`,
/// protecting the motor from repeated full-power stall current. `repeatCount` is
/// passed explicitly so the caller can reduce it for pairs with a suspicious stall
/// history without changing the global default.
///
/// @param[in] phaseDelayUs  Phase-angle delay in microseconds.
/// @param[in] nOn           Burst on-count.
/// @param[in] nOff          Burst off-count.
/// @param[in] targetRPM     Target RPM for the current calibration step (used by StartupKick).
/// @param[in] repeatCount   Maximum number of trials to average.
/// @return Averaged TripletResult. On stall: measuredRPM=0, stress=kAcFanStressFactorMax.
static TripletResult MeasureTriplet( uint16_t phaseDelayUs, uint8_t nOn, uint8_t nOff, Rpm targetRPM, uint8_t repeatCount ) {
    TripletResult res;
    res.phaseDelayUs = phaseDelayUs;
    res.nOn          = nOn;
    res.nOff         = nOff;
    res.measuredRPM  = 0;
    res.stress       = kAcFanStressFactorMax;

    uint32_t sumRPM    = 0;
    uint32_t sumStress = 0;
    uint8_t  valid     = 0;

    for ( uint8_t r = 0; r < repeatCount; r++ ) {
        RotorStop();

        if ( !StartupKick( targetRPM ) ) {
            // First stall — bail immediately. This triplet cannot sustain
            // rotation. Further trials would only pump more stall current
            // into the windings without any prospect of a useful result.
            return res;
        }

        ApplyParams( phaseDelayUs, nOn, nOff );

        Rpm      settledRPM = 0;
        uint16_t p2p        = 0xFFFFU;
        bool     settled    = WaitForSettle( &settledRPM, &p2p );
        uint32_t peakJerk   = MeasurePeakJerk( kJerkWindowMs );

        StressFactor s = CalculateStress( settledRPM, peakJerk, p2p, settled );

        sumRPM    += settledRPM;
        sumStress += s;
        valid++;
    }

    if ( valid > 0 ) {
        res.measuredRPM = (Rpm)( sumRPM / valid );
        res.stress      = (StressFactor)( sumStress / valid );
    }

    return res;
}

// ============================================================================
// INTERNAL — CLAMPING HELPERS
// ============================================================================

/// @brief Clamp a raw alpha value to [kAcFanMinAlphaUs, kAcFanMaxAlphaUs].
/// @param[in] v  Raw signed value from the Phase 2 neighbourhood sweep.
/// @return Clamped uint16_t alpha in microseconds.
static uint16_t ClampAlpha( int32_t v ) {
    if ( v < kAcFanMinAlphaUs ) return (uint16_t)kAcFanMinAlphaUs;
    if ( v > kAcFanMaxAlphaUs ) return (uint16_t)kAcFanMaxAlphaUs;
    return (uint16_t)v;
}

/// @brief Clamp a raw nOn value to [1, kAcFanMaxBurstWindow].
/// @param[in] v  Raw signed value from the Phase 2 neighbourhood sweep.
/// @return Clamped uint8_t nOn (minimum 1 to keep the gate firing).
static uint8_t ClampNon( int32_t v ) {
    if ( v < 1 )                       return 1;
    if ( v > kAcFanMaxBurstWindow ) return (uint8_t)kAcFanMaxBurstWindow;
    return (uint8_t)v;
}

/// @brief Clamp a raw nOff value to [0, kAcFanMaxBurstWindow].
/// @param[in] v  Raw signed value from the Phase 2 neighbourhood sweep.
/// @return Clamped uint8_t nOff (0 = continuous firing within the window).
static uint8_t ClampNoff( int32_t v ) {
    if ( v < 0 )                       return 0;
    if ( v > kAcFanMaxBurstWindow ) return (uint8_t)kAcFanMaxBurstWindow;
    return (uint8_t)v;
}

// ============================================================================
// INTERNAL — LINEAR INTERPOLATION
// ============================================================================

/// @brief Linearly interpolate between @p low and @p high at fractional position @p fractionPm.
/// @param[in] low        Value at fraction = 0.
/// @param[in] high       Value at fraction = 1000.
/// @param[in] fractionPm Fractional position in permille (0–1000).
/// @return Interpolated integer value.
static int32_t InterpLinear( int32_t low, int32_t high, Permille fractionPm ) {
    return low + ( ( ( high - low ) * (int32_t)fractionPm ) / 1000 );
}

// ============================================================================
// INTERNAL — OPERATIONAL FLOOR ENFORCEMENT
// ============================================================================

/// @brief Back-fill unfeasible low-speed slots with the lowest feasible strategy.
///
/// Ensures the runtime interpolator always has a valid drive configuration for
/// every slot. Slot 0 is unconditionally set to motor-off:
///   - `phaseDelayUs = kAcFanMaxAlphaUs` (most restrictive phase angle)
///   - `burstOn = 0`       (gate never fires)
///   - `burstWindow = 1`   (non-zero to avoid division-by-zero in the driver)
///
/// @param[in,out] map  Profile map to patch in place.
static void EnforceOperationalFloor( ACFanProfileMapPtr map ) {
    int8_t lowest = -1;

    for ( uint8_t step = 1; step <= kAcFanNumSteps; step++ ) {
        if ( map->slots[ step ].isFeasible ) {
            lowest = (int8_t)step;
            break;
        }
    }

    if ( lowest > 1 ) {
        for ( uint8_t step = 1; step < (uint8_t)lowest; step++ ) {
            map->slots[ step ].strategy   = map->slots[ lowest ].strategy;
            map->slots[ step ].actualRPM  = map->slots[ lowest ].actualRPM;
            map->slots[ step ].isFeasible = true;
        }
    }

    map->slots[ 0 ].strategy.phaseDelayUs = kAcFanMaxAlphaUs;
    map->slots[ 0 ].strategy.burstOn      = 0;
    map->slots[ 0 ].strategy.burstWindow  = 1;
    map->slots[ 0 ].actualRPM             = 0;
    map->slots[ 0 ].isFeasible            = true;
}

// ============================================================================
// PUBLIC API — INIT
// ============================================================================

/// @brief Initialise the AC fan tuning module.
///
/// Acquires the TRIAC channel (AC_FAN_TRIAC_ID) and the rotary encoder handle.
/// Must be called once before ACFanRunCalibration() or ACFanDrive().
void ACFanInitCalibration( void ) {
    s_triac   = TriacOpen( kAcFanTriacId );
    s_encoder = REGetRef( RotaryEncoder1 );
}

// ============================================================================
// PUBLIC API — CALIBRATION
// ============================================================================

/// @brief Run the full calibration sequence and populate @p mapOut.
///
/// Algorithm overview:
///   1. Measure `motorMaxRPM` at full power after MAX_RPM_SETTLE_MS.
///   2. For each speed step 1..kAcFanNumSteps:
///      a. Try the Phase 2 winner from the previous step as a seed. If it lands
///         within Phase 1 tolerance the coarse grid is skipped entirely.
///      b. Phase 1 (coarse): up to 75 triplets across a sparse 5×5×3 grid, ±5 % tolerance.
///         Pairs are skipped or reduced based on their cross-step stall counter.
///         The nOff loop bails on stall since higher nOff only reduces duty further.
///      c. Phase 2 (fine): dense neighbourhood around the Phase 1 best, ±2 % tolerance.
///         The nOff loop bails on stall for the same reason.
///      d. Update cross-step stall counters for any pair that stalled.
///      e. Record the Phase 2 winner as the profile entry for this step.
///   3. Enforce the operational floor and assign slot 0 (motor-off).
///
/// @note Blocks for up to approximately 45 minutes. Must be called from a
///       FreeRTOS task context.
/// @warning The caller must persist mapOut to non-volatile storage after return.
void ACFanRunCalibration( ACFanProfileMapPtr mapOut ) {
    if ( !mapOut || !s_triac || !s_encoder ) {
        return;
    }

    memset( mapOut, 0, sizeof( ACFanProfileMap ) );

    // Cross-step stall memory: 2-bit saturating counters for each
    // (alphaIndex, nOnIndex) pair in the Phase 1 grid.
    // Persists across all steps; cleared only at the start of calibration.
    uint32_t stallMap[ kStallMapWords ] = { 0, 0 };

    // Previous step's Phase 2 winner. Used to seed the next step's search.
    // Initialised to an invalid sentinel (stress == MAX) so the first step
    // always falls through to the full coarse grid.
    TripletResult prevWinner;
    prevWinner.phaseDelayUs = kAcFanMinAlphaUs;
    prevWinner.nOn          = 1;
    prevWinner.nOff         = 0;
    prevWinner.measuredRPM  = 0;
    prevWinner.stress       = kAcFanStressFactorMax;

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
        osDelay( kMaxRpmSettleMs );
        mapOut->motorMaxRPM = ReadRPM();
        TriacOff( s_triac );
    }

    Rpm motorMax = mapOut->motorMaxRPM;

    if ( motorMax < 10 ) {
        return;  // Could not measure a meaningful max RPM — abort.
    }

    // -------------------------------------------------------------------------
    // Step 2: Per-step calibration loop.
    // -------------------------------------------------------------------------
    for ( uint8_t step = 1; step <= kAcFanNumSteps; step++ ) {
        Rpm targetRPM = (Rpm)( ( (uint32_t)motorMax * step * kAcFanStepIncrementPm ) / 1000UL );
        Rpm tolP1     = (Rpm)( ( (uint32_t)motorMax * kPhase1RpmTolerancePm ) / 1000UL );
        Rpm tolP2     = (Rpm)( ( (uint32_t)motorMax * kPhase2RpmTolerancePm ) / 1000UL );

        // Stall flags for Phase 1 pairs on this step only. Used to update
        // the cross-step stallMap after Phase 1 completes.
        // One bit per pair; bit set means this step produced a stall.
        uint32_t stepStallFlags[ kStallMapWords ] = { 0, 0 };

        // Initialise best candidates with worst-case sentinel values.
        TripletResult bestP1;
        bestP1.phaseDelayUs = kAcFanMinAlphaUs;
        bestP1.nOn          = 1;
        bestP1.nOff         = 0;
        bestP1.measuredRPM  = 0;
        bestP1.stress       = kAcFanStressFactorMax;

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

        if ( prevWinner.stress < kAcFanStressFactorMax ) {
            TripletResult seed = MeasureTriplet( prevWinner.phaseDelayUs,
                                                 prevWinner.nOn,
                                                 prevWinner.nOff,
                                                 targetRPM,
                                                 kRepeatCount );

            Rpm lo = ( targetRPM > tolP1 ) ? ( targetRPM - tolP1 ) : 0;
            Rpm hi = targetRPM + tolP1;

            if ( seed.measuredRPM >= lo && seed.measuredRPM <= hi && seed.stress < kAcFanStressFactorMax ) {
                // Previous winner is still valid for this step. Use it as the
                // Phase 1 seed and skip the coarse grid.
                bestP1             = seed;
                skippedCoarseGrid  = true;
            }
        }

        // ---------------------------------------------------------------------
        // Phase 1: Coarse grid search — up to 75 triplets per step.
        //
        // Skipped entirely if the previous-winner seed succeeded.
        //
        // Alpha: 5 values evenly spaced across [MIN_ALPHA_US, MAX_ALPHA_US].
        //        With 5 steps: 1000, 2625, 4250, 5875, 7500 µs.
        // nOn:   5 values evenly spaced across [1, MAX_BURST_WINDOW].
        //        With 5 steps: 1, 8, 15, 22, 30.
        // nOff:  3 fixed values in ascending order: 0, 7, 15.
        //
        // Cross-step stall counters govern how each pair is treated:
        //   counter == 0 or 1  → full kRepeatCount trials
        //   counter >= kStallCountReduce → kRepeatCountReduced trials
        //   counter >= kStallCountSkip   → skip entirely, no measurement
        //
        // nOff loop bails on stall since kPhase1NoffValues is ascending —
        // higher nOff means less duty, which can only worsen a stall.
        // ---------------------------------------------------------------------
        if ( !skippedCoarseGrid ) {
            for ( uint8_t ai = 0; ai < kPhase1AlphaSteps; ai++ ) {
                uint16_t alpha = (uint16_t)( kAcFanMinAlphaUs +
                                             ( (uint32_t)ai * ( kAcFanMaxAlphaUs - kAcFanMinAlphaUs ) ) /
                                                 ( kPhase1AlphaSteps - 1 ) );

                for ( uint8_t ni = 0; ni < kPhase1NonSteps; ni++ ) {
                    uint8_t nOn = (uint8_t)( 1 + ( (uint32_t)ni * ( kAcFanMaxBurstWindow - 1 ) ) /
                                                      ( kPhase1NonSteps - 1 ) );

                    uint8_t pairIdx    = StallMapIndex( ai, ni );
                    uint8_t stallCount = StallMapGet( stallMap, pairIdx );

                    // Skip pairs that have stalled on every previous step.
                    if ( stallCount >= kStallCountSkip ) {
                        continue;
                    }

                    // Use reduced trial count for suspicious pairs.
                    uint8_t trials = ( stallCount >= kStallCountReduce ) ? kRepeatCountReduced : kRepeatCount;

                    bool pairStalled = false;

                    for ( uint8_t fi = 0; fi < kPhase1NoffCount; fi++ ) {
                        uint8_t nOff = kPhase1NoffValues[ fi ];

#if PERIODIC_FLUSH
                        if ( tripletCount > 0 && ( tripletCount % FLUSH_INTERVAL_TRIPLETS ) == 0 ) {
                            PeriodicFlush();
                        }
                        tripletCount++;
#endif

                        TripletResult res = MeasureTriplet( alpha, nOn, nOff, targetRPM, trials );

                        // Stall detected: record it and bail the nOff loop.
                        // kPhase1NoffValues is ascending so remaining entries
                        // will only reduce duty further — pointless to continue.
                        if ( res.measuredRPM == 0 && res.stress == kAcFanStressFactorMax ) {
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
                        uint8_t bit   = pairIdx;  // 1 bit per pair for step flags
                        uint8_t word  = bit >> 5u;
                        uint8_t shift = bit & 31u;
                        stepStallFlags[ word ] |= ( 1u << shift );
                    }
                }
            }

            // Update cross-step stall counters from this step's results.
            // Only pairs that stalled on this step get their counter incremented.
            for ( uint8_t p = 0; p < kStallMapPairs; p++ ) {
                uint8_t word  = p >> 5u;
                uint8_t shift = p & 31u;
                if ( stepStallFlags[ word ] & ( 1u << shift ) ) {
                    StallMapIncrement( stallMap, p );
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
        // neighbourhood and stalls here are handled by the nOff bail-out only.
        // The stall map is a Phase 1 concept; Phase 2 is fine-grained enough
        // that false negatives from skipping would be more costly than the
        // measurements themselves.
        // ---------------------------------------------------------------------
        TripletResult bestP2 = bestP1;

        int32_t aStart   = (int32_t)bestP1.phaseDelayUs - (int32_t)kPhase2AlphaRadiusUs;
        int32_t aEnd     = (int32_t)bestP1.phaseDelayUs + (int32_t)kPhase2AlphaRadiusUs;
        int32_t onStart  = (int32_t)bestP1.nOn - kPhase2NonRadius;
        int32_t onEnd    = (int32_t)bestP1.nOn + kPhase2NonRadius;
        int32_t offStart = (int32_t)bestP1.nOff - kPhase2NoffRadius;
        int32_t offEnd   = (int32_t)bestP1.nOff + kPhase2NoffRadius;

        for ( int32_t aRaw = aStart; aRaw <= aEnd; aRaw += (int32_t)kPhase2AlphaStepUs ) {
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

                    TripletResult res = MeasureTriplet( alpha, nOn, nOff, targetRPM, kRepeatCount );

                    // Stall detected: bail out of the nOff loop. Increasing
                    // nOff further reduces duty and can only make this worse.
                    if ( res.measuredRPM == 0 && res.stress == kAcFanStressFactorMax ) {
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
        bool feasible = ( bestP2.stress < kAcFanStressFactorMax );

        mapOut->slots[ step ].isFeasible            = feasible;
        mapOut->slots[ step ].actualRPM             = bestP2.measuredRPM;
        mapOut->slots[ step ].strategy.phaseDelayUs = bestP2.phaseDelayUs;
        mapOut->slots[ step ].strategy.burstOn      = bestP2.nOn;
        mapOut->slots[ step ].strategy.burstWindow  = bestP2.nOn + bestP2.nOff;

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

/// @brief Drive the fan at @p requestedPm permille of motorMaxRPM using a calibrated map.
///
/// The profile map divides the speed range into kAcFanNumSteps equal bands.
/// Requests exactly on a band boundary are applied directly. Requests between
/// two boundaries are linearly interpolated across all three drive parameters
/// (phaseDelayUs, burstOn, burstWindow).
///
/// Special case: if one neighbouring slot has `burstOn=0` (motor off) and the
/// other does not, interpolation would produce meaningless fractional values.
/// The function snaps to whichever slot is nearer instead.
///
/// Guard: after interpolation, `burstOn` is clamped to `burstWindow`. Independent
/// interpolation of both fields can produce a rounding inconsistency where
/// `burstOn > burstWindow`, which the TRIAC driver would reject.
void ACFanDrive( const ACFanProfileMapPtr map, Permille requestedPm ) {
    if ( !map || !s_triac ) {
        return;
    }

    if ( requestedPm > 1000 ) requestedPm = 1000;

    // Zero — apply slot 0 (motor off).
    if ( requestedPm == 0 ) {
        TriacDriveParams p;
        p.phaseDelayUs = map->slots[ 0 ].strategy.phaseDelayUs;
        p.burstOn      = map->slots[ 0 ].strategy.burstOn;
        p.burstWindow  = map->slots[ 0 ].strategy.burstWindow;
        TriacRun( s_triac, p );
        return;
    }

    uint8_t lowerIdx = (uint8_t)( requestedPm / kAcFanStepIncrementPm );
    uint8_t upperIdx = lowerIdx + 1;

    if ( lowerIdx > kAcFanNumSteps ) lowerIdx = kAcFanNumSteps;
    if ( upperIdx > kAcFanNumSteps ) upperIdx = kAcFanNumSteps;

    // Exact boundary — no interpolation needed.
    if ( lowerIdx == upperIdx || ( requestedPm % kAcFanStepIncrementPm ) == 0 ) {
        TriacDriveParams p;
        p.phaseDelayUs = map->slots[ lowerIdx ].strategy.phaseDelayUs;
        p.burstOn      = map->slots[ lowerIdx ].strategy.burstOn;
        p.burstWindow  = map->slots[ lowerIdx ].strategy.burstWindow;
        TriacRun( s_triac, p );
        return;
    }

    const ACFanDriveParamsPtr low  = &map->slots[ lowerIdx ].strategy;
    const ACFanDriveParamsPtr high = &map->slots[ upperIdx ].strategy;

    // Fractional position within the band in permille (0–1000).
    // e.g. requestedPm=150 with step=100 → lower=1, fraction=500.
    Permille fraction = (Permille)( ( requestedPm % kAcFanStepIncrementPm ) * 10U );

    // Snap rather than interpolate across the on/off boundary.
    if ( ( low->burstOn == 0 ) != ( high->burstOn == 0 ) ) {
        const ACFanDriveParamsPtr chosen = ( fraction < 500 ) ? low : high;
        TriacDriveParams          p;
        p.phaseDelayUs = chosen->phaseDelayUs;
        p.burstOn      = chosen->burstOn;
        p.burstWindow  = chosen->burstWindow;
        TriacRun( s_triac, p );
        return;
    }

    // Standard linear interpolation.
    TriacDriveParams p;
    p.phaseDelayUs = (uint16_t)InterpLinear( (int32_t)low->phaseDelayUs, (int32_t)high->phaseDelayUs, fraction );
    p.burstOn      = (uint8_t)InterpLinear( (int32_t)low->burstOn, (int32_t)high->burstOn, fraction );
    p.burstWindow  = (uint8_t)InterpLinear( (int32_t)low->burstWindow, (int32_t)high->burstWindow, fraction );

    // Guard against burstOn > burstWindow due to independent rounding.
    if ( p.burstOn > p.burstWindow ) {
        p.burstOn = p.burstWindow;
    }

    TriacRun( s_triac, p );
}

#endif // FEATURE_AC_FAN_CALIBRATION
