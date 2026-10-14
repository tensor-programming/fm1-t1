/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Torso T-1 Architecture - Phase 2: Core Algorithmic Subsystems Header */

#ifndef TORSO_T1_ALGO_H
#define TORSO_T1_ALGO_H

#include "torso_t1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------- 1. Euclidean Engine (Bjorklund Distribution) --- */

/**
 * Generate a 64-bit step trigger bitmask using Bjorklund's Euclidean distribution algorithm.
 * @param steps  Number of sequence steps (1 to 64)
 * @param pulses Number of active pulses to distribute (0 to steps)
 * @return 64-bit integer where bit k represents whether step k triggers (1) or rests (0).
 */
uint64_t t1_euclidean_bjorklund(uint8_t steps, uint8_t pulses);

/**
 * Apply circular shift rotation to a step bitmask.
 * @param mask   Input 64-bit trigger mask
 * @param steps  Total length of the loop (1 to 64)
 * @param rotate Rotation offset (-64 to +64)
 * @return Rotated 64-bit bitmask within [0, steps - 1]
 */
uint64_t t1_euclidean_rotate(uint64_t mask, uint8_t steps, int8_t rotate);

/**
 * Compute the complete 64-bit trigger mask for a Cycle, combining:
 * 1. Bjorklund distribution E(pulses, steps)
 * 2. Circular rotation shift
 * 3. Manual pulse overrides (active_mask_lo and active_mask_hi)
 */
uint64_t t1_euclidean_eval_cycle(const CycleParams *cp);

/* ----------------- 2. Pace & Ratchet Accumulator --- */

#define T1_MAX_RATCHET_REPEATS 16

typedef struct {
    uint8_t  repeats;                           /* Number of active repeats (1 to 16) */
    uint32_t tick_offsets[T1_MAX_RATCHET_REPEATS]; /* Tick offset from pulse start */
    uint16_t sub_ticks[T1_MAX_RATCHET_REPEATS];    /* Fractional sub-tick timing */
    uint32_t gate_ticks[T1_MAX_RATCHET_REPEATS];   /* Gate duration per repeat */
    uint8_t  velocities[T1_MAX_RATCHET_REPEATS];   /* Velocity after offset slope */
} T1RatchetPattern;

/**
 * Evaluate the non-linear ratchet repeats and velocity slope for a pulse.
 * @param cp                    Cycle parameter block (repeats, pace, time, offset, velocity)
 * @param pulse_duration_ticks  Length of the step in 192 PPQN ticks
 * @param out                   Output ratchet timing and velocity pattern
 */
void t1_pace_eval_ratchet(const CycleParams *cp, uint32_t pulse_duration_ticks, T1RatchetPattern *out);

/**
 * Compute non-linear pace curve warp factor in Q15 format (0 to 32768).
 * @param normalized_x_q15  Progress along repeat sequence in Q15 (0 to 32768)
 * @param pace              Log/Exp curve shape (-64 = accelerating, 0 = linear, +63 = decelerating)
 * @return Warped progress in Q15 (0 to 32768)
 */
uint16_t t1_pace_curve_q15(uint16_t normalized_x_q15, int8_t pace);

/* ----------------- 3. Harmony & Voicing Engine --- */

#define T1_NUM_SCALES 16

/* Scale Interval Definitions (bitmasks across 12 chromatic semitones) */
extern const uint16_t T1_SCALE_MASKS[T1_NUM_SCALES];
extern const char * const T1_SCALE_NAMES[T1_NUM_SCALES];

/**
 * Quantize a raw chromatic note to the selected musical scale and root.
 * @param raw_note  Input MIDI note (0 to 127)
 * @param root_note Root pitch 0 (C) to 11 (B)
 * @param scale_id  Scale type (0 to 15)
 * @return Quantized MIDI note (0 to 127) on a valid scale degree
 */
uint8_t t1_harmony_quantize(int16_t raw_note, uint8_t root_note, uint8_t scale_id);

/**
 * Evaluate voice inversion and octave displacement style.
 * @param base_note  Root quantized pitch
 * @param voicing    Voicing mode (0: root, 1-3: inversions, 4-5: drop-2/3, 6-7: octave jumps, etc.)
 * @param voice_idx  Index of voice in chord/polyphony (0 to 5)
 * @param chord_size Total voices sounding (1 to 6)
 * @return Transposed / displaced MIDI note (clamped 0 to 127)
 */
uint8_t t1_harmony_eval_voicing(uint8_t base_note, uint8_t voicing, uint8_t voice_idx, uint8_t chord_size);

/**
 * Compute melodic contour wave shape offset for a given step.
 * @param phrase_shape  Melodic contour shape (0: flat, 1: sine, 2: tri, 3: ramp up, 4: ramp dn, 5: arch, 6: u-arch, 7: s&h)
 * @param range         Melodic excursion span in octaves (0 to 7)
 * @param step_idx      Current step index (0 to steps - 1)
 * @param total_steps   Total steps in cycle
 * @return Signed semitone excursion offset
 */
int16_t t1_harmony_eval_phrase(uint8_t phrase_shape, uint8_t range, uint8_t step_idx, uint8_t total_steps);

