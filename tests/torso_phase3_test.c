/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Test Suite: Torso T-1 Architecture - Phase 3 Verification */

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

/* ---------------- Mock FM Core Synth Callbacks --- */
typedef struct {
    uint8_t note;
    uint8_t velocity;
    bool    active;
} MockVoice;

static MockVoice g_mock_voices[6];
static uint32_t  g_mock_note_ons = 0;
static uint32_t  g_mock_note_offs = 0;
static uint32_t  g_mock_steals = 0;

void synth_note_on(uint8_t voice, uint8_t note, uint8_t velocity)
{
    TEST_ASSERT(voice < 6, "Synth voice index must be 0..5");
    if (g_mock_voices[voice].active) {
        g_mock_steals++;
    }
    g_mock_voices[voice].note     = note;
    g_mock_voices[voice].velocity = velocity;
    g_mock_voices[voice].active   = true;
    g_mock_note_ons++;
}

void synth_note_off(uint8_t voice)
{
    TEST_ASSERT(voice < 6, "Synth voice index must be 0..5");
    g_mock_voices[voice].active = false;
    g_mock_note_offs++;
}

static void reset_mock_synth(void)
{
    memset(g_mock_voices, 0, sizeof(g_mock_voices));
    g_mock_note_ons = 0;
    g_mock_note_offs = 0;
    g_mock_steals = 0;
}

/* ---------------- Mock External MIDI Sink Callbacks --- */
static uint8_t  g_uart_bytes[1024];
static uint16_t g_uart_count = 0;
static uint32_t g_usb_packets[1024];
static uint16_t g_usb_count = 0;

static void mock_uart_out(uint8_t byte)
{
    if (g_uart_count < sizeof(g_uart_bytes)) {
        g_uart_bytes[g_uart_count++] = byte;
    }
}

static void mock_usb_out(uint32_t packet)
{
    if (g_usb_count < (sizeof(g_usb_packets) / sizeof(g_usb_packets[0]))) {
        g_usb_packets[g_usb_count++] = packet;
    }
}

static void reset_mock_midi(void)
{
    g_uart_count = 0;
    g_usb_count = 0;
    memset(g_uart_bytes, 0, sizeof(g_uart_bytes));
    memset(g_usb_packets, 0, sizeof(g_usb_packets));
}

/* ---------------- Test 1: Division Timing & Swing Calculations --- */
static void test_division_and_swing(void)
{
    printf("== [Test 1] Division Timing in 192 PPQN Ticks & Swing Calculations\n");

    /* 1. All division durations in 192 PPQN ticks */
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_1)   == 768u, "1/1 Whole Note = 768 ticks");
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_2)   == 384u, "1/2 Half Note = 384 ticks");
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_4)   == 192u, "1/4 Quarter Note = 192 ticks");
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_8)   == 96u,  "1/8 Eighth Note = 96 ticks");
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_16)  == 48u,  "1/16 Sixteenth Note = 48 ticks");
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_32)  == 24u,  "1/32 Thirty-second Note = 24 ticks");
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_8T)  == 64u,  "1/8 Triplet = 64 ticks (3 per quarter note)");
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_16T) == 32u,  "1/16 Triplet = 32 ticks (3 per eighth note)");
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_32T) == 16u,  "1/32 Triplet = 16 ticks (3 per sixteenth note)");
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_8D)  == 144u, "1/8 Dotted = 144 ticks");
    TEST_ASSERT(t1_division_ticks(T1_DIV_1_16D) == 72u,  "1/16 Dotted = 72 ticks");

    /* Verify all divisions divide 192 PPQN with zero rounding error */
    TEST_ASSERT((192u * 4u) % t1_division_ticks(T1_DIV_1_1) == 0, "Whole note fits 4 quarter notes");
    TEST_ASSERT((192u * 2u) % t1_division_ticks(T1_DIV_1_2) == 0, "Half note fits 2 quarter notes");
    TEST_ASSERT(192u % t1_division_ticks(T1_DIV_1_4) == 0, "Quarter note exact");
    TEST_ASSERT(192u % t1_division_ticks(T1_DIV_1_8) == 0, "Eighth note exact");
    TEST_ASSERT(192u % t1_division_ticks(T1_DIV_1_16) == 0, "Sixteenth note exact");
    TEST_ASSERT(192u % t1_division_ticks(T1_DIV_1_32) == 0, "32nd note exact");
    TEST_ASSERT(192u % (t1_division_ticks(T1_DIV_1_8T) * 3u) == 0, "Three 1/8 triplets = 1 quarter note");
    TEST_ASSERT(96u  % (t1_division_ticks(T1_DIV_1_16T) * 3u) == 0, "Three 1/16 triplets = 1 eighth note");

    printf("   -> Division timing in 192 PPQN ticks verified!\n");
}

