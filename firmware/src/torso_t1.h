/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Torso T-1 Architecture - Phase 1: Environment Baseline & Resource Budgeting */

#ifndef TORSO_T1_H
#define TORSO_T1_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------ Architecture Dimensions --- */
#define NUM_TRACKS        6
#define NUM_CYCLES        6
#define PATTERNS_PER_BANK 6
#define NUM_BANKS         6
#define TOTAL_PATTERNS    (NUM_BANKS * PATTERNS_PER_BANK) /* 36 patterns total */
#define PPQN_RATE         192                             /* 192 Pulses Per Quarter Note */

/* -------------------------------- FreeRTOS Task Isolation & Priority Levels --- */
/*
 * Priority Hierarchy as defined in PLAN.md Phase 1:
 * - Priority Level 1 (Critical): Hardware Timer ISR at 192 PPQN (tick resolution 2.604 ms @ 120 BPM, 1.041 ms @ 300 BPM)
 * - Priority Level 2 (High): Audio DMA callback servicing the 6-operator FM synth core (MicroDexed/Dexed audio frames)
 * - Priority Level 3 (Medium-High): Sequencer Micro-Queue Dispatcher (note-ons, sub-tick ratchets, micro-delay, note-offs)
 * - Priority Level 4 (Medium): Serial/USB MIDI output worker (flushes UART and TinyUSB ring buffers)
 * - Priority Level 5 (Low): UI task & OLED/LCD refresh (runs at 30–60 Hz)
 */

/* Relative Priority Ranking (1 = highest/critical, 5 = lowest) */
#define T1_PRIO_LEVEL_1_CRITICAL_TIMER_ISR 1
#define T1_PRIO_LEVEL_2_HIGH_AUDIO_DMA     2
#define T1_PRIO_LEVEL_3_MED_HIGH_MICRO_Q   3
#define T1_PRIO_LEVEL_4_MED_MIDI_WORKER    4
#define T1_PRIO_LEVEL_5_LOW_UI             5

/* FreeRTOS Numeric Priority Values (higher number = higher execution priority in FreeRTOS scheduler) */
#ifndef tskIDLE_PRIORITY
#define tskIDLE_PRIORITY ((uint32_t)0)
#endif

#define configT1_PRIO_UI          (tskIDLE_PRIORITY + 1) /* Priority 1: UI task (30-60 Hz) */
#define configT1_PRIO_MIDI_WORKER (tskIDLE_PRIORITY + 2) /* Priority 2: Serial/USB MIDI output worker */
#define configT1_PRIO_MICRO_QUEUE (tskIDLE_PRIORITY + 3) /* Priority 3: Micro-Queue Dispatcher */
#define configT1_PRIO_AUDIO_DMA   (tskIDLE_PRIORITY + 4) /* Priority 4: Audio DMA callback (FM synth core) */
#define configT1_PRIO_TIMER_ISR   (tskIDLE_PRIORITY + 5) /* Priority 5: 192 PPQN Hardware Timer ISR */

/* --------------------------------- Data Model & Exact Struct Packing (Phase 2) --- */

/* 1. Cycle Parameter Block: Exactly 128 bytes */
typedef struct __attribute__((packed)) {
    /* Rhythm & Timing (8 bytes) */
    uint8_t steps;          /* 1 to 64 */
    uint8_t pulses;         /* 0 to steps */
    int8_t  rotate;         /* -64 to +64 */
    uint8_t division;       /* 1/1, 1/2, 1/4, 1/8, 1/16, 1/32 (triplets/dotted) */
    uint8_t repeats;        /* 1 to 16 */
    int8_t  offset;         /* Velocity slope (-64 to +63) */
    uint8_t time;           /* Ratchet gate density (1 to 127) */
    int8_t  pace;           /* Log/Exp curve shape (-64 to +63) */

    /* Pitch & Voicing (8 bytes) */
    int8_t  pitch_offset;   /* Chromatic semitone shift (-48 to +48) */
    uint8_t scale_id;       /* Scale definition index (0 to 15) */
    uint8_t root_note;      /* 0 (C) to 11 (B) */
    uint8_t voicing;        /* Inversion / octave displacement style */
    uint8_t style;          /* Arp direction mode */
    uint8_t phrase;         /* Melodic contour wave shape */
    uint8_t range;          /* Melodic excursion span */
    union {
        uint8_t reserved_pitch;
        uint8_t chord_size;     /* Polyphonic chord notes: 1 to 4 (default 1 if 0) */
    };
    /* Dynamics & Stochastic (8 bytes) */
    uint8_t velocity;       /* Base velocity (1 to 127) */
    uint8_t probability;    /* Trigger chance (0 to 100%) */
    uint8_t sustain;        /* Gate length (1% to >100% legato) */
    uint8_t accent;         /* Accent velocity delta */
    uint8_t groove_id;      /* Micro-timing template (0 to 15) */
    int8_t  delay_ticks;    /* Track micro-nudge (-12 to +12 ticks) */
    uint8_t random_depth;   /* Modulation depth (0 to 127) */
    uint8_t random_rate;    /* LFSR step interval (0 to 15) */

    /* Modulation Targets & Bitmasks (8 bytes) */
    uint16_t random_targets;/* Parameter assignment bitmask */
    uint8_t  cc_target;     /* MIDI CC# (0 to 127) */
    uint8_t  cc_value;      /* Base CC value */
    uint32_t active_mask_lo;/* Manual pulse enable mask (steps 1-32) */

    /* Overrides & Secondary Mask (8 bytes) */
    uint32_t active_mask_hi;/* Manual pulse enable mask (steps 33-64) */
    uint32_t reserved_flags;

    /* Parameter Lock Arena (88 bytes): 11 sparse step-override entries (8 bytes per entry) */
    uint8_t p_lock_data[88];
} CycleParams;

