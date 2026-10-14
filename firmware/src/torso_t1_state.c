/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Torso T-1 Architecture - Phase 4: State Machine, Performance Modes, and Storage Implementation */

#include "torso_t1_state.h"

#if !defined(FELUCCA_ID) && !defined(FM1_SYS_H)
#include <string.h>
#else
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
#endif

T1StateRuntime t1_state_runtime;

/* ---------------- 1. [TEMP] Snapshot Mode Functions --- */

void t1_state_init(void)
{
    memset(&t1_state_runtime, 0, sizeof(t1_state_runtime));
    t1_state_runtime.mode = T1_MODE_NORMAL;

    /* Prime snapshot buffer with active pattern so revert state is valid */
    memcpy(&t1_sequencer_ram.snapshot_pattern,
           &t1_sequencer_ram.active_pattern,
           sizeof(Pattern));
}

void t1_temp_enter(void)
{
    if (!t1_state_runtime.is_temp_active) {
        /* Entering [TEMP]: keep snapshot_pattern intact as baseline */
        memcpy(&t1_sequencer_ram.snapshot_pattern,
               &t1_sequencer_ram.active_pattern,
               sizeof(Pattern));
        t1_state_runtime.is_temp_active = true;
        t1_state_runtime.mode           = T1_MODE_TEMP;
    }
}

void t1_temp_revert(void)
{
    uint32_t t0 = t1_get_now_us();

    /* Revert active_pattern back to snapshot_pattern in under 6 microseconds */
    memcpy(&t1_sequencer_ram.active_pattern,
           &t1_sequencer_ram.snapshot_pattern,
           sizeof(Pattern));

    /* Invalidate cached Euclidean masks to instantly reflect restored state */
    t1_pipeline_invalidate_masks();

    uint32_t duration_us = t1_get_now_us() - t0;
    t1_state_runtime.last_revert_duration_us = duration_us;
    if (duration_us > t1_state_runtime.max_revert_duration_us) {
        t1_state_runtime.max_revert_duration_us = duration_us;
    }
    t1_state_runtime.total_temp_reverts++;

    t1_state_runtime.is_temp_active = false;
    t1_state_runtime.mode           = T1_MODE_NORMAL;
}

void t1_temp_commit(void)
{
    /* Commit: promote active_pattern into snapshot_pattern */
    memcpy(&t1_sequencer_ram.snapshot_pattern,
           &t1_sequencer_ram.active_pattern,
           sizeof(Pattern));

    t1_state_runtime.total_temp_commits++;
    t1_state_runtime.is_temp_active = false;
    t1_state_runtime.mode           = T1_MODE_NORMAL;
}

/* ---------------- 2. [GLOBAL] Relative Track Offsets --- */

void t1_global_enter(void)
{
    t1_state_runtime.is_global_active = true;
    t1_state_runtime.mode             = T1_MODE_GLOBAL;
}

void t1_global_exit(void)
{
    t1_state_runtime.is_global_active = false;
    if (!t1_state_runtime.is_temp_active) {
        t1_state_runtime.mode = T1_MODE_NORMAL;
    } else {
        t1_state_runtime.mode = T1_MODE_TEMP;
    }
}