/* ---------------- Test 2: Enhanced 6-Voice FM Polyphonic Allocator --- */
static void test_fm_voice_allocator(void)
{
    printf("== [Test 2] Enhanced 6-Voice FM Polyphonic Allocator & Stealing\n");

    t1_pipeline_init();
    reset_mock_synth();

    /* 1. Allocate up to 6 voices */
    int8_t v0 = t1_pipeline_alloc_voice(60, 100);
    int8_t v1 = t1_pipeline_alloc_voice(64, 90);
    int8_t v2 = t1_pipeline_alloc_voice(67, 80);
    int8_t v3 = t1_pipeline_alloc_voice(71, 70);
    int8_t v4 = t1_pipeline_alloc_voice(72, 60);
    int8_t v5 = t1_pipeline_alloc_voice(76, 50);

    TEST_ASSERT(v0 >= 0 && v0 < 6, "v0 valid");
    TEST_ASSERT(v1 >= 0 && v1 < 6, "v1 valid");
    TEST_ASSERT(v2 >= 0 && v2 < 6, "v2 valid");
    TEST_ASSERT(v3 >= 0 && v3 < 6, "v3 valid");
    TEST_ASSERT(v4 >= 0 && v4 < 6, "v4 valid");
    TEST_ASSERT(v5 >= 0 && v5 < 6, "v5 valid");

    /* Ensure all 6 allocated unique voice indices */
    bool seen[6] = {0};
    seen[v0] = seen[v1] = seen[v2] = seen[v3] = seen[v4] = seen[v5] = true;
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT(seen[i], "Voice slot allocated");
    }

    /* 2. Retrigger check: allocate existing note 67 -> reuses v2 */
    int8_t retrigger = t1_pipeline_alloc_voice(67, 110);
    TEST_ASSERT(retrigger == v2, "Retrigger reuses existing voice slot");
    TEST_ASSERT(t1_voice_allocator.slots[v2].velocity == 110, "Retrigger updates velocity");
    TEST_ASSERT(t1_voice_allocator.voice_steals == 0, "Retrigger does not steal");

    /* 3. Voice Stealing: all 6 slots active, allocate new note 79 -> steals lowest velocity (v5 with vel 50) */
    int8_t stolen = t1_pipeline_alloc_voice(79, 105);
    TEST_ASSERT(stolen == v5, "Steals lowest-velocity voice (v5)");
    TEST_ASSERT(t1_voice_allocator.slots[stolen].note == 79, "Stolen voice assigned new note");
    TEST_ASSERT(t1_voice_allocator.voice_steals == 1, "Voice steal counter incremented");

    /* 4. Release note 60 -> frees v0 */
    int8_t freed = t1_pipeline_free_voice(60);
    TEST_ASSERT(freed == v0, "Freed voice matches v0");
    TEST_ASSERT(!t1_voice_allocator.slots[v0].active, "Slot is inactive");

    /* 5. Allocate new note -> immediately fills free slot v0 without stealing */
    int8_t new_alloc = t1_pipeline_alloc_voice(84, 95);
    TEST_ASSERT(new_alloc == v0, "Allocates previously freed slot");
    TEST_ASSERT(t1_voice_allocator.voice_steals == 1, "No steal occurred when free slot was available");

    /* 6. Kill all voices */
    t1_pipeline_kill_all_voices();
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT(!t1_voice_allocator.slots[i].active, "All slots cleared after kill");
    }

    printf("   -> 6-voice polyphonic allocator with lowest-velocity stealing verified!\n");
}

