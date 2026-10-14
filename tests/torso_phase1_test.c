/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Test Suite: Torso T-1 Architecture - Phase 1 Verification */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "torso_t1.h"

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

/* Track direct synth calls for testing */
static struct {
    uint8_t note_on_calls;
    uint8_t note_off_calls;
    uint8_t last_voice;
    uint8_t last_note;
    uint8_t last_velocity;
} g_synth_tracker;

void synth_note_on(uint8_t voice, uint8_t note, uint8_t velocity)
{
    g_synth_tracker.note_on_calls++;
    g_synth_tracker.last_voice    = voice;
    g_synth_tracker.last_note     = note;
    g_synth_tracker.last_velocity = velocity;
}

void synth_note_off(uint8_t voice)
{
    g_synth_tracker.note_off_calls++;
    g_synth_tracker.last_voice = voice;
}

/* ---------------- Test 1: Compile-time Struct Sizes & Memory Layout --- */
static void test_struct_packing_and_memory_footprint(void)
{
    printf("== [Test 1] Struct Packing & Memory Footprint Verification\n");

    /* 1. CycleParams: Exactly 128 bytes */
    TEST_ASSERT(sizeof(CycleParams) == 128, "CycleParams must be exactly 128 bytes");

    /* Verify field offsets inside CycleParams */
    TEST_ASSERT(offsetof(CycleParams, steps) == 0, "steps at offset 0");
    TEST_ASSERT(offsetof(CycleParams, pitch_offset) == 8, "pitch_offset at offset 8");
    TEST_ASSERT(offsetof(CycleParams, velocity) == 16, "velocity at offset 16");
    TEST_ASSERT(offsetof(CycleParams, random_targets) == 24, "random_targets at offset 24");
    TEST_ASSERT(offsetof(CycleParams, active_mask_hi) == 32, "active_mask_hi at offset 32");
    TEST_ASSERT(offsetof(CycleParams, p_lock_data) == 40, "p_lock_data at offset 40");
    TEST_ASSERT(sizeof(((CycleParams*)0)->p_lock_data) == 88, "p_lock_data must be 88 bytes");

    /* 2. Track: Exactly 776 bytes */
    TEST_ASSERT(sizeof(Track) == 776, "Track must be exactly 776 bytes");
    TEST_ASSERT(offsetof(Track, cycles) == 0, "cycles array at offset 0");
    TEST_ASSERT(sizeof(((Track*)0)->cycles) == 768, "6 cycles * 128 = 768 bytes");
    TEST_ASSERT(offsetof(Track, active_cycle) == 768, "active_cycle at offset 768");

    /* 3. Pattern: Exactly 4,672 bytes (~4.56 KB) */
    TEST_ASSERT(sizeof(Pattern) == 4672, "Pattern must be exactly 4,672 bytes");
    TEST_ASSERT(offsetof(Pattern, tracks) == 0, "tracks array at offset 0");
    TEST_ASSERT(sizeof(((Pattern*)0)->tracks) == 4656, "6 tracks * 776 = 4,656 bytes");
    TEST_ASSERT(offsetof(Pattern, bpm_x10) == 4656, "bpm_x10 at offset 4656");
    TEST_ASSERT(offsetof(Pattern, swing) == 4658, "swing at offset 4658");
    TEST_ASSERT(offsetof(Pattern, master_scale) == 4659, "master_scale at offset 4659");
    TEST_ASSERT(offsetof(Pattern, master_root) == 4660, "master_root at offset 4660");
    TEST_ASSERT(offsetof(Pattern, master_length_override) == 4661, "master_length_override at offset 4661");

    /* 4. Active Working RAM (Double-Buffered): Exactly 9,344 bytes of patterns */
    TEST_ASSERT(DOUBLE_BUFFER_SIZE == 9344, "Active + snapshot patterns must be 9,344 bytes");
    TEST_ASSERT(sizeof(t1_sequencer_ram.active_pattern) == 4672, "active_pattern size is 4,672");
    TEST_ASSERT(sizeof(t1_sequencer_ram.snapshot_pattern) == 4672, "snapshot_pattern size is 4,672");

    /* 5. Total Flash Storage: 168.2 KB for all 36 patterns across all 6 banks */
    size_t total_flash_patterns_bytes = TOTAL_PATTERNS * sizeof(Pattern);
    TEST_ASSERT(TOTAL_PATTERNS == 36, "Total patterns pool is 36 (6 banks x 6 patterns)");
    TEST_ASSERT(total_flash_patterns_bytes == 36 * 4672, "Flash size calculation");
    TEST_ASSERT(total_flash_patterns_bytes == 168192, "Flash storage is 168,192 bytes (~168.2 KB)");
    /* Verify fits within three standard 64 KB SPI flash sectors (3 * 65536 = 196608 bytes) */
    TEST_ASSERT(total_flash_patterns_bytes <= 3 * 65536, "Fits in three 64KB SPI flash sectors");

    printf("   -> Struct packing, field alignment, and memory footprint assertions passed!\n");
}

