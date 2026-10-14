/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Torso T-1 Architecture - Phase 1: Environment Baseline & Resource Budgeting */

#include "torso_t1.h"

#if !defined(FELUCCA_ID) && !defined(FM1_SYS_H)
#include <string.h>
#include <time.h>
#else
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
#endif

/* ------------------- RAM Reservation: Static Allocation Definitions --- */
/*
 * Statically allocate SequencerRAM (9,344 bytes active + snapshot patterns)
 * in fast internal SRAM to avoid heap fragmentation and latency (PLAN.md Phase 1, item 2).
 */
SequencerRAM   t1_sequencer_ram;

/*
 * Static ring buffers for external MIDI queues (256 bytes per interface)
 * Allocated statically for UART TRS-MIDI and USB-MIDI.
 */
MidiRingBuffer t1_midi_uart_queue;
MidiRingBuffer t1_midi_usb_queue;

/* Micro-Queue Dispatcher Storage */
T1MicroQueue   t1_micro_queue;

/* Performance & Timing Statistics */
T1TimerStats   t1_timer_stats;

/* ------------------ FreeRTOS Task Isolation Descriptors Table --- */
const T1TaskDescriptor t1_task_table[T1_TASK_COUNT] = {
    {
        .task_name         = "T1_Timer_ISR",
        .priority_level    = T1_PRIO_LEVEL_1_CRITICAL_TIMER_ISR,
        .freertos_priority = configT1_PRIO_TIMER_ISR,
        .stack_size_words  = 256,
        .period_us         = 2604, /* Nominal 2.604 ms @ 120 BPM */
        .role_description  = "192 PPQN Hardware Timer ISR & Master Clock Engine"
    },
    {
        .task_name         = "T1_Audio_DMA",
        .priority_level    = T1_PRIO_LEVEL_2_HIGH_AUDIO_DMA,
        .freertos_priority = configT1_PRIO_AUDIO_DMA,
        .stack_size_words  = 512,
        .period_us         = 2902, /* Half-buffer DMA frame (~2.9 ms @ 44.1 kHz) */
        .role_description  = "Audio DMA callback servicing 6-operator FM synth core"
    },
    {
        .task_name         = "T1_Micro_Queue",
        .priority_level    = T1_PRIO_LEVEL_3_MED_HIGH_MICRO_Q,
        .freertos_priority = configT1_PRIO_MICRO_QUEUE,
        .stack_size_words  = 256,
        .period_us         = 0,    /* Event-driven from 192 PPQN timer */
        .role_description  = "Sequencer Micro-Queue Dispatcher (ratchets, micro-timing, note events)"
    },
    {
        .task_name         = "T1_MIDI_Worker",
        .priority_level    = T1_PRIO_LEVEL_4_MED_MIDI_WORKER,
        .freertos_priority = configT1_PRIO_MIDI_WORKER,
        .stack_size_words  = 256,
        .period_us         = 1000, /* 1 kHz servicing rate (1 ms) */
        .role_description  = "Serial UART TRS and USB-MIDI ring buffer output flush worker"
    },
    {
        .task_name         = "T1_UI_Refresh",
        .priority_level    = T1_PRIO_LEVEL_5_LOW_UI,
        .freertos_priority = configT1_PRIO_UI,
        .stack_size_words  = 512,
        .period_us         = 16666,/* 60 Hz display refresh cadence (16.6 ms) */
        .role_description  = "UI task & OLED/LCD refresh (runs at 30-60 Hz)"
    }
};

/* ---------------------------- High-Resolution Timestamp Helper --- */
uint32_t t1_get_now_us(void)
{
#if defined(FM1_TICKS_PER_US) || defined(FM1_SYS_H) || defined(FELUCCA_ID)
    extern uint32_t fm1_ticks(void);
    return fm1_ticks() / 24u;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL);
#endif
}

