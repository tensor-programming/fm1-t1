/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Torso T-1 Architecture - Phase 3: Dual Output Routing & Event Pipeline Header */

#ifndef TORSO_T1_PIPELINE_H
#define TORSO_T1_PIPELINE_H

#include "torso_t1.h"
#include "torso_t1_algo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Division Timing in 192 PPQN Ticks --- */
enum {
    T1_DIV_1_1   = 0,  /* 1/1 Whole Note: 768 ticks */
    T1_DIV_1_2   = 1,  /* 1/2 Half Note: 384 ticks */
    T1_DIV_1_4   = 2,  /* 1/4 Quarter Note: 192 ticks */
    T1_DIV_1_8   = 3,  /* 1/8 Eighth Note: 96 ticks */
    T1_DIV_1_16  = 4,  /* 1/16 Sixteenth Note: 48 ticks (Default standard step) */
    T1_DIV_1_32  = 5,  /* 1/32 Thirty-second Note: 24 ticks */
    T1_DIV_1_8T  = 6,  /* 1/8 Triplet: 64 ticks (3 notes per quarter note) */
    T1_DIV_1_16T = 7,  /* 1/16 Triplet: 32 ticks (3 notes per eighth note) */
    T1_DIV_1_32T = 8,  /* 1/32 Triplet: 16 ticks */
    T1_DIV_1_8D  = 9,  /* 1/8 Dotted: 144 ticks */
    T1_DIV_1_16D = 10, /* 1/16 Dotted: 72 ticks */
    T1_DIV_COUNT = 11
};

/**
 * Return duration of one step in 192 PPQN ticks for a given division.
 */
uint32_t t1_division_ticks(uint8_t division);

/* ---------------- Per-Track Pipeline Runtime State --- */
typedef struct {
    uint32_t tick_accum;        /* Ticks accumulated in current step */
    uint32_t current_step_len;   /* Length of current step (with swing) */
    uint8_t  step_index;        /* Step index within active cycle (0 to steps - 1) */
    uint32_t pulse_counter;     /* Total Euclidean pulses triggered on this track */
    uint32_t cycle_pass;        /* Loop pass counter for cycle */
    uint64_t active_mask;       /* Cached Euclidean trigger mask */
    bool     mask_valid;        /* True if active_mask is cached and valid */
    T1LFSRState lfsr;           /* Deterministic LFSR state per track */
} T1TrackRuntime;

/* ---------------- Enhanced Voice Allocation for FM Core (Polyphony Stress Safe) --- */
#define T1_FM_POLYPHONY 6

typedef struct {
    uint8_t  note;
    uint8_t  velocity;
    bool     active;
    uint32_t age;
} T1VoiceSlot;

typedef struct {
    T1VoiceSlot slots[T1_FM_POLYPHONY];
    uint32_t    voice_age_counter;
    uint32_t    total_note_ons;
    uint32_t    total_note_offs;
    uint32_t    voice_steals;
} T1VoiceAllocator;

extern T1VoiceAllocator t1_voice_allocator;
extern T1TrackRuntime   t1_track_runtimes[NUM_TRACKS];

/* ---------------- Pipeline API Functions --- */

/**
 * Initialize the Phase 3 event pipeline and track runtimes.
 */
void t1_pipeline_init(void);

/**
 * Reset track playback position and clock alignment.
 */
void t1_pipeline_reset_transport(void);

/**
 * Invalidate cached Euclidean masks (called when parameters change).
 */
void t1_pipeline_invalidate_masks(void);

/**
 * Master 192 PPQN Pipeline Step Evaluator:
 * Evaluates all 6 tracks simultaneously on 192 PPQN ticks, schedules
 * micro-timing ratchets and offsets, and dispatches due events.
 */
void t1_pipeline_tick(uint32_t current_tick);

/**
 * Direct voice allocation for Track 0 FM synth with lowest-velocity / oldest-note stealing.
 * @return Allocated voice index 0..5, or -1 on error.
 */
int8_t t1_pipeline_alloc_voice(uint8_t note, uint8_t velocity);

/**
 * Release voice currently playing the specified note.
 * @return Voice index 0..5 that was freed, or -1 if not found.
 */
int8_t t1_pipeline_free_voice(uint8_t note);

/**
 * Reset all active FM voices and send clean note-offs.
 */
void t1_pipeline_kill_all_voices(void);

/**
 * Service the MIDI worker (Priority Level 4):
 * Flushes the UART and USB ring buffers to physical outputs.
 */
void t1_pipeline_flush_midi(void (*uart_out)(uint8_t byte), void (*usb_out)(uint32_t packet));

#ifdef __cplusplus
}
#endif

#endif /* TORSO_T1_PIPELINE_H */