/* ---------------- Test 2: Static RAM Reservation & MIDI Ring Buffers --- */
static void test_static_ram_and_midi_queues(void)
{
    printf("== [Test 2] Static RAM Reservation & 256-Byte MIDI Ring Buffers\n");

    /* 1. MIDI Queue Sizing: Exactly 256 bytes per physical output interface */
    TEST_ASSERT(sizeof(t1_midi_uart_queue.buffer) == 256, "UART MIDI queue buffer must be 256 bytes");
    TEST_ASSERT(sizeof(t1_midi_usb_queue.buffer) == 256, "USB MIDI queue buffer must be 256 bytes");

    /* 2. Ring buffer operations */
    midi_ring_init(&t1_midi_uart_queue);
    TEST_ASSERT(midi_ring_available(&t1_midi_uart_queue) == 0, "Initial available is 0");
    TEST_ASSERT(midi_ring_free_space(&t1_midi_uart_queue) == 255, "Free space is 255 (capacity - 1)");

    /* Push bytes up to capacity */
    for (int i = 0; i < 255; i++) {
        bool ok = midi_ring_push(&t1_midi_uart_queue, (uint8_t)(i & 0xFF));
        TEST_ASSERT(ok, "Pushing byte must succeed");
    }
    TEST_ASSERT(midi_ring_available(&t1_midi_uart_queue) == 255, "Buffer is full with 255 bytes");
    TEST_ASSERT(midi_ring_free_space(&t1_midi_uart_queue) == 0, "Free space is now 0");

    /* Next push should drop and record dropped_bytes */
    bool ok_overflow = midi_ring_push(&t1_midi_uart_queue, 0xEE);
    TEST_ASSERT(!ok_overflow, "Pushing to full buffer must return false");
    TEST_ASSERT(t1_midi_uart_queue.dropped_bytes == 1, "Dropped byte counter must increment");

    /* Pop all bytes and verify FIFO order */
    for (int i = 0; i < 255; i++) {
        uint8_t b = 0;
        bool pop_ok = midi_ring_pop(&t1_midi_uart_queue, &b);
        TEST_ASSERT(pop_ok, "Popping byte must succeed");
        TEST_ASSERT(b == (uint8_t)(i & 0xFF), "Popped byte must match FIFO order");
    }
    TEST_ASSERT(midi_ring_available(&t1_midi_uart_queue) == 0, "Buffer is now empty");

    /* Pop on empty returns false */
    uint8_t b_empty = 0;
    TEST_ASSERT(!midi_ring_pop(&t1_midi_uart_queue, &b_empty), "Popping empty buffer returns false");

    printf("   -> Static RAM reservation and 256-byte ring buffers verified!\n");
}

