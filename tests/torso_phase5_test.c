/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Test Suite: Torso T-1 Architecture - Phase 5 Verification */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "torso_t1.h"
#include "torso_t1_algo.h"
#include "torso_t1_pipeline.h"
#include "torso_t1_state.h"
#include "torso_t1_ui.h"

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

/* ---------------- Test 1: Track & Cycle Selection Navigation --- */
static void test_track_and_cycle_selection(void)
{
    printf("== [Test 1] Track & Cycle Selection Navigation\n");

    t1_system_init();
    t1_ui_init();

    TEST_ASSERT(t1_ui_state.selected_track == 0, "Initial track is Track 0 (Track 1)");
    TEST_ASSERT(t1_ui_state.selected_cycle == 0, "Initial cycle is Cycle 0 (Cycle 1)");

    /* 1. Track stepping & wrap-around */
    for (uint8_t t = 1; t < NUM_TRACKS; t++) {
        t1_ui_next_track();
        TEST_ASSERT(t1_ui_state.selected_track == t, "Track incremented");
    }
    t1_ui_next_track();
    TEST_ASSERT(t1_ui_state.selected_track == 0, "Track wraps 5 -> 0");

    t1_ui_prev_track();
    TEST_ASSERT(t1_ui_state.selected_track == 5, "Track wraps 0 -> 5");

    t1_ui_select_track(2);
    TEST_ASSERT(t1_ui_state.selected_track == 2, "Direct track selection");

    /* 2. Cycle selection */
    for (uint8_t c = 0; c < NUM_CYCLES; c++) {
        t1_ui_select_cycle(c);
        TEST_ASSERT(t1_ui_state.selected_cycle == c, "Cycle selected");
        TEST_ASSERT(t1_sequencer_ram.active_pattern.tracks[2].active_cycle == c, "Track active cycle updated");
    }

    printf("   -> Track and cycle navigation verified!\n");
}

/* ---------------- Test 2: Display Paging (4 Pages) --- */
static void test_display_paging(void)
{
    printf("== [Test 2] Display Paging (4 Pages: Rhythm, Ratchet, Harmony, Mod/CC)\n");

    t1_ui_init();

    TEST_ASSERT(t1_ui_state.current_page == T1_PAGE_RHYTHM, "Initial page is Rhythm (Page 1)");

    t1_ui_next_page();
    TEST_ASSERT(t1_ui_state.current_page == T1_PAGE_RATCHET, "Page 2 is Ratchet");

    t1_ui_next_page();
    TEST_ASSERT(t1_ui_state.current_page == T1_PAGE_HARMONY, "Page 3 is Harmony");

    t1_ui_next_page();
    TEST_ASSERT(t1_ui_state.current_page == T1_PAGE_MOD_CC, "Page 4 is Mod/CC");

    t1_ui_next_page();
    TEST_ASSERT(t1_ui_state.current_page == T1_PAGE_RHYTHM, "Page wraps 3 -> 0");

    t1_ui_prev_page();
    TEST_ASSERT(t1_ui_state.current_page == T1_PAGE_MOD_CC, "Page wraps 0 -> 3");

    t1_ui_set_page(T1_PAGE_HARMONY);
    TEST_ASSERT(t1_ui_state.current_page == T1_PAGE_HARMONY, "Direct page selection");

    printf("   -> 4-page navigation and wrap-around verified!\n");
}

