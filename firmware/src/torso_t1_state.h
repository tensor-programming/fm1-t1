/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Torso T-1 Architecture - Phase 4: State Machine, Performance Modes, and Storage Header */

#ifndef TORSO_T1_STATE_H
#define TORSO_T1_STATE_H

#include "torso_t1.h"
#include "torso_t1_algo.h"
#include "torso_t1_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Parameter Identifiers for Global Offsets --- */
typedef enum {
    T1_PARAM_STEPS = 0,
    T1_PARAM_PULSES,
    T1_PARAM_ROTATE,
    T1_PARAM_DIVISION,
    T1_PARAM_REPEATS,
    T1_PARAM_OFFSET,
    T1_PARAM_TIME,
    T1_PARAM_PACE,
    T1_PARAM_PITCH,
    T1_PARAM_SCALE,
    T1_PARAM_ROOT,
    T1_PARAM_VOICING,
    T1_PARAM_STYLE,
    T1_PARAM_PHRASE,
    T1_PARAM_RANGE,
    T1_PARAM_VELOCITY,
    T1_PARAM_SUSTAIN,
    T1_PARAM_PROBABILITY,
    T1_PARAM_DELAY,
    T1_PARAM_RANDOM_DEPTH,
    T1_PARAM_CHORD_SIZE,
    T1_PARAM_COUNT
} T1ParameterID;

/* ---------------- Performance Mode State --- */
typedef enum {
    T1_MODE_NORMAL = 0,
    T1_MODE_TEMP   = 1,   /* [TEMP] punch-in snapshot mode */
    T1_MODE_GLOBAL = 2,   /* [GLOBAL] parameter offset mode */
} T1PerformanceMode;

typedef struct {
    T1PerformanceMode mode;
    bool     is_temp_active;
    bool     is_global_active;
    uint32_t last_revert_duration_us;
    uint32_t max_revert_duration_us;
    uint32_t total_temp_reverts;
    uint32_t total_temp_commits;
    uint32_t total_global_tweaks;
} T1StateRuntime;

extern T1StateRuntime t1_state_runtime;

/* ---------------- 1. [TEMP] Snapshot Mode Functions --- */

/**
 * Initialize state runtime and default performance modes.
 */
void t1_state_init(void);

/**
 * Enter [TEMP] mode:
 * Preserves snapshot_pattern intact. Live edits apply to active_pattern.
 */
void t1_temp_enter(void);

/**
 * Release [TEMP] mode (Revert):
 * Reverts active_pattern back to snapshot_pattern in < 10 us via memcpy.
 * Invalidates Euclidean pipeline cached masks.
 */
void t1_temp_revert(void);

/**
 * Commit [TEMP] mode changes:
 * Overwrites snapshot_pattern with active_pattern.
 */
void t1_temp_commit(void);

/* ---------------- 2. [GLOBAL] Relative Track Offsets --- */

/**
 * Enter [GLOBAL] modifier mode.
 */
void t1_global_enter(void);

/**
 * Exit [GLOBAL] modifier mode.
 */
void t1_global_exit(void);

/**
 * Apply relative delta to a parameter across all 6 tracks simultaneously.
 * Respects individual parameter min/max bounds without overwriting individual track offsets.
 */
void t1_global_apply_delta(T1ParameterID param, int16_t delta);

/* ---------------- 3. SPI Flash Layout (36 Patterns Total) --- */

#define T1_FLASH_TOTAL_SIZE      (256u * 1024u)  /* 256 KB Dedicated SPI Flash */
#define T1_FLASH_BANK_STRIDE     (0x8000u)       /* 32 KB Stride per Bank */
#define T1_FLASH_BANK_ALLOCATION (28032u)        /* 6 * 4,672 B = 28.03 KB per Bank */
#define T1_FLASH_SECTOR_SIZE     (64u * 1024u)   /* 64 KB Erase Boundary */

/**
 * Return absolute flash base offset for a bank index (0..5).
 */
uint32_t t1_flash_get_bank_offset(uint8_t bank_idx);

/**
 * Return absolute flash offset for a pattern within a bank (bank 0..5, pattern 0..5).
 */
uint32_t t1_flash_get_pattern_offset(uint8_t bank_idx, uint8_t pat_idx);

/**
 * Verify that each bank's storage allocation (28,032 bytes) does not span
 * across non-contiguous 64 KB flash erase sectors (PLAN.md Checklist Item 2).
 * @return true if all 6 banks comply with sector boundary constraints.
 */
bool t1_flash_verify_boundaries(void);

/**
 * Driver abstraction callbacks for SPI flash I/O.
 */
typedef struct {
    int (*read)(uint32_t offset, void *dst, uint32_t size);
    int (*write)(uint32_t offset, const void *src, uint32_t size);
    int (*erase_sector)(uint32_t offset);
} T1FlashDriver;

/**
 * Save a single pattern to dedicated SPI flash.
 */
bool t1_flash_save_pattern(const T1FlashDriver *drv, uint8_t bank_idx, uint8_t pat_idx, const Pattern *pat);

/**
 * Load a single pattern from dedicated SPI flash.
 */
bool t1_flash_load_pattern(const T1FlashDriver *drv, uint8_t bank_idx, uint8_t pat_idx, Pattern *pat);

/**
 * Save an entire bank (6 patterns, 28,032 bytes) to SPI flash.
 */
bool t1_flash_save_bank(const T1FlashDriver *drv, uint8_t bank_idx, const Pattern patterns[PATTERNS_PER_BANK]);

/**
 * Load an entire bank (6 patterns, 28,032 bytes) from SPI flash.
 */
bool t1_flash_load_bank(const T1FlashDriver *drv, uint8_t bank_idx, Pattern patterns[PATTERNS_PER_BANK]);

#ifdef __cplusplus
}
#endif

#endif /* TORSO_T1_STATE_H */