void t1_global_apply_delta(T1ParameterID param, int16_t delta)
{
    if (delta == 0) return;

    Pattern *pat = &t1_sequencer_ram.active_pattern;
    bool invalidate_masks = false;

    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        Track *trk = &pat->tracks[t];
        uint8_t c_idx = trk->active_cycle % NUM_CYCLES;
        CycleParams *cp = &trk->cycles[c_idx];

        switch (param) {
            case T1_PARAM_STEPS: {
                int16_t v = (int16_t)cp->steps + delta;
                if (v < 1)  v = 1;
                if (v > 64) v = 64;
                cp->steps = (uint8_t)v;
                if (cp->pulses > cp->steps) cp->pulses = cp->steps;
                invalidate_masks = true;
                break;
            }
            case T1_PARAM_PULSES: {
                int16_t v = (int16_t)cp->pulses + delta;
                if (v < 0) v = 0;
                uint8_t max_p = cp->steps ? cp->steps : 16u;
                if (v > max_p) v = max_p;
                cp->pulses = (uint8_t)v;
                invalidate_masks = true;
                break;
            }
            case T1_PARAM_ROTATE: {
                int16_t v = (int16_t)cp->rotate + delta;
                if (v < -64) v = -64;
                if (v > 64)  v = 64;
                cp->rotate = (int8_t)v;
                invalidate_masks = true;
                break;
            }
            case T1_PARAM_DIVISION: {
                int16_t v = (int16_t)cp->division + delta;
                if (v < 0) v = 0;
                if (v >= T1_DIV_COUNT) v = T1_DIV_COUNT - 1;
                cp->division = (uint8_t)v;
                break;
            }
            case T1_PARAM_REPEATS: {
                int16_t v = (int16_t)cp->repeats + delta;
                if (v < 1)  v = 1;
                if (v > 16) v = 16;
                cp->repeats = (uint8_t)v;
                break;
            }
            case T1_PARAM_OFFSET: {
                int16_t v = (int16_t)cp->offset + delta;
                if (v < -64) v = -64;
                if (v > 63)  v = 63;
                cp->offset = (int8_t)v;
                break;
            }
            case T1_PARAM_TIME: {
                int16_t v = (int16_t)cp->time + delta;
                if (v < 1)   v = 1;
                if (v > 127) v = 127;
                cp->time = (uint8_t)v;
                break;
            }
            case T1_PARAM_PACE: {
                int16_t v = (int16_t)cp->pace + delta;
                if (v < -64) v = -64;
                if (v > 63)  v = 63;
                cp->pace = (int8_t)v;
                break;
            }
            case T1_PARAM_PITCH: {
                int16_t v = (int16_t)cp->pitch_offset + delta;
                if (v < -48) v = -48;
                if (v > 48)  v = 48;
                cp->pitch_offset = (int8_t)v;
                break;
            }
            case T1_PARAM_SCALE: {
                int16_t v = (int16_t)cp->scale_id + delta;
                if (v < 0)  v = 0;
                if (v > 15) v = 15;
                cp->scale_id = (uint8_t)v;
                break;
            }
            case T1_PARAM_ROOT: {
                int16_t v = (int16_t)cp->root_note + delta;
                v = ((v % 12) + 12) % 12;
                cp->root_note = (uint8_t)v;
                break;
            }
            case T1_PARAM_VOICING: {
                int16_t v = (int16_t)cp->voicing + delta;
                if (v < 0)  v = 0;
                if (v > 11) v = 11;
                cp->voicing = (uint8_t)v;
                break;
            }
            case T1_PARAM_STYLE: {
                int16_t v = (int16_t)cp->style + delta;
                if (v < 0) v = 0;
                if (v > 7) v = 7;
                cp->style = (uint8_t)v;
                break;
            }
            case T1_PARAM_PHRASE: {
                int16_t v = (int16_t)cp->phrase + delta;
                if (v < 0) v = 0;
                if (v > 7) v = 7;
                cp->phrase = (uint8_t)v;
                break;
            }
            case T1_PARAM_RANGE: {
                int16_t v = (int16_t)cp->range + delta;
                if (v < 0)   v = 0;
                if (v > 127) v = 127;
                cp->range = (uint8_t)v;
                break;
            }
            case T1_PARAM_VELOCITY: {
                int16_t v = (int16_t)cp->velocity + delta;
                if (v < 1)   v = 1;
                if (v > 127) v = 127;
                cp->velocity = (uint8_t)v;
                break;
            }
            case T1_PARAM_SUSTAIN: {
                int16_t v = (int16_t)cp->sustain + delta;
                if (v < 1)   v = 1;
                if (v > 255) v = 255;
                cp->sustain = (uint8_t)v;
                break;
            }
            case T1_PARAM_PROBABILITY: {
                int16_t v = (int16_t)cp->probability + delta;
                if (v < 0)   v = 0;
                if (v > 100) v = 100;
                cp->probability = (uint8_t)v;
                break;
            }
            case T1_PARAM_DELAY: {
                int16_t v = (int16_t)cp->delay_ticks + delta;
                if (v < -12) v = -12;
                if (v > 12)  v = 12;
                cp->delay_ticks = (int8_t)v;
                break;
            }
            case T1_PARAM_RANDOM_DEPTH: {
                int16_t v = (int16_t)cp->random_depth + delta;
                if (v < 0)   v = 0;
                if (v > 127) v = 127;
                cp->random_depth = (uint8_t)v;
                break;
            }
            case T1_PARAM_CHORD_SIZE: {
                int16_t v = (int16_t)cp->chord_size + delta;
                if (v < 1) v = 1;
                if (v > 4) v = 4;
                cp->chord_size = (uint8_t)v;
                break;
            }
            default:
                break;
        }
    }

    if (invalidate_masks) {
        t1_pipeline_invalidate_masks();
    }

    t1_state_runtime.total_global_tweaks++;
}

