/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Torso T-1 Architecture - Phase 5: UI & Control Surface Adaptation Header */

#ifndef TORSO_T1_UI_H
#define TORSO_T1_UI_H

#include "torso_t1.h"
#include "torso_t1_algo.h"
#include "torso_t1_pipeline.h"
#include "torso_t1_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Torso T-1 Display Pages (4 Pages Total) --- */
typedef enum {
    T1_PAGE_RHYTHM  = 0, /* Page 1: Steps/MaxLen, Pulses/Rotate, Div/Gate, Cycle/StepMod */
    T1_PAGE_RATCHET = 1, /* Page 2: Repeats/Offset, Time/Pace, Velocity/Accent, Prob/Groove */
    T1_PAGE_HARMONY = 2, /* Page 3: Pitch/MasterTrans, Root/Scale, Voicing/Style, Phrase/Range */
    T1_PAGE_MOD_CC  = 3, /* Page 4: RndTarget/Channel, RndDepth/RndRate, CCNum/Slew, CCVal/LoopSync */
    T1_PAGE_COUNT   = 4
} T1UIPage;

/* ---------------- UI & Control Surface Context State --- */
typedef struct {
    uint8_t current_page;     /* T1UIPage (0..3) */
    uint8_t selected_track;   /* Active Track (0..5 -> Tracks 1..6) */
    uint8_t selected_cycle;   /* Active Cycle (0..5 -> Cycles 1..6) */
    bool    shift_active;     /* Secondary modifier held (Encoder Shift row) */
    bool    temp_active;      /* [TEMP] punch-in mode held */
    bool    global_active;    /* [GLOBAL] parameter offset modifier held */
    uint8_t edit_step;        /* 0..63 for per-step Parameter Locking (0xFF = track mode) */
    uint8_t hot_encoder;      /* 0..3: last adjusted physical encoder */
    uint8_t hot_timeout;      /* Frames to highlight last tweaked card */
} T1UIState;

extern T1UIState t1_ui_state;

/* ---------------- Lifecycle & Initialization --- */

/**
 * Initialize the Torso T-1 UI subsystem, default page, and track selection.
 */
void t1_ui_init(void);

/* ---------------- Navigation & Selection API --- */

/**
 * Select active track (0..5 -> Tracks 1..6).
 */
void t1_ui_select_track(uint8_t track_idx);

/**
 * Cycle forward / backward through tracks 1..6.
 */
void t1_ui_next_track(void);
void t1_ui_prev_track(void);

/**
 * Select active cycle (0..5 -> Cycles 1..6).
 */
void t1_ui_select_cycle(uint8_t cycle_idx);

/**
 * Switch active UI page (0..3).
 */
void t1_ui_set_page(T1UIPage page);
void t1_ui_next_page(void);
void t1_ui_prev_page(void);

/**
 * Modifier state toggles / holds.
 */
void t1_ui_set_shift(bool active);
void t1_ui_set_temp(bool active);
void t1_ui_set_global(bool active);

/**
 * Parameter Lock step selection (0xFF = clear P-Lock cursor).
 */
void t1_ui_set_edit_step(uint8_t step_idx);

/* ---------------- Physical Encoder Handling (4 Knobs) --- */

/**
 * Handle rotary encoder rotation for one of the 4 physical knobs.
 * @param encoder_idx Physical knob index (0..3)
 * @param steps       Signed rotary delta (e.g. +1, -1, +2, -2)
 */
void t1_ui_on_encoder_turn(uint8_t encoder_idx, int8_t steps);

/* ---------------- Screen Rendering Metadata API --- */

/**
 * Format title line for the screen header.
 * e.g. "T1:C1 RHYTHM [TEMP]"
 */
void t1_ui_get_header_string(char *dst, uint32_t max_len);

/**
 * Fetch card rendering information for one of the 4 on-screen columns.
 * @param col        Column index 0..3
 * @param out_label  Destination buffer for card parameter title (e.g. "STEPS", "PULSES")
 * @param out_val    Destination buffer for formatted value (e.g. "16", "3/8", "C#")
 * @param out_unit   Destination pointer for unit string (e.g. "st", "%", "bpm")
 * @param out_ratio  Destination for bar meter ratio (0..1000, or -1 for none)
 */
void t1_ui_get_card_info(uint8_t col, char *out_label, char *out_val, const char **out_unit, int32_t *out_ratio);

#ifdef __cplusplus
}
#endif

#endif /* TORSO_T1_UI_H */