/* -------------------- 192 PPQN Hardware Timer Calculations --- */
/*
 * Formula:
 * At 192 PPQN:
 * Quarter note duration in seconds = 60.0 / BPM
 * Tick duration in seconds = 60.0 / (192 * BPM)
 * Tick duration in microseconds = 60,000,000 / (192 * BPM)
 * With bpm_x10 (BPM * 10):
 * tick_period_us = 600,000,000 / (192 * bpm_x10) = 3,125,000 / bpm_x10
 *
 * At 120.0 BPM (bpm_x10 = 1200):
 * 3,125,000 / 1200 = 2604.166... us = 2.604 ms
 * At 300.0 BPM (bpm_x10 = 3000):
 * 3,125,000 / 3000 = 1041.666... us = 1.041 ms
 */
uint32_t t1_calc_tick_period_us(uint16_t bpm_x10)
{
    if (bpm_x10 < 300u)  bpm_x10 = 300u;  /* Clamp 30.0 BPM min */
    if (bpm_x10 > 3000u) bpm_x10 = 3000u; /* Clamp 300.0 BPM max */
    return (uint32_t)(3125000u / bpm_x10);
}

uint32_t t1_calc_tick_period_ns(uint16_t bpm_x10)
{
    if (bpm_x10 < 300u)  bpm_x10 = 300u;
    if (bpm_x10 > 3000u) bpm_x10 = 3000u;
    return (uint32_t)(3125000000ULL / bpm_x10);
}

void t1_timer_set_bpm(uint16_t bpm_x10)
{
    if (bpm_x10 < 300u)  bpm_x10 = 300u;
    if (bpm_x10 > 3000u) bpm_x10 = 3000u;
    t1_timer_stats.bpm_x10        = bpm_x10;
    t1_timer_stats.tick_period_us = t1_calc_tick_period_us(bpm_x10);
    t1_timer_stats.tick_period_ns = t1_calc_tick_period_ns(bpm_x10);
    t1_sequencer_ram.active_pattern.bpm_x10 = bpm_x10;
}

const T1TimerStats *t1_timer_get_stats(void)
{
    return &t1_timer_stats;
}

void t1_timer_reset_stats(void)
{
    uint16_t bpm = t1_timer_stats.bpm_x10;
    memset(&t1_timer_stats, 0, sizeof(t1_timer_stats));
    t1_timer_set_bpm(bpm ? bpm : 1200u);
}

/* ----------------- 192 PPQN Hardware Timer ISR (Priority Level 1) --- */
void t1_timer_192ppqn_isr(void)
{
    uint32_t t0 = t1_get_now_us();
    t1_timer_stats.total_ticks++;

    /* Dispatch due micro-timing and note events */
    t1_micro_queue_dispatch(t1_timer_stats.total_ticks, 0);

    /* Duration profiling and verification: strictly < 80 us */
    uint32_t duration_us = t1_get_now_us() - t0;
    t1_timer_stats.last_duration_us = duration_us;
    if (duration_us > t1_timer_stats.max_duration_us) {
        t1_timer_stats.max_duration_us = duration_us;
    }
    if (duration_us >= 80u) {
        t1_timer_stats.violations_80us++;
    }
}

/* ------------------- Static MIDI Ring Buffer Implementation --- */
void midi_ring_init(MidiRingBuffer *rb)
{
    if (!rb) return;
    rb->head = 0;
    rb->tail = 0;
    rb->dropped_bytes = 0;
    memset(rb->buffer, 0, sizeof(rb->buffer));
}

uint16_t midi_ring_available(const MidiRingBuffer *rb)
{
    if (!rb) return 0;
    uint16_t h = rb->head;
    uint16_t t = rb->tail;
    return (uint16_t)((h - t + T1_MIDI_RING_BUFFER_SIZE) % T1_MIDI_RING_BUFFER_SIZE);
}

uint16_t midi_ring_free_space(const MidiRingBuffer *rb)
{
    if (!rb) return 0;
    return (uint16_t)((T1_MIDI_RING_BUFFER_SIZE - 1u) - midi_ring_available(rb));
}

bool midi_ring_push(MidiRingBuffer *rb, uint8_t byte)
{
    if (!rb) return false;
    uint16_t next = (uint16_t)((rb->head + 1u) % T1_MIDI_RING_BUFFER_SIZE);
    if (next == rb->tail) {
        /* Buffer is full: count dropped byte and return false */
        rb->dropped_bytes++;
        return false;
    }
    rb->buffer[rb->head] = byte;
    rb->head = next;
    return true;
}

