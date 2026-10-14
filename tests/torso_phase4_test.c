/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Test Suite: Torso T-1 Architecture - Phase 4 Verification */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "torso_t1.h"
#include "torso_t1_algo.h"
#include "torso_t1_pipeline.h"
#include "torso_t1_state.h"

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

/* ---------------- Simulated 256 KB Dedicated SPI Flash Driver --- */
static uint8_t g_sim_flash[T1_FLASH_TOTAL_SIZE];
static uint32_t g_flash_erase_count = 0;
static uint32_t g_flash_write_count = 0;
static uint32_t g_flash_read_count = 0;

static int sim_flash_read(uint32_t offset, void *dst, uint32_t size)
{
    if (offset + size > T1_FLASH_TOTAL_SIZE) return -1;
    memcpy(dst, &g_sim_flash[offset], size);
    g_flash_read_count++;
    return 0;
}

static int sim_flash_write(uint32_t offset, const void *src, uint32_t size)
{
    if (offset + size > T1_FLASH_TOTAL_SIZE) return -1;
    /* NOR flash programming: bits can only be flipped 1 -> 0 without an erase */
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < size; i++) {
        g_sim_flash[offset + i] &= s[i];
    }
    g_flash_write_count++;
    return 0;
}

static int sim_flash_erase_sector(uint32_t offset)
{
    /* Align to 32 KB or 64 KB sector */
    uint32_t sector_base = offset & ~(T1_FLASH_BANK_STRIDE - 1u);
    if (sector_base + T1_FLASH_BANK_STRIDE > T1_FLASH_TOTAL_SIZE) return -1;
    memset(&g_sim_flash[sector_base], 0xFF, T1_FLASH_BANK_STRIDE);
    g_flash_erase_count++;
    return 0;
}

static const T1FlashDriver g_sim_driver = {
    .read         = sim_flash_read,
    .write        = sim_flash_write,
    .erase_sector = sim_flash_erase_sector
};

static void reset_sim_flash(void)
{
    memset(g_sim_flash, 0xFF, sizeof(g_sim_flash));
    g_flash_erase_count = 0;
    g_flash_write_count = 0;
    g_flash_read_count  = 0;
}