_Static_assert(sizeof(CycleParams) == 128, "CycleParams must be 128 bytes");

/* 2. Track State: Exactly 776 bytes */
typedef struct t1_track {
    CycleParams cycles[NUM_CYCLES]; /* 128 * 6 = 768 bytes */
    uint8_t     active_cycle;       /* 0 to 5 */
    uint8_t     cycle_count;        /* 1 to 6 */
    uint8_t     midi_channel;       /* 0 = Internal FM Synth; 1-15 = External MIDI */
    bool        muted;
    bool        soloed;
    uint8_t     reserved[3];
} Track;

_Static_assert(sizeof(Track) == 776, "Track must be 776 bytes");

/* 3. Pattern State: Exactly 4,672 bytes (~4.56 KB) */
typedef struct t1_pattern {
    Track    tracks[NUM_TRACKS];    /* 776 * 6 = 4,656 bytes */
    uint16_t bpm_x10;               /* 300 to 3000 (30.0 - 300.0 BPM) */
    uint8_t  swing;                 /* 50% to 75% */
    uint8_t  master_scale;
    uint8_t  master_root;
    uint8_t  master_length_override;/* Global loop stutter */
    uint8_t  reserved[10];
} Pattern;

_Static_assert(sizeof(Pattern) == 4672, "Pattern must be 4672 bytes");

/* 4. Double-Buffered Workspace: 9,344 bytes (~9.12 KB) for active + snapshot patterns */
typedef struct t1_sequencer_ram {
    Pattern active_pattern;   /* Live playback state (4,672 bytes) */
    Pattern snapshot_pattern; /* Revert buffer for [TEMP] mode (4,672 bytes) */
    uint8_t active_bank_idx;  /* 0 to 5 */
    uint8_t active_pattern_idx;/* 0 to 5 */
    uint8_t reserved[2];      /* Alignment to 4-byte boundary */
} SequencerRAM;

#define PATTERN_PAYLOAD_SIZE (sizeof(Pattern))           /* 4,672 bytes */
#define DOUBLE_BUFFER_SIZE   (2u * PATTERN_PAYLOAD_SIZE) /* 9,344 bytes */

/* ------------------- RAM Reservation: Static Allocation Declarations --- */
/* Statically allocated SequencerRAM in fast internal SRAM (Phase 1, item 2) */
extern SequencerRAM t1_sequencer_ram;

/* Static ring buffer definition for external MIDI queues (256 bytes per interface) */
#define T1_MIDI_RING_BUFFER_SIZE 256