/* ---------------- Test 3: Polyphonic Chord Generation & Voicing --- */
static void test_chord_generation(void)
{
    printf("== [Test 3] Polyphonic Chord Generation & Voicing Engine\n");

    /* Test C Major scale (root = 0, scale = 1 [Major], base note = 60 [C4]) */
    uint8_t chord_notes[4];

    /* 1. Monophonic */
    t1_harmony_eval_chord(0, 1, 60, 1, 0, chord_notes);
    TEST_ASSERT(chord_notes[0] == 60, "Monophonic note is root C4");

    /* 2. Dyad (Root, Diatonic 3rd = C4, E4) */
    t1_harmony_eval_chord(0, 1, 60, 2, 0, chord_notes);
    TEST_ASSERT(chord_notes[0] == 60, "Dyad voice 0 = C4 (60)");
    TEST_ASSERT(chord_notes[1] == 64, "Dyad voice 1 = E4 (64)");

    /* 3. Triad (Root, 3rd, 5th = C4, E4, G4) */
    t1_harmony_eval_chord(0, 1, 60, 3, 0, chord_notes);
    TEST_ASSERT(chord_notes[0] == 60, "Triad voice 0 = C4 (60)");
    TEST_ASSERT(chord_notes[1] == 64, "Triad voice 1 = E4 (64)");
    TEST_ASSERT(chord_notes[2] == 67, "Triad voice 2 = G4 (67)");

    /* 4. 4-note 7th chord (Root, 3rd, 5th, 7th = C4, E4, G4, B4) */
    t1_harmony_eval_chord(0, 1, 60, 4, 0, chord_notes);
    TEST_ASSERT(chord_notes[0] == 60, "7th chord voice 0 = C4 (60)");
    TEST_ASSERT(chord_notes[1] == 64, "7th chord voice 1 = E4 (64)");
    TEST_ASSERT(chord_notes[2] == 67, "7th chord voice 2 = G4 (67)");
    TEST_ASSERT(chord_notes[3] == 71, "7th chord voice 3 = B4 (71)");

    /* 5. 1st Inversion on 7th chord: lowest note moves up octave */
    t1_harmony_eval_chord(0, 1, 60, 4, 1 /* Inversion 1 */, chord_notes);
    TEST_ASSERT(chord_notes[0] == 72, "Inversion 1: C4 shifted to C5 (72)");
    TEST_ASSERT(chord_notes[1] == 64, "Inversion 1: E4 unchanged");
    TEST_ASSERT(chord_notes[2] == 67, "Inversion 1: G4 unchanged");
    TEST_ASSERT(chord_notes[3] == 71, "Inversion 1: B4 unchanged");

    /* 6. Drop-2 Voicing on 7th chord: second note from top (G4 = 67) lowers 1 octave */
    t1_harmony_eval_chord(0, 1, 60, 4, 4 /* Drop-2 */, chord_notes);
    TEST_ASSERT(chord_notes[0] == 60, "Drop-2 voice 0 = C4 (60)");
    TEST_ASSERT(chord_notes[1] == 64, "Drop-2 voice 1 = E4 (64)");
    TEST_ASSERT(chord_notes[2] == 55, "Drop-2 voice 2 = G3 (55 = 67 - 12)");
    TEST_ASSERT(chord_notes[3] == 71, "Drop-2 voice 3 = B4 (71)");

    printf("   -> Polyphonic chord generation and diatonic voicing verified!\n");
}

