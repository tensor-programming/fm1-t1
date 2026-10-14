/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Test Suite: Torso T-1 Architecture - Phase 2 Verification */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "torso_t1.h"
#include "torso_t1_algo.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (!(cond)) { \
        printf("FAILED: %s (line %d): %s\n", __func__, __LINE__, msg); \
        exit(1); \
    } else { \
        g_tests_passed++; \
    } \
} while(0)

/* Helper: count set bits in a 64-bit integer */
static uint8_t popcount64(uint64_t v)
{
    uint8_t c = 0;
    while (v) {
        c += (uint8_t)(v & 1ULL);
        v >>= 1;
    }
    return c;
}

/* ---------------- Test 1: Bjorklund Euclidean Generation & Rotation --- */
static void test_euclidean_engine(void)
{
    printf("== [Test 1] Bjorklund Euclidean Rhythm Generation & Rotation\n");

    /* 1. Edge cases */
    TEST_ASSERT(t1_euclidean_bjorklund(16, 0) == 0ULL, "0 pulses gives 0 mask");
    TEST_ASSERT(t1_euclidean_bjorklund(16, 16) == 0xFFFFULL, "16 pulses in 16 steps gives all 1s");
    TEST_ASSERT(t1_euclidean_bjorklund(64, 64) == ~0ULL, "64 pulses in 64 steps gives full 64-bit mask");

    /* 2. Classical Ethnomusicological Rhythms */

    /* Tresillo E(3, 8): [1, 0, 0, 1, 0, 0, 1, 0] -> bits 0, 3, 6 set */
    uint64_t tresillo = t1_euclidean_bjorklund(8, 3);
    TEST_ASSERT(popcount64(tresillo) == 3, "Tresillo has exactly 3 pulses");
    TEST_ASSERT((tresillo & (1ULL << 0)) != 0, "Step 0 is a pulse");
    TEST_ASSERT((tresillo & (1ULL << 1)) == 0, "Step 1 is a rest");
    TEST_ASSERT((tresillo & (1ULL << 2)) == 0, "Step 2 is a rest");
    TEST_ASSERT((tresillo & (1ULL << 3)) != 0, "Step 3 is a pulse");
    TEST_ASSERT((tresillo & (1ULL << 4)) == 0, "Step 4 is a rest");
    TEST_ASSERT((tresillo & (1ULL << 5)) == 0, "Step 5 is a rest");
    TEST_ASSERT((tresillo & (1ULL << 6)) != 0, "Step 6 is a pulse");
    TEST_ASSERT((tresillo & (1ULL << 7)) == 0, "Step 7 is a rest");
    /* Mask: 0b01001001 = 0x49 */
    TEST_ASSERT(tresillo == 0x49ULL, "Tresillo bitmask matches 0x49");

    /* Cinquillo E(5, 8): 5 pulses in 8 steps */
    uint64_t cinquillo = t1_euclidean_bjorklund(8, 5);
    TEST_ASSERT(popcount64(cinquillo) == 5, "Cinquillo has 5 pulses");

    /* Four-on-the-floor E(4, 16): hits on 0, 4, 8, 12 */
    uint64_t house = t1_euclidean_bjorklund(16, 4);
    TEST_ASSERT(popcount64(house) == 4, "Four-on-the-floor has 4 pulses");
    TEST_ASSERT(house == ((1ULL << 0) | (1ULL << 4) | (1ULL << 8) | (1ULL << 12)), "House beat hits every 4 steps");

    /* 3. Circular Shift Rotation (PLAN.md: (mask >> r) | (mask << (steps - r))) */
    /* Rotate Tresillo (0, 3, 6) with r = 1 -> hits at 2, 5, 7 */
    uint64_t rot_1 = t1_euclidean_rotate(tresillo, 8, 1);
    TEST_ASSERT(popcount64(rot_1) == 3, "Rotated mask preserves pulse count");
    TEST_ASSERT((rot_1 & (1ULL << 2)) != 0, "Rotated step 2 is hit");
    TEST_ASSERT((rot_1 & (1ULL << 5)) != 0, "Rotated step 5 is hit");
    TEST_ASSERT((rot_1 & (1ULL << 7)) != 0, "Rotated step 7 is hit");

    /* Rotate by negative offset (-1) -> hits at 1, 4, 7 */
    uint64_t rot_neg1 = t1_euclidean_rotate(tresillo, 8, -1);
    TEST_ASSERT(popcount64(rot_neg1) == 3, "Negative rotated mask preserves pulse count");
    TEST_ASSERT((rot_neg1 & (1ULL << 1)) != 0, "Rotated step 1 is hit");
    TEST_ASSERT((rot_neg1 & (1ULL << 4)) != 0, "Rotated step 4 is hit");
    TEST_ASSERT((rot_neg1 & (1ULL << 7)) != 0, "Rotated step 7 is hit");

    /* 4. Full Cycle Evaluation with Manual Bitmask Overrides */
    CycleParams cp;
    memset(&cp, 0, sizeof(cp));
    cp.steps  = 16;
    cp.pulses = 4;
    cp.rotate = 0;
    cp.active_mask_lo = (1u << 2); /* Manually add step 2 */

    uint64_t eval_mask = t1_euclidean_eval_cycle(&cp);
    TEST_ASSERT(popcount64(eval_mask) == 5, "Original 4 pulses + 1 manual override = 5 pulses");
    TEST_ASSERT((eval_mask & (1ULL << 2)) != 0, "Step 2 is set by manual override");

    printf("   -> Bjorklund Euclidean rhythms, circular rotation, and manual masks verified!\n");
}