/* ---------------- Test 3: Physical Encoders (Turn & Shift across all 4 Pages) --- */
static void test_encoder_parameter_bindings(void)
{
    printf("== [Test 3] Physical Encoder Turn & Shift Bindings (PLAN.md Page Map)\n");

    t1_system_init();
    t1_pipeline_init();
    t1_ui_init();

    Track *trk = &t1_sequencer_ram.active_pattern.tracks[0];
    CycleParams *cp = &trk->cycles[0];

    /* ---------------- Page 1: Rhythm --- */
    t1_ui_set_page(T1_PAGE_RHYTHM);

    /* Enc 0 Turn: Steps (1..64) */
    t1_ui_set_shift(false);
    cp->steps = 16;
    t1_ui_on_encoder_turn(0, +4);
    TEST_ASSERT(cp->steps == 20, "Enc 0 Turn increments Steps: 16 -> 20");
    t1_ui_on_encoder_turn(0, -30);
    TEST_ASSERT(cp->steps == 1, "Enc 0 Turn clamps Steps min at 1");

    /* Enc 0 Shift: Max Length / Master Override */
    t1_ui_set_shift(true);
    t1_ui_on_encoder_turn(0, +16);
    TEST_ASSERT(t1_sequencer_ram.active_pattern.master_length_override == 16, "Enc 0 Shift sets Master Length Override");

    /* Enc 1 Turn: Pulses (0..steps) */
    t1_ui_set_shift(false);
    cp->steps = 16;
    cp->pulses = 4;
    t1_ui_on_encoder_turn(1, +5);
    TEST_ASSERT(cp->pulses == 9, "Enc 1 Turn increments Pulses: 4 -> 9");
    t1_ui_on_encoder_turn(1, +20);
    TEST_ASSERT(cp->pulses == 16, "Enc 1 Turn clamps Pulses max at steps (16)");

    /* Enc 1 Shift: Rotate (-64..+64) */
    t1_ui_set_shift(true);
    cp->rotate = 0;
    t1_ui_on_encoder_turn(1, -3);
    TEST_ASSERT(cp->rotate == -3, "Enc 1 Shift sets Rotate to -3");

    /* Enc 2 Turn: Division (0..10) */
    t1_ui_set_shift(false);
    cp->division = T1_DIV_1_16;
    t1_ui_on_encoder_turn(2, +1);
    TEST_ASSERT(cp->division == T1_DIV_1_32, "Enc 2 Turn advances Division to 1/32");

    /* Enc 2 Shift: Gate Length / Sustain */
    t1_ui_set_shift(true);
    cp->sustain = 50;
    t1_ui_on_encoder_turn(2, +2);
    TEST_ASSERT(cp->sustain == 60, "Enc 2 Shift adjusts Sustain: 50 -> 60%");

    /* Enc 3 Turn: Cycle Count / Active Cycle */
    t1_ui_set_shift(false);
    trk->active_cycle = 0;
    t1_ui_on_encoder_turn(3, +2);
    TEST_ASSERT(trk->active_cycle == 2, "Enc 3 Turn switches Active Cycle: C1 -> C3");

    /* Enc 3 Shift: Step Mod */
    t1_ui_set_shift(true);
    t1_ui_select_cycle(0);
    cp->active_mask_lo = 0;
    t1_ui_on_encoder_turn(3, +1);
    TEST_ASSERT(cp->active_mask_lo != 0, "Enc 3 Shift toggles step mod mask");


    /* ---------------- Page 2: Ratchet --- */
    t1_ui_set_page(T1_PAGE_RATCHET);

    /* Enc 0 Turn: Repeats (1..16) */
    t1_ui_set_shift(false);
    cp->repeats = 1;
    t1_ui_on_encoder_turn(0, +7);
    TEST_ASSERT(cp->repeats == 8, "Enc 0 Turn sets Repeats to 8");

    /* Enc 0 Shift: Offset (-64..+63) */
    t1_ui_set_shift(true);
    cp->offset = 0;
    t1_ui_on_encoder_turn(0, +15);
    TEST_ASSERT(cp->offset == 15, "Enc 0 Shift sets velocity slope Offset to +15");

    /* Enc 1 Turn: Time (1..127) */
    t1_ui_set_shift(false);
    cp->time = 64;
    t1_ui_on_encoder_turn(1, +5);
    TEST_ASSERT(cp->time == 74, "Enc 1 Turn adjusts Time (gate density)");

    /* Enc 1 Shift: Pace (-64..+63) */
    t1_ui_set_shift(true);
    cp->pace = 0;
    t1_ui_on_encoder_turn(1, -25);
    TEST_ASSERT(cp->pace == -25, "Enc 1 Shift sets accelerating Pace to -25");

    /* Enc 2 Turn: Velocity (1..127) */
    t1_ui_set_shift(false);
    cp->velocity = 100;
    t1_ui_on_encoder_turn(2, -10);
    TEST_ASSERT(cp->velocity == 90, "Enc 2 Turn adjusts Velocity: 100 -> 90");

    /* Enc 2 Shift: Accent (0..127) */
    t1_ui_set_shift(true);
    cp->accent = 0;
    t1_ui_on_encoder_turn(2, +30);
    TEST_ASSERT(cp->accent == 30, "Enc 2 Shift sets Accent to 30");

    /* Enc 3 Turn: Probability (0..100) */
    t1_ui_set_shift(false);
    cp->probability = 100;
    t1_ui_on_encoder_turn(3, -15);
    TEST_ASSERT(cp->probability == 70, "Enc 3 Turn adjusts Probability: 100 -> 70%");

    /* Enc 3 Shift: Groove (0..15) */
    t1_ui_set_shift(true);
    cp->groove_id = 0;
    t1_ui_on_encoder_turn(3, +4);
    TEST_ASSERT(cp->groove_id == 4, "Enc 3 Shift sets Groove template to 4");

    /* ---------------- Page 3: Harmony --- */
    t1_ui_set_page(T1_PAGE_HARMONY);

    /* Enc 0 Turn: Pitch Offset (-48..+48) */
    t1_ui_set_shift(false);
    cp->pitch_offset = 0;
    t1_ui_on_encoder_turn(0, +7);
    TEST_ASSERT(cp->pitch_offset == 7, "Enc 0 Turn sets Pitch Offset to +7 st (Fifth)");

    /* Enc 0 Shift: Master Transpose (0..11) */
    t1_ui_set_shift(true);
    t1_sequencer_ram.active_pattern.master_root = 0;
    t1_ui_on_encoder_turn(0, +5);
    TEST_ASSERT(t1_sequencer_ram.active_pattern.master_root == 5, "Enc 0 Shift sets Master Root: C -> F");

    /* Enc 1 Turn: Root Note (0..11) */
    t1_ui_set_shift(false);
    cp->root_note = 0;
    t1_ui_on_encoder_turn(1, +2);
    TEST_ASSERT(cp->root_note == 2, "Enc 1 Turn sets Scale Root: C -> D");

    /* Enc 1 Shift: Scale ID (0..15) */
    t1_ui_set_shift(true);
    cp->scale_id = 0;
    t1_ui_on_encoder_turn(1, +1);
    TEST_ASSERT(cp->scale_id == 1, "Enc 1 Shift sets Scale: Major");

    /* Enc 2 Turn: Voicing (0..11) */
    t1_ui_set_shift(false);
    cp->voicing = 0;
    t1_ui_on_encoder_turn(2, +4);
    TEST_ASSERT(cp->voicing == 4, "Enc 2 Turn sets Voicing: Drop-2");

    /* Enc 2 Shift: Style (0..7) */
    t1_ui_set_shift(true);
    cp->style = 0;
    t1_ui_on_encoder_turn(2, +2);
    TEST_ASSERT(cp->style == 2, "Enc 2 Shift sets Arp Style: Up-Down");

    /* Enc 3 Turn: Phrase (0..7) */
    t1_ui_set_shift(false);
    cp->phrase = 0;
    t1_ui_on_encoder_turn(3, +1);
    TEST_ASSERT(cp->phrase == 1, "Enc 3 Turn sets Melodic Phrase: Sine");

    /* Enc 3 Shift: Range (0..127) */
    t1_ui_set_shift(true);
    cp->range = 0;
    t1_ui_on_encoder_turn(3, +24);
    TEST_ASSERT(cp->range == 24, "Enc 3 Shift sets Excursion Range: 24 st");

    /* ---------------- Page 4: Mod / CC --- */
    t1_ui_set_page(T1_PAGE_MOD_CC);

    /* Enc 0 Shift: MIDI Channel (0..15) */
    t1_ui_set_shift(true);
    trk->midi_channel = 0;
    t1_ui_on_encoder_turn(0, +3);
    TEST_ASSERT(trk->midi_channel == 3, "Enc 0 Shift sets MIDI Channel: 4");

    /* Enc 1 Turn: Random Depth (0..127) */
    t1_ui_set_shift(false);
    cp->random_depth = 0;
    t1_ui_on_encoder_turn(1, +50);
    TEST_ASSERT(cp->random_depth == 50, "Enc 1 Turn sets Random Depth to 50");

    /* Enc 2 Turn: CC Target (0..127) */
    cp->cc_target = 0;
    t1_ui_on_encoder_turn(2, +74);
    TEST_ASSERT(cp->cc_target == 74, "Enc 2 Turn sets CC Number to 74 (Filter Cutoff)");

    /* Enc 2 Shift: Delay Nudge (-12..+12 ticks) */
    t1_ui_set_shift(true);
    cp->delay_ticks = 0;
    t1_ui_on_encoder_turn(2, -4);
    TEST_ASSERT(cp->delay_ticks == -4, "Enc 2 Shift sets Micro-Delay to -4 ticks");

    /* Enc 3 Turn: CC Value (0..127) */
    t1_ui_set_shift(false);
    cp->cc_value = 0;
    t1_ui_on_encoder_turn(3, +64);
    TEST_ASSERT(cp->cc_value == 64, "Enc 3 Turn sets CC Value to 64");

    /* Enc 3 Shift: Polyphonic Voices (1..4) */
    t1_ui_set_shift(true);
    cp->chord_size = 1;
    t1_ui_on_encoder_turn(3, +3);
    TEST_ASSERT(cp->chord_size == 4, "Enc 3 Shift sets Polyphonic Chord Size to 4 Voices");

    printf("   -> All encoder turn and shift bindings across 4 pages verified!\n");
}