/* ---------------- Test 3: 192 PPQN Hardware Timer Timing Resolution --- */
static void test_192ppqn_timer_resolution(void)
{
    printf("== [Test 3] 192 PPQN Hardware Timer Resolution Calculations\n");

    /*
     * PLAN.md specification:
     * - At 120 BPM, tick resolution is 2.604 ms
     * - At 300 BPM, resolution is 1.041 ms
     */

    /* 1. Test 120.0 BPM (bpm_x10 = 1200) */
    uint32_t us_120 = t1_calc_tick_period_us(1200);
    uint32_t ns_120 = t1_calc_tick_period_ns(1200);

    /* 60,000,000 / (192 * 120) = 2604.1666... us */
    TEST_ASSERT(us_120 == 2604, "120 BPM tick resolution must be 2604 us (2.604 ms)");
    TEST_ASSERT(ns_120 == 2604166, "120 BPM tick resolution in ns must be ~2,604,166 ns");

    /* 2. Test 300.0 BPM (bpm_x10 = 3000) */
    uint32_t us_300 = t1_calc_tick_period_us(3000);
    uint32_t ns_300 = t1_calc_tick_period_ns(3000);

    /* 60,000,000 / (192 * 300) = 1041.6666... us */
    TEST_ASSERT(us_300 == 1041, "300 BPM tick resolution must be 1041 us (1.041 ms)");
    TEST_ASSERT(ns_300 == 1041666, "300 BPM tick resolution in ns must be ~1,041,666 ns");

    /* 3. Test other BPM ranges */
    uint32_t us_60  = t1_calc_tick_period_us(600);  /* 60.0 BPM */
    TEST_ASSERT(us_60 == 5208, "60 BPM tick resolution must be 5208 us (5.208 ms)");

    uint32_t us_140 = t1_calc_tick_period_us(1400); /* 140.0 BPM */
    TEST_ASSERT(us_140 == 2232, "140 BPM tick resolution must be 2232 us");

    /* 4. Clamping boundaries */
    uint32_t us_low = t1_calc_tick_period_us(100);  /* under 30.0 BPM -> clamped to 30.0 BPM */
    uint32_t us_30  = t1_calc_tick_period_us(300);
    TEST_ASSERT(us_low == us_30, "Values below 30.0 BPM clamped to 30.0 BPM");

    uint32_t us_high = t1_calc_tick_period_us(4000); /* over 300.0 BPM -> clamped to 300.0 BPM */
    TEST_ASSERT(us_high == us_300, "Values above 300.0 BPM clamped to 300.0 BPM");

    /* 5. Verify timer setup updates stats */
    t1_timer_set_bpm(1200);
    const T1TimerStats *stats = t1_timer_get_stats();
    TEST_ASSERT(stats->bpm_x10 == 1200, "Stats BPM set to 1200");
    TEST_ASSERT(stats->tick_period_us == 2604, "Stats tick period set to 2604 us");

    printf("   -> 192 PPQN resolution matches PLAN.md specifications (2.604 ms @ 120 BPM, 1.041 ms @ 300 BPM)!\n");
}