/* ---------------- Test 2: Pace & Ratchet Accumulator --- */
static void test_pace_and_ratchet_accumulator(void)
{
    printf("== [Test 2] Pace & Ratchet Accumulator (Non-Linear Q15 Curves)\n");

    /* 1. Linear Pace (pace == 0) */
    CycleParams cp_lin;
    memset(&cp_lin, 0, sizeof(cp_lin));
    cp_lin.repeats  = 4;
    cp_lin.pace     = 0;
    cp_lin.time     = 64; /* 50% gate */
    cp_lin.velocity = 100;
    cp_lin.offset   = 0;  /* flat velocity */

    uint32_t step_duration = 192; /* 192 PPQN ticks */
    T1RatchetPattern rat_lin;
    t1_pace_eval_ratchet(&cp_lin, step_duration, &rat_lin);

    TEST_ASSERT(rat_lin.repeats == 4, "4 repeats generated");
    TEST_ASSERT(rat_lin.tick_offsets[0] == 0, "Repeat 0 starts at tick 0");
    TEST_ASSERT(rat_lin.tick_offsets[1] == 48, "Repeat 1 starts at tick 48");
    TEST_ASSERT(rat_lin.tick_offsets[2] == 96, "Repeat 2 starts at tick 96");
    TEST_ASSERT(rat_lin.tick_offsets[3] == 144, "Repeat 3 starts at tick 144");

    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(rat_lin.velocities[i] == 100, "Linear velocity is flat 100");
    }

    /* 2. Accelerating Pace (pace < 0): Bouncing Ball */
    CycleParams cp_acc = cp_lin;
    cp_acc.pace = -50; /* Strong acceleration */
    T1RatchetPattern rat_acc;
    t1_pace_eval_ratchet(&cp_acc, step_duration, &rat_acc);

    TEST_ASSERT(rat_acc.tick_offsets[0] == 0, "Repeat 0 starts at 0");
    uint32_t int0_acc = rat_acc.tick_offsets[1] - rat_acc.tick_offsets[0];
    uint32_t int1_acc = rat_acc.tick_offsets[2] - rat_acc.tick_offsets[1];
    uint32_t int2_acc = rat_acc.tick_offsets[3] - rat_acc.tick_offsets[2];

    /* Accelerating intervals must decrease: int0 > int1 > int2 */
    TEST_ASSERT(int0_acc > int1_acc, "Accelerating: interval 0 > interval 1");
    TEST_ASSERT(int1_acc > int2_acc, "Accelerating: interval 1 > interval 2");

    /* 3. Decelerating Pace (pace > 0) */
    CycleParams cp_dec = cp_lin;
    cp_dec.pace = +50; /* Strong deceleration */
    T1RatchetPattern rat_dec;
    t1_pace_eval_ratchet(&cp_dec, step_duration, &rat_dec);

    uint32_t int0_dec = rat_dec.tick_offsets[1] - rat_dec.tick_offsets[0];
    uint32_t int1_dec = rat_dec.tick_offsets[2] - rat_dec.tick_offsets[1];
    uint32_t int2_dec = rat_dec.tick_offsets[3] - rat_dec.tick_offsets[2];

    /* Decelerating intervals must increase: int0 < int1 < int2 */
    TEST_ASSERT(int0_dec < int1_dec, "Decelerating: interval 0 < interval 1");
    TEST_ASSERT(int1_dec < int2_dec, "Decelerating: interval 1 < interval 2");

    /* 4. Velocity Slope Offset */
    CycleParams cp_vel = cp_lin;
    cp_vel.repeats  = 4;
    cp_vel.velocity = 60;
    cp_vel.offset   = +30; /* Velocity slope from 60 to 90 */
    T1RatchetPattern rat_vel;
    t1_pace_eval_ratchet(&cp_vel, step_duration, &rat_vel);

    TEST_ASSERT(rat_vel.velocities[0] == 60, "Start velocity is 60");
    TEST_ASSERT(rat_vel.velocities[1] == 70, "Repeat 1 velocity is 70");
    TEST_ASSERT(rat_vel.velocities[2] == 80, "Repeat 2 velocity is 80");
    TEST_ASSERT(rat_vel.velocities[3] == 90, "End velocity is 90");

    printf("   -> Non-linear ratchet pacing and velocity slopes verified!\n");
}