/**
 * Arpeggiator note order evaluation based on Style parameter.
 * @param style         Style index (0: Up, 1: Down, 2: Up-Down, 3: Down-Up, 4: Converge, 5: Diverge, 6: Random, 7: Order)
 * @param step_counter  Progressive step counter
 * @param pool_size     Number of notes available
 * @return Index into the note pool
 */
uint8_t t1_harmony_eval_style(uint8_t style, uint32_t step_counter, uint8_t pool_size);

/**
 * Evaluate polyphonic chord notes (1 to 4 voices) based on diatonic scale intervals and voicing.
 * @param root_note Root note of the scale (0..11)
 * @param scale_id Scale definition index (0..15)
 * @param base_note Quantized root note of the chord
 * @param chord_size Number of notes in the chord (1..4)
 * @param voicing Voicing / inversion style (0..11)
 * @param out_notes Destination array for evaluated MIDI notes (must hold chord_size elements)
 */
void t1_harmony_eval_chord(uint8_t root_note, uint8_t scale_id, uint8_t base_note,
                           uint8_t chord_size, uint8_t voicing, uint8_t *out_notes);

/* ----------------- 4. Deterministic Galois LFSR Random Engine --- */

/* Modulation Target Bitmasks (random_targets field) */
#define T1_RND_TARGET_VELOCITY (1u << 0)
#define T1_RND_TARGET_PITCH    (1u << 1)
#define T1_RND_TARGET_SUSTAIN  (1u << 2)
#define T1_RND_TARGET_ACCENT   (1u << 3)
#define T1_RND_TARGET_DELAY    (1u << 4)
#define T1_RND_TARGET_REPEATS  (1u << 5)
#define T1_RND_TARGET_CC       (1u << 6)
#define T1_RND_TARGET_VOICING  (1u << 7)

typedef struct {
    uint16_t state;         /* Current 16-bit Galois LFSR state */
    uint16_t cycle_seed;    /* Seed saved at the beginning of a cycle loop */
    uint8_t  cycle_counter; /* Current cycle pass within the loop period */
    uint8_t  loop_period;   /* Configured loop period (1, 2, 3, 4, 6 cycles) */
} T1LFSRState;

/**
 * Initialize LFSR state with deterministic initial seed.
 */
void t1_lfsr_init(T1LFSRState *lfsr, uint16_t seed, uint8_t random_rate);

/**
 * Advance Galois LFSR by one step and return 16-bit pseudo-random value.
 */
uint16_t t1_lfsr_next(T1LFSRState *lfsr);

/**
 * Notify LFSR of a cycle boundary wrap to maintain deterministic loop period locking.
 */
void t1_lfsr_on_cycle_boundary(T1LFSRState *lfsr);

/**
 * Apply deterministic stochastic modulation to a parameter if tagged in random_targets.
 * @param lfsr           LFSR state pointer
 * @param random_targets Bitmask of active modulation targets
 * @param target_flag    Target bit to check (e.g. T1_RND_TARGET_VELOCITY)
 * @param base_val       Base parameter value
 * @param depth          Modulation depth (0 to 127)
 * @param min_val        Allowed minimum clamped output
 * @param max_val        Allowed maximum clamped output
 * @return Modulated and clamped parameter value
 */
int32_t t1_lfsr_modulate(T1LFSRState *lfsr, uint16_t random_targets, uint16_t target_flag,
                         int32_t base_val, uint8_t depth, int32_t min_val, int32_t max_val);

/* ----------------- 5. Parameter Lock (P-Lock) Arena --- */

#define T1_MAX_PLOCKS_PER_CYCLE 11

typedef struct __attribute__((packed)) {
    uint8_t step_idx;       /* Step index 0..63 (0xFF = unused) */
    uint8_t param_id;       /* Target parameter ID */
    int16_t value;          /* Override value */
    uint8_t flags;          /* Trigger/condition flags */
    uint8_t reserved[3];    /* Alignment padding to 8 bytes */
} T1ParamLockEntry;

_Static_assert(sizeof(T1ParamLockEntry) == 8, "T1ParamLockEntry must be exactly 8 bytes");
_Static_assert(T1_MAX_PLOCKS_PER_CYCLE * sizeof(T1ParamLockEntry) == 88,
               "11 entries * 8 bytes = 88 bytes p_lock_data arena");

/**
 * Set a parameter lock on a specific step.
 */
bool t1_plock_set(CycleParams *cp, uint8_t step_idx, uint8_t param_id, int16_t value);

/**
 * Query whether a parameter lock exists for a specific step and parameter.
 * @param out_val Receives the locked value if found
 * @return true if a lock exists on this step, false otherwise
 */
bool t1_plock_get(const CycleParams *cp, uint8_t step_idx, uint8_t param_id, int16_t *out_val);

/**
 * Clear a parameter lock on a step.
 */
bool t1_plock_clear(CycleParams *cp, uint8_t step_idx, uint8_t param_id);

/**
 * Clear all parameter locks in a Cycle.
 */
void t1_plock_clear_all(CycleParams *cp);

#ifdef __cplusplus
}
#endif

#endif /* TORSO_T1_ALGO_H */