bool midi_ring_push_bytes(MidiRingBuffer *rb, const uint8_t *data, uint16_t len)
{
    if (!rb || !data) return false;
    if (midi_ring_free_space(rb) < len) {
        rb->dropped_bytes += len;
        return false;
    }
    for (uint16_t i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((rb->head + 1u) % T1_MIDI_RING_BUFFER_SIZE);
        rb->buffer[rb->head] = data[i];
        rb->head = next;
    }
    return true;
}

bool midi_ring_pop(MidiRingBuffer *rb, uint8_t *byte)
{
    if (!rb || !byte) return false;
    if (rb->head == rb->tail) {
        return false; /* Empty */
    }
    *byte = rb->buffer[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1u) % T1_MIDI_RING_BUFFER_SIZE);
    return true;
}

void midi_ring_flush(MidiRingBuffer *rb)
{
    if (!rb) return;
    rb->head = rb->tail;
}

/* ------------------- Sequencer Micro-Queue Dispatcher (Level 3) --- */
void t1_micro_queue_init(void)
{
    t1_micro_queue.head = 0;
    t1_micro_queue.tail = 0;
    t1_micro_queue.dropped_events = 0;
    memset(t1_micro_queue.events, 0, sizeof(t1_micro_queue.events));
}

bool t1_micro_queue_push(const T1MicroEvent *evt)
{
    if (!evt) return false;
    uint16_t cap = T1_MICRO_QUEUE_CAPACITY;
    uint16_t next = (uint16_t)((t1_micro_queue.head + 1u) % cap);
    if (next == t1_micro_queue.tail) {
        t1_micro_queue.dropped_events++;
        return false;
    }

    /* Insertion sort backwards from head to maintain strict chronological order */
    uint16_t curr = t1_micro_queue.head;
    while (curr != t1_micro_queue.tail) {
        uint16_t prev = (uint16_t)((curr - 1u + cap) % cap);
        const T1MicroEvent *p = &t1_micro_queue.events[prev];
        bool p_is_after = false;

        if (p->tick > evt->tick) {
            p_is_after = true;
        } else if (p->tick == evt->tick) {
            if (p->sub_tick > evt->sub_tick) {
                p_is_after = true;
            } else if (p->sub_tick == evt->sub_tick) {
                /* When timestamps match, Note-Off should precede Note-On */
                if (p->type == T1_EVENT_NOTE_ON && evt->type == T1_EVENT_NOTE_OFF) {
                    p_is_after = true;
                }
            }
        }

        if (p_is_after) {
            t1_micro_queue.events[curr] = *p;
            curr = prev;
        } else {
            break;
        }
    }

    t1_micro_queue.events[curr] = *evt;
    t1_micro_queue.head = next;
    return true;
}

/* ------------------ Internal Polyphonic FM Synth Voice Allocation --- */
#define FM_SYNTH_VOICES 6

typedef struct {
    uint8_t  note;
    uint8_t  velocity;
    bool     active;
    uint32_t age;
} FMPolyVoice;

static FMPolyVoice fm_voices[FM_SYNTH_VOICES];
static uint32_t    fm_voice_age_counter = 0;

void t1_fm_voice_alloc_reset(void)
{
    memset(fm_voices, 0, sizeof(fm_voices));
    fm_voice_age_counter = 0;
}

int8_t t1_fm_voice_find(uint8_t note)
{
    for (uint8_t i = 0; i < FM_SYNTH_VOICES; i++) {
        if (fm_voices[i].active && fm_voices[i].note == note) {
            return (int8_t)i;
        }
    }
    return -1;
}