/* ---------------- Test 3: Harmony & Voicing Engine --- */
static void test_harmony_and_voicing_engine(void)
{
    printf("== [Test 3] Harmony & Voicing Engine (16 Scales, Inversions, Phrasing)\n");

    /* 1. All 16 Scales Defined */
    TEST_ASSERT(T1_NUM_SCALES == 16, "16 scales defined");
    TEST_ASSERT(T1_SCALE_MASKS[0] == 0x0FFF, "Scale 0 is Chromatic");
    TEST_ASSERT(T1_SCALE_MASKS[1] == 0x0AD5, "Scale 1 is Major");
    TEST_ASSERT(T1_SCALE_MASKS[2] == 0x05AD, "Scale 2 is Natural Minor");

    /* 2. Scale Quantization (C Major: C, D, E, F, G, A, B) */
    /* C4 = 60 (in scale) */
    TEST_ASSERT(t1_harmony_quantize(60, 0, 1) == 60, "C4 in C Major stays 60");
    /* C#4 = 61 -> snaps down to C4 = 60 */
    TEST_ASSERT(t1_harmony_quantize(61, 0, 1) == 60, "C#4 in C Major snaps down to C4 (60)");
    /* D4 = 62 (in scale) */
    TEST_ASSERT(t1_harmony_quantize(62, 0, 1) == 62, "D4 in C Major stays 62");
    /* D#4 = 63 -> snaps down to D4 = 62 */
    TEST_ASSERT(t1_harmony_quantize(63, 0, 1) == 62, "D#4 in C Major snaps down to D4 (62)");

    /* Transposed Root: D Major (Root = 2) */
    /* C#4 = 61 is in D Major! */
    TEST_ASSERT(t1_harmony_quantize(61, 2, 1) == 61, "C#4 in D Major is in scale");

    /* 3. Voicing: Inversions and Octave Displacements */
    /* Inversion 1 on lowest voice: shifts +12 semitones */
    uint8_t note_v0 = t1_harmony_eval_voicing(60, 1, 0, 3);
    uint8_t note_v1 = t1_harmony_eval_voicing(64, 1, 1, 3);
    TEST_ASSERT(note_v0 == 72, "Inversion 1 raises lowest note from 60 to 72");
    TEST_ASSERT(note_v1 == 64, "Inversion 1 keeps higher notes unchanged");

    /* Octave Down (voicing 8) */
    TEST_ASSERT(t1_harmony_eval_voicing(60, 8, 0, 1) == 48, "Octave down shifts -12 semitones");

    /* 4. Melodic Phrase Contours */
    /* Flat phrase (0) -> always 0 */
    TEST_ASSERT(t1_harmony_eval_phrase(0, 2, 5, 16) == 0, "Flat phrase offset is 0");

    /* Triangle phrase (2): peaks at midpoint */
    int16_t p_start = t1_harmony_eval_phrase(2, 2, 0, 16);
    int16_t p_mid   = t1_harmony_eval_phrase(2, 2, 8, 16);
    int16_t p_end   = t1_harmony_eval_phrase(2, 2, 15, 16);
    TEST_ASSERT(p_start == 0, "Triangle starts at 0");
    TEST_ASSERT(p_mid > p_start, "Triangle peaks in middle");
    TEST_ASSERT(p_mid > p_end, "Triangle falls back down");

    /* 5. Style Directions */
    /* Up style (0) */
    TEST_ASSERT(t1_harmony_eval_style(0, 0, 4) == 0, "Up: step 0 -> note 0");
    TEST_ASSERT(t1_harmony_eval_style(0, 1, 4) == 1, "Up: step 1 -> note 1");
    TEST_ASSERT(t1_harmony_eval_style(0, 4, 4) == 0, "Up: step 4 wraps to 0");

    /* Down style (1) */
    TEST_ASSERT(t1_harmony_eval_style(1, 0, 4) == 3, "Down: step 0 -> note 3");
    TEST_ASSERT(t1_harmony_eval_style(1, 1, 4) == 2, "Down: step 1 -> note 2");

    printf("   -> Harmony quantization, voicing styles, phrases, and directions verified!\n");
}