/* ---------------- Test 1: [TEMP] Mode & Snapshot Revert Latency (< 10 us, Checklist Item 4) --- */
static void test_temp_mode_and_latency(void)
{
    printf("== [Test 1] [TEMP] Mode & Snapshot Revert Latency (< 10 us Target, PLAN.md Checklist Item 4)\n");

    t1_system_init();
    t1_pipeline_init();
    t1_state_init();

    Pattern *pat = &t1_sequencer_ram.active_pattern;
    pat->bpm_x10 = 1250;
    pat->swing   = 55;
    pat->tracks[0].cycles[0].steps    = 16;
    pat->tracks[0].cycles[0].pulses   = 4;
    pat->tracks[0].cycles[0].velocity = 100;
    pat->tracks[1].cycles[0].steps    = 8;
    pat->tracks[1].cycles[0].pulses   = 3;

    /* Enter [TEMP] mode: preserves baseline snapshot */
    t1_temp_enter();
    TEST_ASSERT(t1_state_runtime.is_temp_active, "[TEMP] mode active flag set");
    TEST_ASSERT(t1_state_runtime.mode == T1_MODE_TEMP, "[TEMP] mode enum set");

    /* Live performance modifications during [TEMP] */
    pat->tracks[0].cycles[0].steps    = 32; /* User tweaked steps */
    pat->tracks[0].cycles[0].pulses   = 16;/* User tweaked pulses */
    pat->tracks[0].cycles[0].velocity = 127;
    pat->tracks[0].muted              = true;/* User muted Track 0 */
    pat->bpm_x10                      = 1500;/* Tempo change */

    TEST_ASSERT(pat->tracks[0].cycles[0].steps == 32, "Active pattern modified");
    TEST_ASSERT(t1_sequencer_ram.snapshot_pattern.tracks[0].cycles[0].steps == 16, "Snapshot pattern intact at 16 steps");
    TEST_ASSERT(t1_sequencer_ram.snapshot_pattern.bpm_x10 == 1250, "Snapshot tempo intact at 125 BPM");

    /* Benchmark [TEMP] revert operation over 1,000 runs */
    uint64_t total_revert_ns = 0;
    uint32_t max_revert_ns = 0;
    const uint32_t REVERT_ITERATIONS = 1000;

    for (uint32_t i = 0; i < REVERT_ITERATIONS; i++) {
        /* Mess up active pattern */
        pat->bpm_x10 = (uint16_t)(1000 + i);
        pat->tracks[0].cycles[0].steps = (uint8_t)(i % 64 + 1);

        struct timespec ts0, ts1;
        clock_gettime(CLOCK_MONOTONIC, &ts0);

        t1_temp_revert();

        clock_gettime(CLOCK_MONOTONIC, &ts1);
        uint64_t dur_ns = (uint64_t)(ts1.tv_sec - ts0.tv_sec) * 1000000000ULL + (uint64_t)(ts1.tv_nsec - ts0.tv_nsec);
        total_revert_ns += dur_ns;
        if (dur_ns > max_revert_ns) {
            max_revert_ns = (uint32_t)dur_ns;
        }

        /* Verify instant revert */
        TEST_ASSERT(pat->bpm_x10 == 1250, "BPM restored");
        TEST_ASSERT(pat->tracks[0].cycles[0].steps == 16, "Steps restored");
        TEST_ASSERT(pat->tracks[0].muted == false, "Mute restored");
    }

    uint32_t avg_ns = (uint32_t)(total_revert_ns / REVERT_ITERATIONS);
    uint32_t max_us = max_revert_ns / 1000u;

    printf("   -> Benchmark over %u [TEMP] reverts (4,672 bytes Pattern):\n", REVERT_ITERATIONS);
    printf("   -> Average revert duration: %u ns (%.3f us)\n", avg_ns, (double)avg_ns / 1000.0);
    printf("   -> Maximum revert duration: %u us (Budget: < 10 us)\n", max_us);

    TEST_ASSERT(max_us < 10u, "[TEMP] revert memcpy duration must complete within strictly < 10 us (PLAN.md Checklist Item 4)");
    TEST_ASSERT(!t1_state_runtime.is_temp_active, "[TEMP] mode inactive after revert");

    /* Test [TEMP] commit: modifications are saved into snapshot */
    t1_temp_enter();
    pat->tracks[0].cycles[0].steps = 24;
    pat->tracks[0].cycles[0].pulses = 12;
    t1_temp_commit();

    TEST_ASSERT(!t1_state_runtime.is_temp_active, "[TEMP] inactive after commit");
    TEST_ASSERT(t1_sequencer_ram.snapshot_pattern.tracks[0].cycles[0].steps == 24, "Snapshot updated with committed steps");
    TEST_ASSERT(t1_sequencer_ram.snapshot_pattern.tracks[0].cycles[0].pulses == 12, "Snapshot updated with committed pulses");

    printf("   -> [TEMP] mode and snapshot revert latency target verified!\n");
}