typedef struct {
    uint8_t  buffer[T1_MIDI_RING_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t dropped_bytes;
} MidiRingBuffer;

_Static_assert(sizeof(((MidiRingBuffer *)0)->buffer) == 256, "MIDI ring buffer capacity must be 256 bytes");

/* Static allocation for external MIDI output interfaces (UART TRS-MIDI and USB-MIDI) */
extern MidiRingBuffer t1_midi_uart_queue;
extern MidiRingBuffer t1_midi_usb_queue;

/* -------------------------- Micro-Queue Dispatcher Types (Level 3) --- */
typedef enum {
    T1_EVENT_NOTE_ON,
    T1_EVENT_NOTE_OFF,
    T1_EVENT_CC,
    T1_EVENT_RATCHET,
} T1MicroEventType;

typedef struct {
    uint32_t tick;       /* 192 PPQN tick timestamp */
    uint16_t sub_tick;   /* micro-timing offset / fraction (Q15 or sub-tick ratchet) */
    uint8_t  track;      /* 0 to 5 (Track 0 = Internal FM, 1-5 = External MIDI) */
    uint8_t  type;       /* T1MicroEventType */
    uint8_t  note;       /* MIDI note number (0 to 127) */
    uint8_t  velocity;   /* Velocity (0 to 127) */
    uint8_t  channel;    /* MIDI channel (0 to 15) */
    uint8_t  cc_num;     /* Controller number */
    uint8_t  cc_val;     /* Controller value */
} T1MicroEvent;

#define T1_MICRO_QUEUE_CAPACITY 256

typedef struct {
    T1MicroEvent events[T1_MICRO_QUEUE_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t dropped_events;
} T1MicroQueue;

extern T1MicroQueue t1_micro_queue;

/* -------------------------- Timer ISR & Performance Statistics --- */
typedef struct {
    uint32_t total_ticks;
    uint32_t last_duration_us;
    uint32_t max_duration_us;
    uint32_t violations_80us;   /* count of ISR runs exceeding the 80 us budget */
    uint16_t bpm_x10;           /* current BPM * 10 */
    uint32_t tick_period_us;    /* tick period in microseconds */
    uint32_t tick_period_ns;    /* tick period in nanoseconds */
} T1TimerStats;

extern T1TimerStats t1_timer_stats;

/* ---------------------- FreeRTOS Task Isolation Descriptors --- */
typedef struct {
    const char *task_name;
    uint8_t     priority_level;     /* 1 (Critical) to 5 (Low) */
    uint32_t    freertos_priority;  /* Priority level for FreeRTOS scheduler */
    uint16_t    stack_size_words;   /* Allocated task stack depth */
    uint32_t    period_us;          /* Nominal period in us (0 if event-driven) */
    const char *role_description;
} T1TaskDescriptor;

#define T1_TASK_COUNT 5
extern const T1TaskDescriptor t1_task_table[T1_TASK_COUNT];

/* ------------------------------ Function Prototypes --- */

/* System Initialization */
void t1_system_init(void);
uint32_t t1_get_now_us(void);

/* 192 PPQN Hardware Timer API (Priority Level 1) */
uint32_t t1_calc_tick_period_us(uint16_t bpm_x10);
uint32_t t1_calc_tick_period_ns(uint16_t bpm_x10);
void t1_timer_set_bpm(uint16_t bpm_x10);
void t1_timer_192ppqn_isr(void);
const T1TimerStats *t1_timer_get_stats(void);
void t1_timer_reset_stats(void);

/* MIDI Ring Buffer API (Priority Level 4 Worker Queues) */
void midi_ring_init(MidiRingBuffer *rb);
bool midi_ring_push(MidiRingBuffer *rb, uint8_t byte);
bool midi_ring_push_bytes(MidiRingBuffer *rb, const uint8_t *data, uint16_t len);
bool midi_ring_pop(MidiRingBuffer *rb, uint8_t *byte);
uint16_t midi_ring_available(const MidiRingBuffer *rb);
uint16_t midi_ring_free_space(const MidiRingBuffer *rb);
void midi_ring_flush(MidiRingBuffer *rb);

/* Micro-Queue Dispatcher API (Priority Level 3) */
void t1_micro_queue_init(void);
bool t1_micro_queue_push(const T1MicroEvent *evt);
uint32_t t1_micro_queue_dispatch(uint32_t current_tick, uint16_t current_sub_tick);

/* MIDI Worker Output Task (Priority Level 4) */
uint32_t t1_midi_worker_flush_uart(void (*uart_send_byte)(uint8_t b));
uint32_t t1_midi_worker_flush_usb(void (*usb_send_packet)(uint32_t pkt));
void t1_midi_worker_step(void);

/* Internal FM Voice Engine Callbacks (Priority Level 2 integration for Track 0) */
void synth_note_on(uint8_t voice, uint8_t note, uint8_t velocity);
void synth_note_off(uint8_t voice);
void t1_fm_voice_alloc_reset(void);
int8_t t1_fm_voice_alloc(uint8_t note, uint8_t velocity);
int8_t t1_fm_voice_find(uint8_t note);

/* UI Task Cadence Control (Priority Level 5) */
bool t1_ui_task_should_render(uint32_t now_ms);
void t1_ui_task_did_render(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* TORSO_T1_H */