/* ---------------- Test 4: Deterministic Galois LFSR Random Engine --- */
static void test_lfsr_random_engine(void)
{
    printf("== [Test 4] Deterministic Galois LFSR Random Engine & Cycle Period Locking\n");

    T1LFSRState lfsr;
    uint16_t seed = 0x5432;

    /* 1. Cycle Period Locking (random_rate 1 = loop every 2 cycles) */
    t1_lfsr_init(&lfsr, seed, 1);
    TEST_ASSERT(lfsr.loop_period == 2, "Loop period set to 2 cycles");

    /* Record sequence across 2 cycles */
    uint16_t seq1[10];
    for (int i = 0; i < 5; i++) {
        seq1[i] = t1_lfsr_next(&lfsr);
    }
    t1_lfsr_on_cycle_boundary(&lfsr); /* End of Cycle 1 */
    for (int i = 5; i < 10; i++) {
        seq1[i] = t1_lfsr_next(&lfsr);
    }
    t1_lfsr_on_cycle_boundary(&lfsr); /* End of Cycle 2: should wrap and reset seed! */

    /* Next 2 cycles must produce the EXACT same sequence */
    uint16_t seq2[10];
    for (int i = 0; i < 5; i++) {
        seq2[i] = t1_lfsr_next(&lfsr);
    }
    t1_lfsr_on_cycle_boundary(&lfsr);
    for (int i = 5; i < 10; i++) {
        seq2[i] = t1_lfsr_next(&lfsr);
    }

    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(seq1[i] == seq2[i], "Deterministic cycle loop produces exact repeatable sequence");
    }

    /* 2. Stochastic Modulation of Tagged Parameters */
    t1_lfsr_init(&lfsr, seed, 0);
    uint16_t targets = T1_RND_TARGET_VELOCITY | T1_RND_TARGET_PITCH;

    /* Velocity target tagged: should modulate within base +- (depth/2) */
    int32_t base_vel = 100;
    uint8_t depth = 20;
    int32_t mod_vel = t1_lfsr_modulate(&lfsr, targets, T1_RND_TARGET_VELOCITY, base_vel, depth, 1, 127);
    TEST_ASSERT(mod_vel >= 90 && mod_vel <= 110, "Modulated velocity stays within depth bounds");

    /* Untagged parameter (e.g. Sustain): should NOT modulate */
    int32_t base_sus = 50;
    int32_t mod_sus = t1_lfsr_modulate(&lfsr, targets, T1_RND_TARGET_SUSTAIN, base_sus, depth, 1, 127);
    TEST_ASSERT(mod_sus == base_sus, "Untagged parameter is unchanged");

    printf("   -> Deterministic Galois LFSR and cycle loop locking verified!\n");
}