/* ---------------- Test 2: [GLOBAL] Relative Parameter Offsets --- */
static void test_global_parameter_offsets(void)
{
    printf("== [Test 2] [GLOBAL] Relative Track Parameter Offsets\n");

    t1_system_init();
    t1_state_init();

    Pattern *pat = &t1_sequencer_ram.active_pattern;

    /* Set up distinct initial parameter baselines for each track */
    pat->tracks[0].cycles[0].steps       = 16;
    pat->tracks[1].cycles[0].steps       = 12;
    pat->tracks[2].cycles[0].steps       = 8;
    pat->tracks[3].cycles[0].steps       = 24;
    pat->tracks[4].cycles[0].steps       = 32;
    pat->tracks[5].cycles[0].steps       = 60;

    pat->tracks[0].cycles[0].velocity    = 100;
    pat->tracks[1].cycles[0].velocity    = 90;
    pat->tracks[2].cycles[0].velocity    = 80;
    pat->tracks[3].cycles[0].velocity    = 110;
    pat->tracks[4].cycles[0].velocity    = 120;
    pat->tracks[5].cycles[0].velocity    = 70;

    pat->tracks[0].cycles[0].pitch_offset= 0;
    pat->tracks[1].cycles[0].pitch_offset= -5;
    pat->tracks[2].cycles[0].pitch_offset= 7;
    pat->tracks[3].cycles[0].pitch_offset= 12;
    pat->tracks[4].cycles[0].pitch_offset= -12;
    pat->tracks[5].cycles[0].pitch_offset= 4;

    /* Enter [GLOBAL] mode */
    t1_global_enter();
    TEST_ASSERT(t1_state_runtime.is_global_active, "[GLOBAL] active");
    TEST_ASSERT(t1_state_runtime.mode == T1_MODE_GLOBAL, "Mode is GLOBAL");

    /* 1. Apply delta +2 to STEPS across all tracks */
    t1_global_apply_delta(T1_PARAM_STEPS, +2);

    TEST_ASSERT(pat->tracks[0].cycles[0].steps == 18, "Track 0 steps: 16 -> 18");
    TEST_ASSERT(pat->tracks[1].cycles[0].steps == 14, "Track 1 steps: 12 -> 14");
    TEST_ASSERT(pat->tracks[2].cycles[0].steps == 10, "Track 2 steps: 8 -> 10");
    TEST_ASSERT(pat->tracks[3].cycles[0].steps == 26, "Track 3 steps: 24 -> 26");
    TEST_ASSERT(pat->tracks[4].cycles[0].steps == 34, "Track 4 steps: 32 -> 34");
    TEST_ASSERT(pat->tracks[5].cycles[0].steps == 62, "Track 5 steps: 60 -> 62");

    /* 2. Apply delta +10 to STEPS: Track 5 should clamp at 64 max */
    t1_global_apply_delta(T1_PARAM_STEPS, +10);
    TEST_ASSERT(pat->tracks[5].cycles[0].steps == 64, "Track 5 steps clamped at 64 max");
    TEST_ASSERT(pat->tracks[0].cycles[0].steps == 28, "Track 0 steps: 18 -> 28");

    /* 3. Apply delta -15 to VELOCITY */
    t1_global_apply_delta(T1_PARAM_VELOCITY, -15);
    TEST_ASSERT(pat->tracks[0].cycles[0].velocity == 85,  "Track 0 vel: 100 -> 85");
    TEST_ASSERT(pat->tracks[1].cycles[0].velocity == 75,  "Track 1 vel: 90 -> 75");
    TEST_ASSERT(pat->tracks[2].cycles[0].velocity == 65,  "Track 2 vel: 80 -> 65");
    TEST_ASSERT(pat->tracks[3].cycles[0].velocity == 95,  "Track 3 vel: 110 -> 95");
    TEST_ASSERT(pat->tracks[4].cycles[0].velocity == 105, "Track 4 vel: 120 -> 105");
    TEST_ASSERT(pat->tracks[5].cycles[0].velocity == 55,  "Track 5 vel: 70 -> 55");

    /* 4. Apply delta +12 (1 octave) to PITCH */
    t1_global_apply_delta(T1_PARAM_PITCH, +12);
    TEST_ASSERT(pat->tracks[0].cycles[0].pitch_offset == 12,  "Track 0 pitch: 0 -> +12");
    TEST_ASSERT(pat->tracks[1].cycles[0].pitch_offset == 7,   "Track 1 pitch: -5 -> +7");
    TEST_ASSERT(pat->tracks[2].cycles[0].pitch_offset == 19,  "Track 2 pitch: 7 -> +19");
    TEST_ASSERT(pat->tracks[3].cycles[0].pitch_offset == 24,  "Track 3 pitch: 12 -> +24");
    TEST_ASSERT(pat->tracks[4].cycles[0].pitch_offset == 0,   "Track 4 pitch: -12 -> 0");
    TEST_ASSERT(pat->tracks[5].cycles[0].pitch_offset == 16,  "Track 5 pitch: 4 -> +16");

    /* Exit [GLOBAL] mode */
    t1_global_exit();
    TEST_ASSERT(!t1_state_runtime.is_global_active, "[GLOBAL] exited");
    TEST_ASSERT(t1_state_runtime.mode == T1_MODE_NORMAL, "Mode restored to NORMAL");

    printf("   -> Global track offsets and relative delta preservation verified!\n");
}

