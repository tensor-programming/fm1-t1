/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Torso T-1 Architecture - Phase 2: Core Algorithmic Subsystems Implementation */

#include "torso_t1_algo.h"

#if !defined(FELUCCA_ID) && !defined(FM1_SYS_H)
#include <string.h>
#else
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
#endif

/* -------------------------------------------------------------------------
 * 1. EUCLIDEAN ENGINE (Bjorklund's Distribution Algorithm)
 * ------------------------------------------------------------------------- */

typedef struct {
    uint64_t pattern;
    uint8_t  len;
} BjorkGroup;

uint64_t t1_euclidean_bjorklund(uint8_t steps, uint8_t pulses)
{
    if (steps == 0) return 0;
    if (steps > 64) steps = 64;
    if (pulses == 0) return 0;
    if (pulses >= steps) {
        return (steps == 64) ? ~0ULL : ((1ULL << steps) - 1ULL);
    }

    BjorkGroup groups[64];
    uint8_t count_ones  = pulses;
    uint8_t count_zeros = (uint8_t)(steps - pulses);

    /* Initialize pulses as '1' (len 1) and rests as '0' (len 1) */
    for (uint8_t i = 0; i < count_ones; i++) {
        groups[i].pattern = 1ULL;
        groups[i].len     = 1;
    }
    for (uint8_t i = 0; i < count_zeros; i++) {
        groups[count_ones + i].pattern = 0ULL;
        groups[count_ones + i].len     = 1;
    }

    /* Iterative Bjorklund pairing */
    while (count_zeros > 1) {
        uint8_t min_c = (count_ones < count_zeros) ? count_ones : count_zeros;

        /* Append zero-groups to one-groups */
        for (uint8_t i = 0; i < min_c; i++) {
            groups[i].pattern = (groups[i].pattern << groups[count_ones + i].len) |
                                 groups[count_ones + i].pattern;
            groups[i].len    += groups[count_ones + i].len;
        }

        if (count_ones >= count_zeros) {
            /* Remaining one-groups become the new remainder (zeros group) */
            uint8_t remainder = count_ones - count_zeros;
            for (uint8_t i = 0; i < remainder; i++) {
                groups[min_c + i] = groups[count_zeros + i];
            }
            count_ones  = min_c;
            count_zeros = remainder;
        } else {
            /* Remaining zero-groups shift down */
            uint8_t remainder = count_zeros - count_ones;
            for (uint8_t i = 0; i < remainder; i++) {
                groups[min_c + i] = groups[count_ones + min_c + i];
            }
            count_zeros = remainder;
        }
    }

    /* Concatenate all remaining groups */
    uint8_t total_groups = count_ones + count_zeros;
    uint64_t result_mask = 0ULL;
    uint8_t current_bit = 0;

    for (uint8_t i = 0; i < total_groups; i++) {
        uint64_t pat = groups[i].pattern;
        uint8_t  len = groups[i].len;

        for (int b = len - 1; b >= 0; b--) {
            if ((pat >> b) & 1ULL) {
                result_mask |= (1ULL << current_bit);
            }
            current_bit++;
            if (current_bit >= steps) break;
        }
        if (current_bit >= steps) break;
    }

    return result_mask;
}

uint64_t t1_euclidean_rotate(uint64_t mask, uint8_t steps, int8_t rotate)
{
    if (steps == 0) return 0;
    if (steps > 64) steps = 64;

    int8_t r = (int8_t)(((rotate % (int8_t)steps) + (int8_t)steps) % (int8_t)steps);
    if (r == 0) return mask;

    uint64_t step_mask = (steps == 64) ? ~0ULL : ((1ULL << steps) - 1ULL);
    mask &= step_mask;

    if (steps == 64) {
        return (mask >> r) | (mask << (64 - r));
    }
    return ((mask >> r) | (mask << (steps - r))) & step_mask;
}