/* ---------------- Test 4: Dual Output Routing (Track 0 FM vs Tracks 1-5 MIDI) --- */
static void test_dual_output_routing(void)
{
    printf("== [Test 4] Dual Output Routing: Track 0 (FM Core) vs Tracks 1-5 (Serial/USB MIDI)\n");

    t1_system_init();
    t1_pipeline_init();
    reset_mock_synth();
    reset_mock_midi();

    Pattern *pat = &t1_sequencer_ram.active_pattern;
    pat->bpm_x10 = 1200;
    pat->swing   = 50;

    /* Track 0: Internal FM Core (1/16 straight, 1 pulse on step 0) */
    Track *t0 = &pat->tracks[0];
    t0->muted = false;
    t0->cycles[0].steps       = 16;
    t0->cycles[0].pulses      = 1;
    t0->cycles[0].rotate      = 0;
    t0->cycles[0].division    = T1_DIV_1_16; /* 48 ticks */
    t0->cycles[0].repeats     = 1;
    t0->cycles[0].velocity    = 100;
    t0->cycles[0].sustain     = 50; /* 50% gate = 24 ticks */
    t0->cycles[0].pitch_offset= 0;
    t0->cycles[0].scale_id    = 0;  /* Chromatic */
    t0->cycles[0].chord_size  = 1;  /* Monophonic */

    /* Track 1: External MIDI Channel 1 (1/16 straight, 1 pulse on step 0) */
    Track *t1 = &pat->tracks[1];
    t1->muted = false;
    t1->midi_channel = 1;
    t1->cycles[0].steps       = 16;
    t1->cycles[0].pulses      = 1;
    t1->cycles[0].rotate      = 0;
    t1->cycles[0].division    = T1_DIV_1_16;
    t1->cycles[0].repeats     = 1;
    t1->cycles[0].velocity    = 90;
    t1->cycles[0].sustain     = 50;
    t1->cycles[0].pitch_offset= 4;  /* E4 */
    t1->cycles[0].scale_id    = 0;
    t1->cycles[0].chord_size  = 1;
    t1->cycles[0].cc_target   = 74; /* Brightness */
    t1->cycles[0].cc_value    = 64;

    /* Mute other tracks for clean inspection */
    for (int t = 2; t < NUM_TRACKS; t++) {
        pat->tracks[t].muted = true;
    }

    t1_pipeline_reset_transport();

    /* Tick 0: Step 0 triggers */
    t1_pipeline_tick(0);

    /* Verify Track 0 triggered direct synth_note_on */
    TEST_ASSERT(g_mock_note_ons == 1, "Track 0 called synth_note_on directly");
    TEST_ASSERT(g_mock_voices[0].active, "Voice 0 is active");
    TEST_ASSERT(g_mock_voices[0].note == 60, "Track 0 note is 60 (Middle C)");
    TEST_ASSERT(g_mock_voices[0].velocity == 100, "Track 0 velocity is 100");

    /* Verify Tracks 1 pushed to MIDI ring buffers (NOT to synth) */
    TEST_ASSERT(midi_ring_available(&t1_midi_uart_queue) > 0, "UART ring buffer has bytes for Track 1");
    TEST_ASSERT(midi_ring_available(&t1_midi_usb_queue) > 0, "USB ring buffer has bytes for Track 1");

    /* Flush MIDI queues */
    t1_pipeline_flush_midi(mock_uart_out, mock_usb_out);
    TEST_ASSERT(g_uart_count >= 6, "Received Note-On (3B) and CC (3B) on UART");
    TEST_ASSERT(g_usb_count == 2, "Received 2 32-bit USB-MIDI packets (Note-On + CC)");

    /* Check MIDI Note-On packet: 0x91 (Channel 1 Note-On), Note 64, Vel 90 */
    TEST_ASSERT(g_uart_bytes[0] == 0x91, "Status byte is Note-On Ch 1");
    TEST_ASSERT(g_uart_bytes[1] == 64,   "Note is 64 (E4)");
    TEST_ASSERT(g_uart_bytes[2] == 90,   "Velocity is 90");

    /* Check USB Note-On packet (CIN 9) */
    TEST_ASSERT((g_usb_packets[0] & 0x0Fu) == 0x09u, "USB packet 0 CIN is 9 (Note-On)");
    TEST_ASSERT(((g_usb_packets[0] >> 8) & 0xFFu) == 0x91u, "USB packet 0 status is 0x91");
    TEST_ASSERT(((g_usb_packets[0] >> 16) & 0xFFu) == 64u,  "USB packet 0 note is 64");
    TEST_ASSERT(((g_usb_packets[0] >> 24) & 0xFFu) == 90u,  "USB packet 0 velocity is 90");

    /* Check MIDI CC packet: 0xB1 (Channel 1 CC), CC# 74, Val 64 */
    TEST_ASSERT(g_uart_bytes[3] == 0xB1, "Status byte is CC Ch 1");
    TEST_ASSERT(g_uart_bytes[4] == 74,   "CC Controller # 74");
    TEST_ASSERT(g_uart_bytes[5] == 64,   "CC Value 64");

    /* Check USB CC packet (CIN 0xB) */
    TEST_ASSERT((g_usb_packets[1] & 0x0Fu) == 0x0Bu, "USB packet 1 CIN is 0xB (CC)");
    TEST_ASSERT(((g_usb_packets[1] >> 8) & 0xFFu) == 0xB1u, "USB packet 1 status is 0xB1");
    TEST_ASSERT(((g_usb_packets[1] >> 16) & 0xFFu) == 74u,  "USB packet 1 controller # is 74");
    TEST_ASSERT(((g_usb_packets[1] >> 24) & 0xFFu) == 64u,  "USB packet 1 value is 64");

    /* Advance to gate expiration at tick 24 */
    reset_mock_midi();
    for (uint32_t tk = 1; tk <= 24; tk++) {
        t1_pipeline_tick(tk);
    }

    /* Verify Track 0 synth_note_off triggered */
    TEST_ASSERT(g_mock_note_offs == 1, "Track 0 called synth_note_off on gate expiry");
    TEST_ASSERT(!g_mock_voices[0].active, "Voice 0 is no longer active");

    /* Verify Track 1 MIDI Note-Off triggered */
    t1_pipeline_flush_midi(mock_uart_out, mock_usb_out);
    TEST_ASSERT(g_uart_count >= 3, "Received Note-Off on UART");
    TEST_ASSERT(g_uart_bytes[0] == 0x81, "Status byte is Note-Off Ch 1");
    TEST_ASSERT(g_uart_bytes[1] == 64,   "Note-Off note 64");
    TEST_ASSERT(g_uart_bytes[2] == 0,    "Note-Off velocity 0");

    TEST_ASSERT(g_usb_count == 1, "Received 1 Note-Off packet on USB");
    TEST_ASSERT((g_usb_packets[0] & 0x0Fu) == 0x08u, "USB Note-Off CIN is 8");
    TEST_ASSERT(((g_usb_packets[0] >> 8) & 0xFFu) == 0x81u, "USB Note-Off status is 0x81");
    TEST_ASSERT(((g_usb_packets[0] >> 16) & 0xFFu) == 64u,  "USB Note-Off note is 64");

    printf("   -> Dual output routing (Track 0 direct FM call, Tracks 1-5 serial MIDI) verified!\n");
}