/* ---------------- Test 4: FreeRTOS Task Isolation Hierarchy --- */
static void test_freertos_task_isolation_hierarchy(void)
{
    printf("== [Test 4] FreeRTOS Task Isolation & Priority Hierarchy\n");

    /*
     * Priority Hierarchy:
     * - Level 1 (Critical): Hardware Timer ISR at 192 PPQN
     * - Level 2 (High): Audio DMA callback servicing FM synth core
     * - Level 3 (Medium-High): Sequencer Micro-Queue Dispatcher
     * - Level 4 (Medium): Serial/USB MIDI output worker
     * - Level 5 (Low): UI task & OLED/LCD refresh (runs at 30-60 Hz)
     */
    TEST_ASSERT(T1_TASK_COUNT == 5, "Exactly 5 task isolation levels");

    /* Task 0: Critical 192 PPQN Timer ISR */
    TEST_ASSERT(t1_task_table[0].priority_level == T1_PRIO_LEVEL_1_CRITICAL_TIMER_ISR, "Task 0 is Priority Level 1");
    TEST_ASSERT(t1_task_table[0].freertos_priority == configT1_PRIO_TIMER_ISR, "Task 0 FreeRTOS priority is highest (5)");

    /* Task 1: High Audio DMA Callback */
    TEST_ASSERT(t1_task_table[1].priority_level == T1_PRIO_LEVEL_2_HIGH_AUDIO_DMA, "Task 1 is Priority Level 2");
    TEST_ASSERT(t1_task_table[1].freertos_priority == configT1_PRIO_AUDIO_DMA, "Task 1 FreeRTOS priority is 4");

    /* Task 2: Medium-High Micro-Queue Dispatcher */
    TEST_ASSERT(t1_task_table[2].priority_level == T1_PRIO_LEVEL_3_MED_HIGH_MICRO_Q, "Task 2 is Priority Level 3");
    TEST_ASSERT(t1_task_table[2].freertos_priority == configT1_PRIO_MICRO_QUEUE, "Task 2 FreeRTOS priority is 3");

    /* Task 3: Medium Serial/USB MIDI output worker */
    TEST_ASSERT(t1_task_table[3].priority_level == T1_PRIO_LEVEL_4_MED_MIDI_WORKER, "Task 3 is Priority Level 4");
    TEST_ASSERT(t1_task_table[3].freertos_priority == configT1_PRIO_MIDI_WORKER, "Task 3 FreeRTOS priority is 2");

    /* Task 4: Low UI Task & LCD Refresh */
    TEST_ASSERT(t1_task_table[4].priority_level == T1_PRIO_LEVEL_5_LOW_UI, "Task 4 is Priority Level 5");
    TEST_ASSERT(t1_task_table[4].freertos_priority == configT1_PRIO_UI, "Task 4 FreeRTOS priority is 1");

    /* Verify strictly descending relative priority ranking */
    for (int i = 0; i < T1_TASK_COUNT - 1; i++) {
        TEST_ASSERT(t1_task_table[i].priority_level < t1_task_table[i+1].priority_level,
                    "Relative priority ranking strictly increases numerically (1 Critical to 5 Low)");
        TEST_ASSERT(t1_task_table[i].freertos_priority > t1_task_table[i+1].freertos_priority,
                    "FreeRTOS scheduler priority strictly decreases numerically (5 highest to 1 lowest)");
    }

    printf("   -> FreeRTOS task isolation table and priority hierarchy verified!\n");
}