uint64_t t1_euclidean_eval_cycle(const CycleParams *cp)
{
    if (!cp) return 0;
    uint8_t steps  = cp->steps  ? cp->steps  : 16;
    uint8_t pulses = cp->pulses;

    uint64_t bjorklund_mask = t1_euclidean_bjorklund(steps, pulses);
    uint64_t rotated_mask   = t1_euclidean_rotate(bjorklund_mask, steps, cp->rotate);

    /* Apply manual pulse overrides (active_mask_lo for steps 1-32, active_mask_hi for 33-64) */
    uint64_t manual_mask = ((uint64_t)cp->active_mask_hi << 32) | (uint64_t)cp->active_mask_lo;
    uint64_t step_mask   = (steps == 64) ? ~0ULL : ((1ULL << steps) - 1ULL);

    return (rotated_mask | manual_mask) & step_mask;
}

/* -------------------------------------------------------------------------
 * 2. PACE & RATCHET ACCUMULATOR
 * ------------------------------------------------------------------------- */

/*
 * Compute non-linear pace curve in Q15 (0 to 32768).
 * pace < 0: accelerating (intervals start large and shrink -> bouncing ball)
 * pace == 0: linear
 * pace > 0: decelerating (intervals start small and expand)
 */
uint16_t t1_pace_curve_q15(uint16_t x_q15, int8_t pace)
{
    if (pace == 0 || x_q15 == 0 || x_q15 >= 32768) {
        return x_q15;
    }

    /*
     * Quadratic bell factor: b(x) = x * (1 - x), in Q15.
     * Peak is 8192 (0.25) at x = 16384 (0.5).
     */
    uint32_t inv_x = 32768u - (uint32_t)x_q15;
    uint32_t bell  = ((uint32_t)x_q15 * inv_x) >> 15; /* 0..8192 */

    /*
     * Warp delta = -pace * bell / 64
     * If pace < 0: y > x (accelerating, early intervals large, later intervals short -> bouncing ball)
     * If pace > 0: y < x (decelerating, early intervals short, later intervals large)
     */
    int32_t delta = -(((int32_t)pace * (int32_t)bell) / 64);
    int32_t y = (int32_t)x_q15 + delta;

    if (y < 0)     y = 0;
    if (y > 32768) y = 32768;
    return (uint16_t)y;
}

void t1_pace_eval_ratchet(const CycleParams *cp, uint32_t pulse_duration_ticks, T1RatchetPattern *out)
{
    if (!cp || !out) return;

    uint8_t reps = cp->repeats;
    if (reps < 1)  reps = 1;
    if (reps > 16) reps = 16;
    out->repeats = reps;

    uint8_t time_gate = cp->time ? cp->time : 64; /* 1..127 */
    int8_t  pace      = cp->pace;
    int8_t  offset    = cp->offset;
    uint8_t base_vel  = cp->velocity ? cp->velocity : 100;

    if (reps == 1) {
        out->tick_offsets[0] = 0;
        out->sub_ticks[0]    = 0;
        out->gate_ticks[0]   = (pulse_duration_ticks * (uint32_t)time_gate) / 128u;
        if (out->gate_ticks[0] == 0) out->gate_ticks[0] = 1;
        out->velocities[0]   = base_vel;
        return;
    }

    for (uint8_t k = 0; k < reps; k++) {
        /* Normalized start and end points of this repeat in Q15 */
        uint16_t x0_q15 = (uint16_t)(((uint32_t)k * 32768u) / reps);
        uint16_t x1_q15 = (uint16_t)(((uint32_t)(k + 1u) * 32768u) / reps);

        uint16_t y0_q15 = t1_pace_curve_q15(x0_q15, pace);
        uint16_t y1_q15 = t1_pace_curve_q15(x1_q15, pace);

        uint32_t start_tick = ((uint64_t)y0_q15 * pulse_duration_ticks) / 32768u;
        uint32_t end_tick   = ((uint64_t)y1_q15 * pulse_duration_ticks) / 32768u;
        uint32_t interval   = (end_tick > start_tick) ? (end_tick - start_tick) : 1u;

        out->tick_offsets[k] = start_tick;
        out->sub_ticks[k]    = (uint16_t)((((uint64_t)y0_q15 * pulse_duration_ticks) % 32768u) >> 1);

        uint32_t gate = (interval * (uint32_t)time_gate) / 128u;
        out->gate_ticks[k] = (gate > 0) ? gate : 1u;

        /* Velocity slope offset across repeats */
        int32_t vel = (int32_t)base_vel + ((int32_t)k * (int32_t)offset) / (int32_t)(reps - 1);
        if (vel < 1)   vel = 1;
        if (vel > 127) vel = 127;
        out->velocities[k] = (uint8_t)vel;
    }
}