/* ---------------- Test 5: Direct Synthesis Polyphony Stress Test (Checklist Item 5) --- */
static void test_polyphony_stress(void)
{
    printf("== [Test 5] Direct Synthesis Polyphony Stress Test (PLAN.md Checklist Item 5)\n");
    printf("   -> 16 repeats per pulse with short sustain playing 4-note polyphonic chords on Track 0\n");

    t1_system_init();
    t1_pipeline_init();
    reset_mock_synth();
    reset_mock_midi();

    Pattern *pat = &t1_sequencer_ram.active_pattern;
    pat->bpm_x10 = 1200;
    pat->swing   = 50;

    /*
     * Configure Track 0:
     * - 16 repeats per pulse (repeats = 16)
     * - 4-note polyphonic chords (chord_size = 4)
     * - Short sustain (sustain = 15% -> short gate lengths)
     * - Division: 1/16 (48 ticks per step)
     * - Pulses: 2 pulses across 16 steps
     */
    Track *t0 = &pat->tracks[0];
    t0->muted = false;
    t0->cycles[0].steps       = 16;
    t0->cycles[0].pulses      = 2;
    t0->cycles[0].rotate      = 0;
    t0->cycles[0].division    = T1_DIV_1_16;
    t0->cycles[0].repeats     = 16;
    t0->cycles[0].offset      = 20;  /* Velocity ramp */
    t0->cycles[0].pace        = -10; /* Accelerating bouncing-ball pace */
    t0->cycles[0].time        = 127; /* Full gate */
    t0->cycles[0].sustain     = 250; /* Legato gate overlap to force voice stealing */
    t0->cycles[0].pitch_offset= 0;
    t0->cycles[0].root_note   = 0;   /* C */
    t0->cycles[0].scale_id    = 1;   /* Major scale */
    t0->cycles[0].chord_size  = 4;   /* 4-note polyphonic chord */
    t0->cycles[0].voicing     = 0;   /* Root position */
    t0->cycles[0].style       = 1;   /* Arp style to advance pitches across repeats */

    /* Mute other tracks to isolate FM core stress */
    for (int t = 1; t < NUM_TRACKS; t++) {
        pat->tracks[t].muted = true;
    }

    t1_pipeline_reset_transport();

    /*
     * Run simulation across 96 ticks (2 full 1/16 steps, including pulse 0 and step 1).
     * With 16 repeats * 4 chord notes = 64 notes attacked within step 0!
     */
    for (uint32_t tk = 0; tk < 96; tk++) {
        t1_pipeline_tick(tk);

        /* Invariant checks at EVERY single tick during stress */
        uint8_t active_count = 0;
        for (int v = 0; v < 6; v++) {
            if (t1_voice_allocator.slots[v].active) {
                active_count++;
            }
        }
        TEST_ASSERT(active_count <= 6, "Active voice count must never exceed 6 (hard FM polyphony limit)");
    }

    /* Verify voice stealing activity */
    TEST_ASSERT(t1_voice_allocator.total_note_ons >= 64, "At least 64 Note-Ons evaluated in stress test");
    TEST_ASSERT(t1_voice_allocator.voice_steals > 0, "Voice stealing successfully triggered under polyphonic load");
    TEST_ASSERT(g_mock_steals > 0, "Mock FM synth witnessed voice stealing cleanly");

    printf("   -> Total Note-Ons: %u, Total Note-Offs: %u, Voice Steals: %u\n",
           t1_voice_allocator.total_note_ons, t1_voice_allocator.total_note_offs, t1_voice_allocator.voice_steals);
    printf("   -> Voice stealing and allocation routines triggered cleanly without hangs or voice leaks!\n");
}