/* ---------------- Test 5: Parameter Lock (P-Lock) Arena --- */
static void test_plock_arena(void)
{
    printf("== [Test 5] Parameter Lock (P-Lock) Arena (11 Entries x 8 Bytes = 88 Bytes)\n");

    CycleParams cp;
    memset(&cp, 0, sizeof(cp));
    t1_plock_clear_all(&cp);

    /* 1. Set parameter locks */
    bool s1 = t1_plock_set(&cp, 3, 10, 120); /* Step 3, param 10 = 120 */
    bool s2 = t1_plock_set(&cp, 7, 12, -45); /* Step 7, param 12 = -45 */
    TEST_ASSERT(s1 && s2, "Setting parameter locks succeeds");

    /* 2. Retrieve parameter locks */
    int16_t val = 0;
    TEST_ASSERT(t1_plock_get(&cp, 3, 10, &val) && val == 120, "Retrieved lock on step 3");
    TEST_ASSERT(t1_plock_get(&cp, 7, 12, &val) && val == -45, "Retrieved lock on step 7");
    TEST_ASSERT(!t1_plock_get(&cp, 4, 10, &val), "No lock on step 4");

    /* 3. Overwrite existing lock */
    t1_plock_set(&cp, 3, 10, 99);
    TEST_ASSERT(t1_plock_get(&cp, 3, 10, &val) && val == 99, "Overwritten lock value");

    /* 4. Fill to capacity (11 entries) */
    t1_plock_clear_all(&cp);
    for (uint8_t i = 0; i < T1_MAX_PLOCKS_PER_CYCLE; i++) {
        bool ok = t1_plock_set(&cp, i, 1, (int16_t)(i * 10));
        TEST_ASSERT(ok, "Filling up to 11 entries succeeds");
    }
    /* 12th entry should fail (arena full) */
    bool overflow = t1_plock_set(&cp, 15, 1, 999);
    TEST_ASSERT(!overflow, "12th parameter lock rejected when arena full");

    /* 5. Clear specific lock */
    TEST_ASSERT(t1_plock_clear(&cp, 5, 1), "Clearing lock succeeds");
    TEST_ASSERT(!t1_plock_get(&cp, 5, 1, &val), "Cleared lock is gone");

    /* Now 1 slot is free, set should succeed again */
    TEST_ASSERT(t1_plock_set(&cp, 15, 1, 999), "Setting into freed slot succeeds");

    printf("   -> Parameter lock arena (11 entries x 8 bytes) verified!\n");
}

/* ------------------------------------------------ Main Test Runner --- */
int main(void)
{
    printf("=================================================================\n");
    printf("  Running Torso T-1 Phase 2 Algorithmic Verification Suite\n");
    printf("=================================================================\n");

    test_euclidean_engine();
    test_pace_and_ratchet_accumulator();
    test_harmony_and_voicing_engine();
    test_lfsr_random_engine();
    test_plock_arena();

    printf("=================================================================\n");
    printf("  ALL %d TORSO T-1 PHASE 2 TESTS PASSED (%d assertions)\n", g_tests_passed, g_tests_run);
    printf("=================================================================\n");
    return 0;
}