/* -------------------------------------------------------------------------
 * 3. HARMONY & VOICING ENGINE
 * ------------------------------------------------------------------------- */

const uint16_t T1_SCALE_MASKS[T1_NUM_SCALES] = {
    0x0FFFu, /* 0: Chromatic (12 notes) */
    0x0AD5u, /* 1: Major (Ionian) [1,0,1,0,1,1,0,1,0,1,0,1] */
    0x05ADu, /* 2: Natural Minor (Aeolian) [1,0,1,1,0,1,0,1,1,0,1,0] */
    0x06ADu, /* 3: Dorian [1,0,1,1,0,1,0,1,0,1,1,0] */
    0x06D5u, /* 4: Mixolydian [1,0,1,0,1,1,0,1,0,1,1,0] */
    0x0295u, /* 5: Major Pentatonic [1,0,1,0,1,0,0,1,0,1,0,0] */
    0x04A9u, /* 6: Minor Pentatonic [1,0,0,1,0,1,0,1,0,0,1,0] */
    0x09ADu, /* 7: Harmonic Minor [1,0,1,1,0,1,0,1,1,0,0,1] */
    0x056Du, /* 8: Phrygian [1,1,0,1,0,1,0,1,1,0,1,0] */
    0x0AF5u, /* 9: Lydian [1,0,1,0,1,0,1,1,0,1,0,1] */
    0x056Bu, /* 10: Locrian [1,1,0,1,0,1,1,0,1,0,1,0] */
    0x0AD7u, /* 11: Melodic Minor [1,0,1,1,0,1,0,1,0,1,0,1] */
    0x04E9u, /* 12: Blues [1,0,0,1,0,1,1,1,0,0,1,0] */
    0x0555u, /* 13: Whole Tone [1,0,1,0,1,0,1,0,1,0,1,0] */
    0x06DBu, /* 14: Diminished Half-Whole [1,1,0,1,1,0,1,1,0,1,1,0] */
    0x0B6Du, /* 15: Diminished Whole-Half [1,0,1,1,0,1,1,0,1,1,0,1] */
};

const char * const T1_SCALE_NAMES[T1_NUM_SCALES] = {
    "Chromatic", "Major", "Minor", "Dorian", "Mixolydian",
    "Maj Pent", "Min Pent", "Harm Minor", "Phrygian", "Lydian",
    "Locrian", "Mel Minor", "Blues", "Whole Tone", "Dim H-W", "Dim W-H"
};

uint8_t t1_harmony_quantize(int16_t raw_note, uint8_t root_note, uint8_t scale_id)
{
    if (scale_id >= T1_NUM_SCALES) scale_id = 0;
    root_note %= 12u;

    if (raw_note < 0)   raw_note = 0;
    if (raw_note > 127) raw_note = 127;

    uint16_t mask = T1_SCALE_MASKS[scale_id];
    if (mask == 0x0FFFu) {
        return (uint8_t)raw_note; /* Chromatic: no snap needed */
    }

    /* Snap downward to the nearest in-scale pitch */
    for (int guard = 0; guard < 12; guard++) {
        uint8_t pitch_class = (uint8_t)(((raw_note - root_note) % 12 + 12) % 12);
        if ((mask >> pitch_class) & 1u) {
            return (uint8_t)raw_note;
        }
        if (raw_note > 0) {
            raw_note--;
        } else {
            break;
        }
    }

    return (uint8_t)raw_note;
}