int8_t t1_fm_voice_alloc(uint8_t note, uint8_t velocity)
{
    fm_voice_age_counter++;

    /* 1. Try to reuse a voice already playing this exact note */
    int8_t existing = t1_fm_voice_find(note);
    if (existing >= 0) {
        fm_voices[existing].velocity = velocity;
        fm_voices[existing].age      = fm_voice_age_counter;
        return existing;
    }

    /* 2. Try to find a free voice */
    for (uint8_t i = 0; i < FM_SYNTH_VOICES; i++) {
        if (!fm_voices[i].active) {
            fm_voices[i].active   = true;
            fm_voices[i].note     = note;
            fm_voices[i].velocity = velocity;
            fm_voices[i].age      = fm_voice_age_counter;
            return (int8_t)i;
        }
    }

    /* 3. Voice Stealing: Steal the oldest voice (or lowest velocity) */
    uint8_t  steal_idx = 0;
    uint32_t oldest_age = fm_voices[0].age;
    for (uint8_t i = 1; i < FM_SYNTH_VOICES; i++) {
        if (fm_voices[i].age < oldest_age) {
            oldest_age = fm_voices[i].age;
            steal_idx  = i;
        }
    }

    synth_note_off(steal_idx);
    fm_voices[steal_idx].active   = true;
    fm_voices[steal_idx].note     = note;
    fm_voices[steal_idx].velocity = velocity;
    fm_voices[steal_idx].age      = fm_voice_age_counter;
    return (int8_t)steal_idx;
}

/* Default FM voice trigger stubs (can be hooked directly to Felucca FM engine) */
__attribute__((weak)) void synth_note_on(uint8_t voice, uint8_t note, uint8_t velocity)
{
    (void)voice;
    (void)note;
    (void)velocity;
}

__attribute__((weak)) void synth_note_off(uint8_t voice)
{
    (void)voice;
}

/* Dispatch events due up to current_tick */
uint32_t t1_micro_queue_dispatch(uint32_t current_tick, uint16_t current_sub_tick)
{
    uint32_t dispatched_count = 0;

    while (t1_micro_queue.head != t1_micro_queue.tail) {
        const T1MicroEvent *evt = &t1_micro_queue.events[t1_micro_queue.tail];

        /* Check if event is due */
        if (evt->tick > current_tick) {
            break;
        }
        if (evt->tick == current_tick && evt->sub_tick > current_sub_tick) {
            break;
        }

        /* Route event based on track assignment */
        if (evt->track == 0) {
            /*
             * Track 0 (Default): Direct C-Call to FM Core
             * Avoids MIDI serialization overhead (PLAN.md Phase 3).
             */
            if (evt->type == T1_EVENT_NOTE_ON) {
                int8_t v = t1_fm_voice_alloc(evt->note, evt->velocity);
                if (v >= 0) {
                    synth_note_on((uint8_t)v, evt->note, evt->velocity);
                }
            } else if (evt->type == T1_EVENT_NOTE_OFF) {
                int8_t v = t1_fm_voice_find(evt->note);
                if (v >= 0) {
                    fm_voices[v].active = false;
                    synth_note_off((uint8_t)v);
                }
            }
        } else {
            /*
             * Tracks 1 through 5: Non-blocking FreeRTOS MIDI Queue
             * Route to static ring buffers (UART TRS-MIDI and USB-MIDI).
             */
            uint8_t ch = evt->channel & 0x0Fu;
            uint8_t msg[3];
            uint8_t msg_len = 0;

            if (evt->type == T1_EVENT_NOTE_ON) {
                msg[0] = (uint8_t)(0x90u | ch);
                msg[1] = evt->note & 0x7Fu;
                msg[2] = evt->velocity & 0x7Fu;
                msg_len = 3;
            } else if (evt->type == T1_EVENT_NOTE_OFF) {
                msg[0] = (uint8_t)(0x80u | ch);
                msg[1] = evt->note & 0x7Fu;
                msg[2] = 0;
                msg_len = 3;
            } else if (evt->type == T1_EVENT_CC) {
                msg[0] = (uint8_t)(0xB0u | ch);
                msg[1] = evt->cc_num & 0x7Fu;
                msg[2] = evt->cc_val & 0x7Fu;
                msg_len = 3;
            }

            if (msg_len > 0) {
                midi_ring_push_bytes(&t1_midi_uart_queue, msg, msg_len);
                midi_ring_push_bytes(&t1_midi_usb_queue,  msg, msg_len);
            }
        }

        t1_micro_queue.tail = (uint16_t)((t1_micro_queue.tail + 1u) % T1_MICRO_QUEUE_CAPACITY);
        dispatched_count++;
    }

    return dispatched_count;
}