/* ---------------- Test 4: Screen Rendering Metadata & Formatted Cards --- */
static void test_screen_rendering_metadata(void)
{
    printf("== [Test 4] Screen Rendering Metadata & Formatted Cards\n");

    t1_system_init();
    t1_ui_init();

    Track *trk = &t1_sequencer_ram.active_pattern.tracks[0];
    CycleParams *cp = &trk->cycles[0];

    cp->steps       = 16;
    cp->pulses      = 4;
    cp->division    = T1_DIV_1_16;
    cp->repeats     = 4;
    cp->time        = 64;
    cp->velocity    = 100;
    cp->probability = 80;
    cp->pitch_offset= 12;
    cp->root_note   = 0;
    cp->scale_id    = 1;
    cp->voicing     = 4; /* Drop-2 */
    cp->phrase      = 1; /* Sine */

    /* 1. Header String Formatting */
    char hdr[64];
    t1_ui_get_header_string(hdr, sizeof(hdr));
    TEST_ASSERT(strcmp(hdr, "T1:C1 RHYTHM") == 0, "Header: T1:C1 RHYTHM");

    t1_ui_set_temp(true);
    t1_ui_get_header_string(hdr, sizeof(hdr));
    TEST_ASSERT(strcmp(hdr, "T1:C1 RHYTHM [TEMP]") == 0, "Header reflects [TEMP] tag");
    t1_ui_set_temp(false);

    t1_ui_set_global(true);
    t1_ui_get_header_string(hdr, sizeof(hdr));
    TEST_ASSERT(strcmp(hdr, "T1:C1 RHYTHM [GLOB]") == 0, "Header reflects [GLOB] tag");
    t1_ui_set_global(false);

    t1_ui_set_shift(true);
    t1_ui_get_header_string(hdr, sizeof(hdr));
    TEST_ASSERT(strcmp(hdr, "T1:C1 RHYTHM [SHFT]") == 0, "Header reflects [SHFT] tag");
    t1_ui_set_shift(false);

    /* 2. Card Info for Rhythm Page */
    char label[16], val[16];
    const char *unit = "";
    int32_t ratio = -1;

    /* Col 0: STEPS */
    t1_ui_get_card_info(0, label, val, &unit, &ratio);
    TEST_ASSERT(strcmp(label, "STEPS") == 0, "Col 0 label is STEPS");
    TEST_ASSERT(strcmp(val, "16") == 0, "Col 0 val is 16");
    TEST_ASSERT(ratio == 250, "Col 0 ratio is 250 (16/64)");

    /* Col 1: PULSES */
    t1_ui_get_card_info(1, label, val, &unit, &ratio);
    TEST_ASSERT(strcmp(label, "PULSES") == 0, "Col 1 label is PULSES");
    TEST_ASSERT(strcmp(val, "4") == 0, "Col 1 val is 4");

    /* Col 2: DIVISION */
    t1_ui_get_card_info(2, label, val, &unit, &ratio);
    TEST_ASSERT(strcmp(label, "DIVISION") == 0, "Col 2 label is DIVISION");
    TEST_ASSERT(strcmp(val, "1/16") == 0, "Col 2 val is 1/16");

    /* 3. Card Info for Harmony Page */
    t1_ui_set_page(T1_PAGE_HARMONY);

    /* Col 1 Shift: SCALE */
    t1_ui_set_shift(true);
    t1_ui_get_card_info(1, label, val, &unit, &ratio);
    TEST_ASSERT(strcmp(label, "SCALE") == 0, "Col 1 Shift label is SCALE");
    TEST_ASSERT(strcmp(val, "MAJOR") == 0, "Col 1 Shift val is MAJOR");

    /* Col 2 Normal: VOICING */
    t1_ui_set_shift(false);
    t1_ui_get_card_info(2, label, val, &unit, &ratio);
    TEST_ASSERT(strcmp(label, "VOICING") == 0, "Col 2 label is VOICING");
    TEST_ASSERT(strcmp(val, "DROP2") == 0, "Col 2 val is DROP2");

    /* Col 3 Normal: PHRASE */
    t1_ui_get_card_info(3, label, val, &unit, &ratio);
    TEST_ASSERT(strcmp(label, "PHRASE") == 0, "Col 3 label is PHRASE");
    TEST_ASSERT(strcmp(val, "SINE") == 0, "Col 3 val is SINE");

    printf("   -> Screen rendering metadata and card formatting verified!\n");
}