uint8_t t1_harmony_eval_voicing(uint8_t base_note, uint8_t voicing, uint8_t voice_idx, uint8_t chord_size)
{
    int16_t n = base_note;
    if (chord_size == 0) chord_size = 1;

    switch (voicing) {
        case 1: /* Inversion 1: move lowest note up an octave */
            if (voice_idx == 0 && chord_size > 1) n += 12;
            break;
        case 2: /* Inversion 2: move two lowest notes up an octave */
            if (voice_idx < 2 && chord_size > 2) n += 12;
            break;
        case 3: /* Inversion 3: move three lowest notes up an octave */
            if (voice_idx < 3 && chord_size > 3) n += 12;
            break;
        case 4: /* Drop-2: lower second note from top by an octave */
            if (voice_idx == (chord_size > 1 ? chord_size - 2 : 0)) n -= 12;
            break;
        case 5: /* Drop-3: lower third note from top by an octave */
            if (chord_size >= 3 && voice_idx == chord_size - 3) n -= 12;
            break;
        case 6: /* Spread: alternate octaves */
            if (voice_idx % 2 == 1) n += 12;
            break;
        case 7: /* Octave Up */
            n += 12;
            break;
        case 8: /* Octave Down */
            n -= 12;
            break;
        case 9: /* Spread Wide: alternating up and down */
            if (voice_idx == 0) n -= 12;
            else if (voice_idx == chord_size - 1) n += 12;
            break;
        case 10: /* 2 Octaves Up */
            n += 24;
            break;
        case 11: /* 2 Octaves Down */
            n -= 24;
            break;
        default:
            break;
    }

    if (n < 0)   n = 0;
    if (n > 127) n = 127;
    return (uint8_t)n;
}

int16_t t1_harmony_eval_phrase(uint8_t phrase_shape, uint8_t range, uint8_t step_idx, uint8_t total_steps)
{
    if (range == 0 || total_steps <= 1) return 0;
    step_idx %= total_steps;

    int32_t max_semitones = (int32_t)range * 7; /* Excursion span scaled by range */
    int32_t delta = 0;

    switch (phrase_shape) {
        case 1: { /* Sine wave contour approximation */
            /* Quarter-cycle lookup */
            int32_t phase = ((int32_t)step_idx * 360) / total_steps;
            /* Simple parabolic sine approximation */
            if (phase < 180) {
                delta = (phase * (180 - phase) * max_semitones) / 8100;
            } else {
                int32_t p = phase - 180;
                delta = -(p * (180 - p) * max_semitones) / 8100;
            }
            break;
        }
        case 2: { /* Triangle wave contour */
            int32_t half = total_steps / 2;
            if (half == 0) half = 1;
            if (step_idx <= half) {
                delta = ((int32_t)step_idx * max_semitones) / half;
            } else {
                delta = (((int32_t)total_steps - step_idx) * max_semitones) / half;
            }
            break;
        }
        case 3: /* Ramp Up (Saw) */
            delta = ((int32_t)step_idx * max_semitones) / (total_steps - 1);
            break;
        case 4: /* Ramp Down (Saw) */
            delta = (((int32_t)total_steps - 1 - step_idx) * max_semitones) / (total_steps - 1);
            break;
        case 5: { /* Exponential Arch (peaking at midpoint) */
            int32_t norm = (2 * step_idx - total_steps);
            int32_t quad = (norm * norm) / (total_steps * total_steps + 1);
            delta = max_semitones - (quad * max_semitones);
            break;
        }
        case 6: { /* Inverted Arch (dip in middle) */
            int32_t norm = (2 * step_idx - total_steps);
            int32_t quad = (norm * norm) / (total_steps * total_steps + 1);
            delta = (quad * max_semitones);
            break;
        }
        case 7: { /* Deterministic pseudo-random S&H */
            uint32_t hash = ((uint32_t)step_idx * 1103515245u + 12345u) & 0x7FFFFFFFu;
            delta = (int32_t)(hash % (uint32_t)(2 * max_semitones + 1)) - max_semitones;
            break;
        }
        default:
            delta = 0;
            break;
    }

    return (int16_t)delta;
}