/* ---------------- Test 3: SPI Flash Layout & Sector Boundary Verification (Checklist Item 2) --- */
static void test_flash_layout_and_boundaries(void)
{
    printf("== [Test 3] SPI Flash Layout & Sector Boundary Verification (PLAN.md Checklist Item 2)\n");

    /*
     * PLAN.md Phase 4 specification:
     * Bank 0: 0x00000 – 0x06FFF (28.03 KB)
     * Bank 1: 0x08000 – 0x0EFFF (28.03 KB)
     * Bank 2: 0x10000 – 0x16FFF (28.03 KB)
     * Bank 3: 0x18000 – 0x1EFFF (28.03 KB)
     * Bank 4: 0x20000 – 0x26FFF (28.03 KB)
     * Bank 5: 0x28000 – 0x2EFFF (28.03 KB)
     */
    TEST_ASSERT(t1_flash_get_bank_offset(0) == 0x00000u, "Bank 0 base matches 0x00000");
    TEST_ASSERT(t1_flash_get_bank_offset(1) == 0x08000u, "Bank 1 base matches 0x08000");
    TEST_ASSERT(t1_flash_get_bank_offset(2) == 0x10000u, "Bank 2 base matches 0x10000");
    TEST_ASSERT(t1_flash_get_bank_offset(3) == 0x18000u, "Bank 3 base matches 0x18000");
    TEST_ASSERT(t1_flash_get_bank_offset(4) == 0x20000u, "Bank 4 base matches 0x20000");
    TEST_ASSERT(t1_flash_get_bank_offset(5) == 0x28000u, "Bank 5 base matches 0x28000");

    /* Verify each bank allocation is exactly 28,032 bytes (6 * 4,672) */
    TEST_ASSERT(T1_FLASH_BANK_ALLOCATION == 28032u, "Bank storage allocation = 28,032 bytes");
    TEST_ASSERT(T1_FLASH_BANK_ALLOCATION == PATTERNS_PER_BANK * sizeof(Pattern), "Bank holds 6 full patterns");

    /* Verify end offsets fit inside designated 28.03 KB (0x7000 = 28,672 bytes) windows */
    for (uint8_t b = 0; b < NUM_BANKS; b++) {
        uint32_t base = t1_flash_get_bank_offset(b);
        uint32_t end  = base + T1_FLASH_BANK_ALLOCATION - 1u;
        uint32_t max_allowed = base + 0x07000u - 1u;

        TEST_ASSERT(end <= max_allowed, "Bank allocation fits strictly within 0x7000 (28.03 KB)");
        TEST_ASSERT(end < base + T1_FLASH_BANK_STRIDE, "Bank allocation fits strictly within 32 KB block");
    }

    /* Verify boundary constraints against 64 KB SPI flash erase sectors */
    bool boundaries_ok = t1_flash_verify_boundaries();
    TEST_ASSERT(boundaries_ok, "Flash boundary verification confirms no bank crosses a 64 KB sector boundary");

    /* Verify pattern offsets within Bank 0 */
    for (uint8_t p = 0; p < PATTERNS_PER_BANK; p++) {
        uint32_t p_off = t1_flash_get_pattern_offset(0, p);
        TEST_ASSERT(p_off == p * sizeof(Pattern), "Pattern offset in Bank 0 is contiguous");
    }

    printf("   -> Flash boundary verification passed: all 6 banks comply with sector erase limits!\n");
}