/* ---------------- Test 5: Parameter Lock Step Editing from UI --- */
static void test_ui_parameter_locking(void)
{
    printf("== [Test 5] Parameter Lock Step Editing from UI\n");

    t1_system_init();
    t1_ui_init();

    Track *trk = &t1_sequencer_ram.active_pattern.tracks[0];
    CycleParams *cp = &trk->cycles[0];

    /* Select step 5 for parameter locking */
    t1_ui_set_edit_step(5);
    TEST_ASSERT(t1_ui_state.edit_step == 5, "Step 5 selected for P-Lock");

    char hdr[64];
    t1_ui_get_header_string(hdr, sizeof(hdr));
    TEST_ASSERT(strstr(hdr, "[P-LOCK:6]") != NULL, "Header displays [P-LOCK:6]");

    /* 1. Lock Pitch on Harmony Page */
    t1_ui_set_page(T1_PAGE_HARMONY);
    t1_ui_on_encoder_turn(0, +7); /* Pitch +7 */

    int16_t locked_pitch = 0;
    bool has_pitch = t1_plock_get(cp, 5, 57 /* P_PITCH */, &locked_pitch);
    TEST_ASSERT(has_pitch, "P-Lock arena contains locked pitch for step 5");
    TEST_ASSERT(locked_pitch == 67, "Locked pitch is 67 (60 + 7)");

    /* 2. Lock Velocity on Ratchet Page */
    t1_ui_set_page(T1_PAGE_RATCHET);
    t1_ui_on_encoder_turn(2, +20); /* Velocity */

    int16_t locked_vel = 0;
    bool has_vel = t1_plock_get(cp, 5, 67 /* P_VEL */, &locked_vel);
    TEST_ASSERT(has_vel, "P-Lock arena contains locked velocity for step 5");
    TEST_ASSERT(locked_vel == 120, "Locked velocity is 120 (100 + 20)");

    /* Clear P-Lock mode */
    t1_ui_set_edit_step(0xFF);
    TEST_ASSERT(t1_ui_state.edit_step == 0xFF, "P-Lock edit step cleared");

    printf("   -> UI parameter locking and step override verified!\n");
}

/* ---------------- Main Runner --- */
int main(void)
{
    printf("=================================================================\n");
    printf("  Running Torso T-1 Phase 5 UI & Control Surface Test Suite\n");
    printf("=================================================================\n");

    test_track_and_cycle_selection();
    test_display_paging();
    test_encoder_parameter_bindings();
    test_screen_rendering_metadata();
    test_ui_parameter_locking();

    printf("=================================================================\n");
    printf("  ALL %d TORSO T-1 PHASE 5 TESTS PASSED (%d assertions)\n",
           g_tests_run, g_tests_passed);
    printf("=================================================================\n");

    return 0;
}