uint8_t t1_harmony_eval_style(uint8_t style, uint32_t step_counter, uint8_t pool_size)
{
    if (pool_size <= 1) return 0;

    switch (style) {
        case 0: /* Up */
            return (uint8_t)(step_counter % pool_size);
        case 1: /* Down */
            return (uint8_t)((pool_size - 1u) - (step_counter % pool_size));
        case 2: { /* Up-Down */
            uint32_t cycle = 2u * pool_size - 2u;
            uint32_t pos = step_counter % cycle;
            return (uint8_t)((pos < pool_size) ? pos : (cycle - pos));
        }
        case 3: { /* Down-Up */
            uint32_t cycle = 2u * pool_size - 2u;
            uint32_t pos = step_counter % cycle;
            return (uint8_t)((pos < pool_size) ? ((pool_size - 1u) - pos) : (pos - pool_size + 1u));
        }
        case 4: { /* Converge (outside-in) */
            uint32_t pos = step_counter % pool_size;
            if (pos % 2 == 0) {
                return (uint8_t)(pos / 2);
            } else {
                return (uint8_t)(pool_size - 1u - (pos / 2));
            }
        }
        case 5: { /* Diverge (inside-out) */
            uint32_t pos = step_counter % pool_size;
            uint8_t mid = pool_size / 2;
            if (pos % 2 == 0) {
                return (uint8_t)(mid + pos / 2);
            } else {
                return (uint8_t)(mid - 1u - pos / 2);
            }
        }
        case 6: { /* Random */
            uint32_t hash = (step_counter * 1664525u + 1013904223u);
            return (uint8_t)(hash % pool_size);
        }
        default:
            return (uint8_t)(step_counter % pool_size);
    }
}

void t1_harmony_eval_chord(uint8_t root_note, uint8_t scale_id, uint8_t base_note,
                           uint8_t chord_size, uint8_t voicing, uint8_t *out_notes)
{
    if (!out_notes) return;
    if (chord_size == 0) chord_size = 1;
    if (chord_size > 4) chord_size = 4;

    uint8_t raw[4];
    raw[0] = base_note;

    if (chord_size > 1) {
        if (scale_id >= T1_NUM_SCALES) scale_id = 0;
        uint16_t mask = T1_SCALE_MASKS[scale_id];

        for (uint8_t v = 1; v < chord_size; v++) {
            uint8_t curr = raw[v - 1] + 1u;
            uint8_t steps_found = 0;
            while (steps_found < 2u && curr <= 127u) {
                uint8_t pc = (uint8_t)(((curr % 12u) + 12u - (root_note % 12u)) % 12u);
                if (mask & (1u << pc)) {
                    steps_found++;
                    if (steps_found == 2u) {
                        break;
                    }
                }
                curr++;
            }
            raw[v] = (curr <= 127u) ? curr : 127u;
        }
    }

    for (uint8_t i = 0; i < chord_size; i++) {
        out_notes[i] = t1_harmony_eval_voicing(raw[i], voicing, i, chord_size);
    }
}

/* -------------------------------------------------------------------------
 * 4. DETERMINISTIC GALOIS LFSR RANDOM ENGINE
 * ------------------------------------------------------------------------- */

void t1_lfsr_init(T1LFSRState *lfsr, uint16_t seed, uint8_t random_rate)
{
    if (!lfsr) return;
    if (seed == 0) seed = 0x192Eu; /* Standard non-zero seed */

    lfsr->state         = seed;
    lfsr->cycle_seed    = seed;
    lfsr->cycle_counter = 0;

    /*
     * Configurable cycle loop periods (1, 2, 3, 4, 6 cycles)
     * Maps random_rate values 0..4 directly to loop lengths.
     */
    switch (random_rate) {
        case 0: lfsr->loop_period = 1; break;
        case 1: lfsr->loop_period = 2; break;
        case 2: lfsr->loop_period = 3; break;
        case 3: lfsr->loop_period = 4; break;
        case 4: lfsr->loop_period = 6; break;
        default:
            lfsr->loop_period = (uint8_t)((random_rate % 6) + 1);
            break;
    }
}