/* ---------------- Serial/USB MIDI Output Worker (Level 4) --- */
uint32_t t1_midi_worker_flush_uart(void (*uart_send_byte)(uint8_t b))
{
    uint32_t count = 0;
    uint8_t byte;
    while (midi_ring_pop(&t1_midi_uart_queue, &byte)) {
        if (uart_send_byte) {
            uart_send_byte(byte);
        }
        count++;
    }
    return count;
}

uint32_t t1_midi_worker_flush_usb(void (*usb_send_packet)(uint32_t pkt))
{
    uint32_t packets_sent = 0;
    uint8_t b1, b2, b3;

    /* Read complete 3-byte MIDI channel messages */
    while (midi_ring_available(&t1_midi_usb_queue) >= 3u) {
        if (!midi_ring_pop(&t1_midi_usb_queue, &b1) ||
            !midi_ring_pop(&t1_midi_usb_queue, &b2) ||
            !midi_ring_pop(&t1_midi_usb_queue, &b3)) {
            break;
        }

        uint8_t cin = (uint8_t)(b1 >> 4);
        uint32_t usb_pkt = ((uint32_t)cin) |
                           ((uint32_t)b1 << 8) |
                           ((uint32_t)b2 << 16) |
                           ((uint32_t)b3 << 24);

        if (usb_send_packet) {
            usb_send_packet(usb_pkt);
        }
        packets_sent++;
    }

    return packets_sent;
}

void t1_midi_worker_step(void)
{
    /* Step routine called by Level 4 task */
    t1_midi_worker_flush_uart(NULL);
    t1_midi_worker_flush_usb(NULL);
}

/* ------------------- UI Task Cadence Control (Level 5) --- */
static uint32_t last_ui_render_ms = 0;

bool t1_ui_task_should_render(uint32_t now_ms)
{
    /* Enforce 30-60 Hz frame rate (at least 16 ms interval) */
    return (now_ms - last_ui_render_ms) >= 16u;
}

void t1_ui_task_did_render(uint32_t now_ms)
{
    last_ui_render_ms = now_ms;
}

/* ------------------------- System Initialization --- */
void t1_system_init(void)
{
    /* Zero active and snapshot working buffers */
    memset(&t1_sequencer_ram, 0, sizeof(t1_sequencer_ram));

    /* Initialize active pattern defaults */
    t1_sequencer_ram.active_bank_idx    = 0;
    t1_sequencer_ram.active_pattern_idx = 0;
    t1_sequencer_ram.active_pattern.bpm_x10 = 1200; /* Default 120.0 BPM */
    t1_sequencer_ram.active_pattern.swing   = 50;   /* 50% straight */

    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        Track *trk = &t1_sequencer_ram.active_pattern.tracks[t];
        trk->active_cycle = 0;
        trk->cycle_count  = 1;
        trk->midi_channel = (t == 0) ? 0 : t; /* Track 0: FM synth, 1-5: MIDI channels 1-5 */
        trk->muted        = false;
        trk->soloed       = false;

        for (uint8_t c = 0; c < NUM_CYCLES; c++) {
            CycleParams *cp = &trk->cycles[c];
            cp->steps       = 16;
            cp->pulses      = 0;
            cp->rotate      = 0;
            cp->division    = 4; /* 1/16 */
            cp->repeats     = 1;
            cp->velocity    = 100;
            cp->probability = 100;
            cp->sustain     = 50;
            memset(cp->p_lock_data, 0xFF, sizeof(cp->p_lock_data));
        }
    }

    /* Initialize snapshot buffer */
    memcpy(&t1_sequencer_ram.snapshot_pattern, &t1_sequencer_ram.active_pattern, sizeof(Pattern));

    /* Initialize Queues */
    midi_ring_init(&t1_midi_uart_queue);
    midi_ring_init(&t1_midi_usb_queue);
    t1_micro_queue_init();
    t1_fm_voice_alloc_reset();

    /* Set up 192 PPQN timer parameters */
    t1_timer_set_bpm(1200);
}