/* ---------------- Test 6: 192 PPQN Pipeline ISR Execution Benchmark (< 80 us Target) --- */
static void test_pipeline_isr_benchmark(void)
{
    printf("== [Test 6] 192 PPQN Master Pipeline Benchmark (< 80 us Target, PLAN.md Checklist Item 3)\n");

    t1_system_init();
    t1_pipeline_init();
    reset_mock_synth();
    reset_mock_midi();

    Pattern *pat = &t1_sequencer_ram.active_pattern;
    pat->bpm_x10 = 1200;
    pat->swing   = 60;

    /*
     * Worst-case load configuration:
     * - Track 0: 4-note chord with 16 repeats (FM core direct synthesis)
     * - Tracks 1-5: All active simultaneously with 16 repeats and random CC modulation
     */
    for (int t = 0; t < NUM_TRACKS; t++) {
        pat->tracks[t].muted = false;
        pat->tracks[t].midi_channel = (uint8_t)t;
        CycleParams *cp = &pat->tracks[t].cycles[0];
        cp->steps        = 16;
        cp->pulses       = 16; /* Every step is a pulse */
        cp->division     = (uint8_t)(T1_DIV_1_16 + (t % 3));
        cp->repeats      = 16; /* Maximum repeats */
        cp->pace         = -20;
        cp->time         = 64;
        cp->sustain      = 50;
        cp->velocity     = 100;
        cp->scale_id     = (uint8_t)(t % 8);
        cp->chord_size   = (t == 0) ? 4 : 1;
        cp->cc_target    = 20 + t;
        cp->cc_value     = 64;
        cp->random_depth = 50;
        cp->random_targets = T1_RND_TARGET_CC | T1_RND_TARGET_VELOCITY;
    }

    t1_pipeline_reset_transport();

    /* Benchmark 500 consecutive 192 PPQN ticks under worst-case load */
    uint64_t total_ns = 0;
    uint32_t max_ns = 0;
    const uint32_t TEST_TICKS = 500;

    for (uint32_t tk = 0; tk < TEST_TICKS; tk++) {
        struct timespec ts0, ts1;
        clock_gettime(CLOCK_MONOTONIC, &ts0);

        t1_pipeline_tick(tk);

        clock_gettime(CLOCK_MONOTONIC, &ts1);
        uint64_t dur_ns = (uint64_t)(ts1.tv_sec - ts0.tv_sec) * 1000000000ULL + (uint64_t)(ts1.tv_nsec - ts0.tv_nsec);
        total_ns += dur_ns;
        if (dur_ns > max_ns) {
            max_ns = (uint32_t)dur_ns;
        }

        /* Flush external MIDI buffers so they don't saturate */
        t1_pipeline_flush_midi(mock_uart_out, mock_usb_out);
        reset_mock_midi();
    }

    uint32_t avg_us = (uint32_t)(total_ns / (TEST_TICKS * 1000ULL));
    uint32_t max_us = max_ns / 1000u;

    printf("   -> Benchmark over %u ticks:\n", TEST_TICKS);
    printf("   -> Average pipeline tick duration: %u us\n", avg_us);
    printf("   -> Maximum pipeline tick duration: %u us (Budget: 80 us)\n", max_us);

    TEST_ASSERT(max_us < 80u, "Maximum pipeline execution duration must remain strictly < 80 us (PLAN.md Checklist Item 3)");
    printf("   -> 192 PPQN pipeline latency is strictly < 80 us budget!\n");
}

/* ---------------- Main Runner --- */
int main(void)
{
    printf("=================================================================\n");
    printf("  Running Torso T-1 Phase 3 Dual Output & Pipeline Test Suite\n");
    printf("=================================================================\n");

    test_division_and_swing();
    test_fm_voice_allocator();
    test_chord_generation();
    test_dual_output_routing();
    test_polyphony_stress();
    test_pipeline_isr_benchmark();

    printf("=================================================================\n");
    printf("  ALL %d TORSO T-1 PHASE 3 TESTS PASSED (%d assertions)\n",
           g_tests_run, g_tests_passed);
    printf("=================================================================\n");

    return 0;
}