uint16_t t1_lfsr_next(T1LFSRState *lfsr)
{
    if (!lfsr) return 0;
    /*
     * 16-bit Galois LFSR with polynomial x^16 + x^14 + x^13 + x^11 + 1
     * Tap mask = 0xB400u
     */
    uint16_t lsb = lfsr->state & 1u;
    lfsr->state >>= 1;
    if (lsb) {
        lfsr->state ^= 0xB400u;
    }
    return lfsr->state;
}

void t1_lfsr_on_cycle_boundary(T1LFSRState *lfsr)
{
    if (!lfsr) return;
    lfsr->cycle_counter++;
    if (lfsr->cycle_counter >= lfsr->loop_period) {
        lfsr->cycle_counter = 0;
        /* Restore seed so the random sequence repeats deterministically */
        lfsr->state = lfsr->cycle_seed;
    }
}

int32_t t1_lfsr_modulate(T1LFSRState *lfsr, uint16_t random_targets, uint16_t target_flag,
                         int32_t base_val, uint8_t depth, int32_t min_val, int32_t max_val)
{
    if (!lfsr || depth == 0 || !(random_targets & target_flag)) {
        return base_val;
    }

    uint16_t rnd = t1_lfsr_next(lfsr);

    /* Bipolar modulation [-depth/2, +depth/2] */
    int32_t delta = (int32_t)(rnd % (uint32_t)(depth + 1u)) - (int32_t)(depth / 2u);
    int32_t result = base_val + delta;

    if (result < min_val) result = min_val;
    if (result > max_val) result = max_val;
    return result;
}

/* -------------------------------------------------------------------------
 * 5. PARAMETER LOCK (P-LOCK) ARENA
 * ------------------------------------------------------------------------- */

bool t1_plock_set(CycleParams *cp, uint8_t step_idx, uint8_t param_id, int16_t value)
{
    if (!cp || step_idx > 63) return false;
    T1ParamLockEntry *entries = (T1ParamLockEntry *)cp->p_lock_data;

    int8_t first_free = -1;

    /* 1. Check if a lock already exists for this step and param */
    for (uint8_t i = 0; i < T1_MAX_PLOCKS_PER_CYCLE; i++) {
        if (entries[i].step_idx == step_idx && entries[i].param_id == param_id) {
            entries[i].value = value;
            return true;
        }
        if (first_free == -1 && entries[i].step_idx == 0xFF) {
            first_free = (int8_t)i;
        }
    }

    /* 2. Allocate in first free slot */
    if (first_free >= 0) {
        entries[first_free].step_idx = step_idx;
        entries[first_free].param_id = param_id;
        entries[first_free].value    = value;
        entries[first_free].flags    = 0;
        return true;
    }

    return false; /* Arena full */
}

bool t1_plock_get(const CycleParams *cp, uint8_t step_idx, uint8_t param_id, int16_t *out_val)
{
    if (!cp || step_idx > 63) return false;
    const T1ParamLockEntry *entries = (const T1ParamLockEntry *)cp->p_lock_data;

    for (uint8_t i = 0; i < T1_MAX_PLOCKS_PER_CYCLE; i++) {
        if (entries[i].step_idx == step_idx && entries[i].param_id == param_id) {
            if (out_val) *out_val = entries[i].value;
            return true;
        }
    }

    return false;
}

bool t1_plock_clear(CycleParams *cp, uint8_t step_idx, uint8_t param_id)
{
    if (!cp || step_idx > 63) return false;
    T1ParamLockEntry *entries = (T1ParamLockEntry *)cp->p_lock_data;

    for (uint8_t i = 0; i < T1_MAX_PLOCKS_PER_CYCLE; i++) {
        if (entries[i].step_idx == step_idx && entries[i].param_id == param_id) {
            entries[i].step_idx = 0xFF;
            return true;
        }
    }

    return false;
}

void t1_plock_clear_all(CycleParams *cp)
{
    if (!cp) return;
    T1ParamLockEntry *entries = (T1ParamLockEntry *)cp->p_lock_data;
    for (uint8_t i = 0; i < T1_MAX_PLOCKS_PER_CYCLE; i++) {
        entries[i].step_idx = 0xFF;
    }
}