/* ---------------- Test 4: Flash Read/Write/Erase Serialization & Streaming --- */
static void test_flash_save_load_streaming(void)
{
    printf("== [Test 4] Flash Read/Write/Erase Serialization & Streaming\n");

    reset_sim_flash();

    /* 1. Create distinctive pattern data */
    Pattern test_pat;
    memset(&test_pat, 0, sizeof(test_pat));
    test_pat.bpm_x10      = 1337;
    test_pat.swing        = 67;
    test_pat.master_scale = 3;
    test_pat.master_root  = 7;
    test_pat.tracks[0].cycles[0].steps    = 32;
    test_pat.tracks[0].cycles[0].pulses   = 11;
    test_pat.tracks[0].cycles[0].repeats  = 4;
    test_pat.tracks[0].cycles[0].velocity = 115;
    test_pat.tracks[5].cycles[0].steps    = 15;
    test_pat.tracks[5].cycles[0].pulses   = 7;

    /* Erase sector and save single pattern to Bank 2, Pattern 3 */
    sim_flash_erase_sector(t1_flash_get_bank_offset(2));
    bool save_ok = t1_flash_save_pattern(&g_sim_driver, 2, 3, &test_pat);
    TEST_ASSERT(save_ok, "Saved pattern to Bank 2, Pattern 3");

    /* Load back pattern into separate buffer */
    Pattern loaded_pat;
    memset(&loaded_pat, 0, sizeof(loaded_pat));
    bool load_ok = t1_flash_load_pattern(&g_sim_driver, 2, 3, &loaded_pat);
    TEST_ASSERT(load_ok, "Loaded pattern from Bank 2, Pattern 3");

    /* Verify bit-for-bit exact memory match (4,672 bytes) */
    TEST_ASSERT(memcmp(&test_pat, &loaded_pat, sizeof(Pattern)) == 0, "Loaded pattern is bit-for-bit identical to saved pattern");
    TEST_ASSERT(loaded_pat.bpm_x10 == 1337, "Loaded BPM matches");
    TEST_ASSERT(loaded_pat.swing == 67, "Loaded swing matches");
    TEST_ASSERT(loaded_pat.tracks[0].cycles[0].steps == 32, "Loaded steps match");

    /* 2. Test saving and loading an entire bank (6 patterns, 28,032 bytes) */
    Pattern bank_patterns[PATTERNS_PER_BANK];
    for (uint8_t p = 0; p < PATTERNS_PER_BANK; p++) {
        memset(&bank_patterns[p], 0, sizeof(Pattern));
        bank_patterns[p].bpm_x10 = (uint16_t)(1000 + p * 50);
        bank_patterns[p].tracks[0].cycles[0].steps = (uint8_t)(8 + p * 4);
    }

    bool bank_save_ok = t1_flash_save_bank(&g_sim_driver, 4, bank_patterns);
    TEST_ASSERT(bank_save_ok, "Saved entire bank 4 (28,032 bytes)");

    Pattern loaded_bank[PATTERNS_PER_BANK];
    memset(loaded_bank, 0, sizeof(loaded_bank));
    bool bank_load_ok = t1_flash_load_bank(&g_sim_driver, 4, loaded_bank);
    TEST_ASSERT(bank_load_ok, "Loaded entire bank 4 (28,032 bytes)");

    for (uint8_t p = 0; p < PATTERNS_PER_BANK; p++) {
        TEST_ASSERT(memcmp(&bank_patterns[p], &loaded_bank[p], sizeof(Pattern)) == 0, "Bank pattern bit-for-bit identical");
        TEST_ASSERT(loaded_bank[p].bpm_x10 == (uint16_t)(1000 + p * 50), "Bank pattern BPM matches");
    }

    /* 3. Streaming time calculation verification (PLAN.md Phase 4 specification) */
    /* At 32 MHz SPI bus rate, 1 byte = 8 bits / 32,000,000 = 0.25 us */
    /* 4,672 bytes * 0.25 us = 1,168 us = 1.168 ms (~ 1.17 ms) */
    uint32_t pattern_bytes = sizeof(Pattern);
    double spi_transfer_ms = ((double)pattern_bytes * 8.0) / 32000.0;
    printf("   -> 4.67 KB pattern stream at 32 MHz SPI: %.2f ms (~ 1.17 ms target)\n", spi_transfer_ms);
    TEST_ASSERT(spi_transfer_ms > 1.15 && spi_transfer_ms < 1.18, "Pattern streaming transfer matches 1.17 ms @ 32 MHz");

    printf("   -> Flash read/write/erase and pattern streaming verified!\n");
}

/* ---------------- Main Runner --- */
int main(void)
{
    printf("=================================================================\n");
    printf("  Running Torso T-1 Phase 4 State Machine & Storage Test Suite\n");
    printf("=================================================================\n");

    test_temp_mode_and_latency();
    test_global_parameter_offsets();
    test_flash_layout_and_boundaries();
    test_flash_save_load_streaming();

    printf("=================================================================\n");
    printf("  ALL %d TORSO T-1 PHASE 4 TESTS PASSED (%d assertions)\n",
           g_tests_run, g_tests_passed);
    printf("=================================================================\n");

    return 0;
}