/* ---------------- Test 5: Micro-Queue Dispatcher & Dual Output Routing --- */
static void test_micro_queue_dispatcher_and_routing(void)
{
    printf("== [Test 5] Micro-Queue Dispatcher & Dual Output Routing\n");

    t1_system_init();
    memset(&g_synth_tracker, 0, sizeof(g_synth_tracker));

    /*
     * PLAN.md Phase 3 Routing:
     * - Track 0: Direct C-Call to FM Core: synth_note_on / synth_note_off
     * - Tracks 1-5: Non-blocking Queue -> External Serial/USB MIDI
     */

    /* Schedule an event for Track 0 (FM synth) at tick 10 */
    T1MicroEvent ev0 = {
        .tick = 10,
        .sub_tick = 0,
        .track = 0,
        .type = T1_EVENT_NOTE_ON,
        .note = 60, /* Middle C */
        .velocity = 100,
        .channel = 0
    };
    bool push0_ok = t1_micro_queue_push(&ev0);
    TEST_ASSERT(push0_ok, "Pushing Track 0 event must succeed");

    /* Schedule an event for Track 1 (External MIDI) at tick 10 */
    T1MicroEvent ev1 = {
        .tick = 10,
        .sub_tick = 0,
        .track = 1,
        .type = T1_EVENT_NOTE_ON,
        .note = 64, /* E4 */
        .velocity = 90,
        .channel = 1
    };
    bool push1_ok = t1_micro_queue_push(&ev1);
    TEST_ASSERT(push1_ok, "Pushing Track 1 event must succeed");

    /* Dispatch at tick 9: events should not fire yet */
    uint32_t dispatched_early = t1_micro_queue_dispatch(9, 0);
    TEST_ASSERT(dispatched_early == 0, "No events should fire before due tick");
    TEST_ASSERT(g_synth_tracker.note_on_calls == 0, "No direct synth call yet");
    TEST_ASSERT(midi_ring_available(&t1_midi_uart_queue) == 0, "No UART MIDI bytes yet");

    /* Dispatch at tick 10: both events should fire */
    uint32_t dispatched_due = t1_micro_queue_dispatch(10, 0);
    TEST_ASSERT(dispatched_due == 2, "Both events must be dispatched at tick 10");

    /* Verify Track 0 direct FM call */
    TEST_ASSERT(g_synth_tracker.note_on_calls == 1, "Track 0 invoked synth_note_on directly");
    TEST_ASSERT(g_synth_tracker.last_note == 60, "Synth received note 60");
    TEST_ASSERT(g_synth_tracker.last_velocity == 100, "Synth received velocity 100");

    /* Verify Track 1 external MIDI queue */
    TEST_ASSERT(midi_ring_available(&t1_midi_uart_queue) == 3, "Track 1 routed to UART MIDI queue (3 bytes)");
    TEST_ASSERT(midi_ring_available(&t1_midi_usb_queue) == 3, "Track 1 routed to USB MIDI queue (3 bytes)");

    uint8_t m0, m1, m2;
    midi_ring_pop(&t1_midi_uart_queue, &m0);
    midi_ring_pop(&t1_midi_uart_queue, &m1);
    midi_ring_pop(&t1_midi_uart_queue, &m2);
    TEST_ASSERT(m0 == 0x91, "MIDI Note-On channel 1 status (0x90 | 1 = 0x91)");
    TEST_ASSERT(m1 == 64, "MIDI Note 64");
    TEST_ASSERT(m2 == 90, "MIDI Velocity 90");

    /* Now test Note-Off for Track 0 */
    T1MicroEvent ev0_off = {
        .tick = 12,
        .sub_tick = 0,
        .track = 0,
        .type = T1_EVENT_NOTE_OFF,
        .note = 60,
        .velocity = 0,
        .channel = 0
    };
    t1_micro_queue_push(&ev0_off);
    t1_micro_queue_dispatch(12, 0);
    TEST_ASSERT(g_synth_tracker.note_off_calls == 1, "Track 0 note-off invoked synth_note_off directly");

    printf("   -> Micro-queue dispatcher and dual output routing verified!\n");
}

/* ---------------- Test 6: 6-Voice FM Polyphonic Voice Allocation & Stealing --- */
static void test_fm_voice_polyphony_and_stealing(void)
{
    printf("== [Test 6] 6-Voice Polyphony Allocation & Voice Stealing (FM Core)\n");

    t1_fm_voice_alloc_reset();
    memset(&g_synth_tracker, 0, sizeof(g_synth_tracker));

    /* Allocate 6 distinct voices (voices 0 to 5) */
    for (uint8_t note = 60; note < 66; note++) {
        int8_t v = t1_fm_voice_alloc(note, 100);
        TEST_ASSERT(v >= 0 && v < 6, "Allocated voice must be in 0..5 range");
    }

    /* All 6 voices should be in use */
    for (uint8_t note = 60; note < 66; note++) {
        int8_t v = t1_fm_voice_find(note);
        TEST_ASSERT(v >= 0, "Note should have an active voice");
    }

    /* Allocate a 7th note (e.g. note 66): should trigger voice stealing on oldest voice (note 60) */
    int8_t stolen_voice = t1_fm_voice_alloc(66, 110);
    TEST_ASSERT(stolen_voice == 0, "Oldest voice (voice 0) must be stolen");
    TEST_ASSERT(t1_fm_voice_find(60) == -1, "Old note 60 is no longer active");
    TEST_ASSERT(t1_fm_voice_find(66) == stolen_voice, "New note 66 occupies stolen voice");
    TEST_ASSERT(g_synth_tracker.note_off_calls == 1, "Stealing triggers clean note-off first");

    printf("   -> 6-voice polyphonic allocation and voice stealing verified!\n");
}