/* ---------------- 3. SPI Flash Layout (36 Patterns Total) --- */

uint32_t t1_flash_get_bank_offset(uint8_t bank_idx)
{
    return (uint32_t)(bank_idx % NUM_BANKS) * T1_FLASH_BANK_STRIDE;
}

uint32_t t1_flash_get_pattern_offset(uint8_t bank_idx, uint8_t pat_idx)
{
    uint32_t bank_base = t1_flash_get_bank_offset(bank_idx);
    return bank_base + (uint32_t)(pat_idx % PATTERNS_PER_BANK) * (uint32_t)sizeof(Pattern);
}

bool t1_flash_verify_boundaries(void)
{
    for (uint8_t b = 0; b < NUM_BANKS; b++) {
        uint32_t start_off = t1_flash_get_bank_offset(b);
        uint32_t end_off   = start_off + T1_FLASH_BANK_ALLOCATION - 1u;

        /* Verify bank start is 32 KB block aligned */
        if ((start_off & (T1_FLASH_BANK_STRIDE - 1u)) != 0) {
            return false;
        }

        /* Verify bank fits within the 32 KB block */
        if ((start_off + T1_FLASH_BANK_ALLOCATION) > (start_off + T1_FLASH_BANK_STRIDE)) {
            return false;
        }

        /* Verify bank does not cross 64 KB sector boundary */
        uint32_t start_sector = start_off / T1_FLASH_SECTOR_SIZE;
        uint32_t end_sector   = end_off / T1_FLASH_SECTOR_SIZE;
        if (start_sector != end_sector) {
            return false;
        }

        /* Verify fits within total 256 KB SPI Flash space */
        if (end_off >= T1_FLASH_TOTAL_SIZE) {
            return false;
        }
    }

    return true;
}

bool t1_flash_save_pattern(const T1FlashDriver *drv, uint8_t bank_idx, uint8_t pat_idx, const Pattern *pat)
{
    if (!drv || !drv->write || !pat) return false;
    if (bank_idx >= NUM_BANKS || pat_idx >= PATTERNS_PER_BANK) return false;

    uint32_t offset = t1_flash_get_pattern_offset(bank_idx, pat_idx);
    int rc = drv->write(offset, pat, sizeof(Pattern));
    return (rc == 0);
}

bool t1_flash_load_pattern(const T1FlashDriver *drv, uint8_t bank_idx, uint8_t pat_idx, Pattern *pat)
{
    if (!drv || !drv->read || !pat) return false;
    if (bank_idx >= NUM_BANKS || pat_idx >= PATTERNS_PER_BANK) return false;

    uint32_t offset = t1_flash_get_pattern_offset(bank_idx, pat_idx);
    int rc = drv->read(offset, pat, sizeof(Pattern));
    return (rc == 0);
}

bool t1_flash_save_bank(const T1FlashDriver *drv, uint8_t bank_idx, const Pattern patterns[PATTERNS_PER_BANK])
{
    if (!drv || !drv->write || !patterns) return false;
    if (bank_idx >= NUM_BANKS) return false;

    uint32_t offset = t1_flash_get_bank_offset(bank_idx);

    if (drv->erase_sector) {
        int err = drv->erase_sector(offset);
        if (err != 0) return false;
    }

    int rc = drv->write(offset, patterns, T1_FLASH_BANK_ALLOCATION);
    return (rc == 0);
}

bool t1_flash_load_bank(const T1FlashDriver *drv, uint8_t bank_idx, Pattern patterns[PATTERNS_PER_BANK])
{
    if (!drv || !drv->read || !patterns) return false;
    if (bank_idx >= NUM_BANKS) return false;

    uint32_t offset = t1_flash_get_bank_offset(bank_idx);
    int rc = drv->read(offset, patterns, T1_FLASH_BANK_ALLOCATION);
    return (rc == 0);
}