/* ---------------- Test 7: 192 PPQN Timer ISR Jitter & Execution Duration --- */
static void test_timer_isr_execution_benchmark(void)
{
    printf("== [Test 7] 192 PPQN Timer ISR Duration Benchmark (< 80 us Target)\n");

    t1_system_init();
    t1_timer_reset_stats();

    /*
     * Concrete Verification Checklist Item 3:
     * Confirm ISR execution time remains strictly < 80 us.
     * Populate micro-queue with worst-case load: simultaneous ratchets & events.
     */
    for (int i = 0; i < 20; i++) {
        T1MicroEvent ev = {
            .tick = t1_timer_stats.total_ticks + 1,
            .sub_tick = 0,
            .track = (uint8_t)(i % 6),
            .type = T1_EVENT_NOTE_ON,
            .note = (uint8_t)(60 + i),
            .velocity = 100,
            .channel = (uint8_t)(i % 6)
        };
        t1_micro_queue_push(&ev);
    }

    /* Run ISR */
    t1_timer_192ppqn_isr();

    const T1TimerStats *stats = t1_timer_get_stats();
    TEST_ASSERT(stats->total_ticks == 1, "Total ticks incremented");
    TEST_ASSERT(stats->violations_80us == 0, "ISR execution duration must remain strictly < 80 us");

    printf("   -> ISR duration: %u us (max: %u us, budget: 80 us, violations: %u)\n",
           stats->last_duration_us, stats->max_duration_us, stats->violations_80us);
    printf("   -> Timer ISR benchmark passed!\n");
}

/* ---------------- Test 8: UI Task 30-60 Hz Cadence Governor --- */
static void test_ui_task_cadence_governor(void)
{
    printf("== [Test 8] UI Task Cadence Governor (30-60 Hz Refresh)\n");

    uint32_t t_now = 1000;
    TEST_ASSERT(t1_ui_task_should_render(t_now), "Initial render should be allowed");
    t1_ui_task_did_render(t_now);

    /* 5 ms later: within 60 Hz frame, should not re-render */
    TEST_ASSERT(!t1_ui_task_should_render(t_now + 5), "Should throttle within 16 ms");
    TEST_ASSERT(!t1_ui_task_should_render(t_now + 15), "Should throttle at 15 ms");

    /* At 16 ms: allowed (60 Hz cadence) */
    TEST_ASSERT(t1_ui_task_should_render(t_now + 16), "Allowed at 16 ms (~60 Hz)");
    t1_ui_task_did_render(t_now + 16);

    /* At 33 ms: allowed (30 Hz cadence) */
    TEST_ASSERT(t1_ui_task_should_render(t_now + 16 + 33), "Allowed at 33 ms (~30 Hz)");

    printf("   -> UI task 30-60 Hz cadence governor verified!\n");
}

/* ------------------------------------------------ Main Test Runner --- */
int main(void)
{
    printf("=================================================================\n");
    printf("  Running Torso T-1 Phase 1 Verification Suite\n");
    printf("=================================================================\n");

    test_struct_packing_and_memory_footprint();
    test_static_ram_and_midi_queues();
    test_192ppqn_timer_resolution();
    test_freertos_task_isolation_hierarchy();
    test_micro_queue_dispatcher_and_routing();
    test_fm_voice_polyphony_and_stealing();
    test_timer_isr_execution_benchmark();
    test_ui_task_cadence_governor();

    printf("=================================================================\n");
    printf("  ALL %d TORSO T-1 PHASE 1 TESTS PASSED (%d assertions)\n", g_tests_passed, g_tests_run);
    printf("=================================================================\n");
    return 0;
}
