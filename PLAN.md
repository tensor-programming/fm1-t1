### Sequencer Architecture & Memory Footprint

Downscaling the sequencer engine to a **6 Banks $\times$ 6 Patterns $\times$ 6 Tracks $\times$ 6 Cycles** architecture establishes a compact, deterministic memory profile:

* **Pattern Pool:** 36 patterns total (6 banks of 6 patterns).
* **Track-Cycle Density:** 36 track-cycle parameter units per pattern.
* **Pattern Payload:** 4,672 bytes (~4.56 KB) per pattern.
* **Active Working RAM (Double-Buffered):** **9.34 KB** (Active Pattern + `[TEMP]` Snapshot).
* **Total Flash Storage:** **168.2 KB** for all 36 patterns across all 6 banks (fits within three standard 64 KB SPI flash erase sectors).

---

### Phase 1: Environment Baseline & Resource Budgeting

1. **FreeRTOS Task Isolation & Interrupt Priorities**
* **Priority Level 1 (Critical):** Hardware Timer ISR configured at 192 PPQN. At 120 BPM, tick resolution is $2.604\text{ ms}$; at 300 BPM, resolution is $1.041\text{ ms}$.
* **Priority Level 2 (High):** Audio DMA callback servicing the 6-operator FM synth core (MicroDexed/Dexed audio frames).
* **Priority Level 3 (Medium-High):** Sequencer Micro-Queue Dispatcher (processes note-ons, sub-tick ratchets, micro-delay, and note-offs).
* **Priority Level 4 (Medium):** Serial/USB MIDI output worker (flushes UART and TinyUSB ring buffers).
* **Priority Level 5 (Low):** UI task & OLED/LCD refresh (runs at 30–60 Hz).


2. **RAM Reservation**
* Statically allocate `SequencerRAM` (9,344 bytes) in fast internal SRAM to avoid heap fragmentation and allocation latency during live performance.
* Allocate static ring buffers for the external MIDI queues (256 bytes per physical output interface).



---

### Phase 2: Data Model & Algorithmic Modules

Define the sequencer state machine in decoupled, testable C modules using tight byte packing and explicit data alignment:

```c
#include <stdint.h>
#include <stdbool.h>

#define NUM_TRACKS        6
#define NUM_CYCLES        6
#define PATTERNS_PER_BANK 6
#define NUM_BANKS         6

// 1. Cycle Parameter Block: Exactly 128 bytes
typedef struct __attribute__((packed)) {
    // Rhythm & Timing (8 bytes)
    uint8_t steps;          // 1 to 64
    uint8_t pulses;         // 0 to steps
    int8_t  rotate;         // -64 to +64
    uint8_t division;       // 1/1, 1/2, 1/4, 1/8, 1/16, 1/32 (triplets/dotted)
    uint8_t repeats;        // 1 to 16
    int8_t  offset;         // Velocity slope (-64 to +63)
    uint8_t time;           // Ratchet gate density (1 to 127)
    int8_t  pace;           // Log/Exp curve shape (-64 to +63)

    // Pitch & Voicing (8 bytes)
    int8_t  pitch_offset;   // Chromatic semitone shift (-48 to +48)
    uint8_t scale_id;       // Scale definition index (0 to 15)
    uint8_t root_note;      // 0 (C) to 11 (B)
    uint8_t voicing;        // Inversion / octave displacement style
    uint8_t style;          // Arp direction mode
    uint8_t phrase;         // Melodic contour wave shape
    uint8_t range;          // Melodic excursion span
    uint8_t reserved_pitch;

    // Dynamics & Stochastic (8 bytes)
    uint8_t velocity;       // Base velocity (1 to 127)
    uint8_t probability;    // Trigger chance (0 to 100%)
    uint8_t sustain;        // Gate length (1% to >100% legato)
    uint8_t accent;         // Accent velocity delta
    uint8_t groove_id;      // Micro-timing template (0 to 15)
    int8_t  delay_ticks;    // Track micro-nudge (-12 to +12 ticks)
    uint8_t random_depth;   // Modulation depth (0 to 127)
    uint8_t random_rate;    // LFSR step interval (0 to 15)

    // Modulation Targets & Bitmasks (8 bytes)
    uint16_t random_targets;// Parameter assignment bitmask
    uint8_t  cc_target;     // MIDI CC# (0 to 127)
    uint8_t  cc_value;      // Base CC value
    uint32_t active_mask_lo;// Manual pulse enable mask (steps 1-32)

    // Overrides & Secondary Mask (8 bytes)
    uint32_t active_mask_hi;// Manual pulse enable mask (steps 33-64)
    uint32_t reserved_flags;

    // Parameter Lock Arena (88 bytes)
    // 11 sparse step-override entries (8 bytes per entry)
    uint8_t p_lock_data[88]; 
} CycleParams;

_Static_assert(sizeof(CycleParams) == 128, "CycleParams must be 128 bytes");

// 2. Track State: 776 bytes
typedef struct {
    CycleParams cycles[NUM_CYCLES]; // 128 * 6 = 768 bytes
    uint8_t     active_cycle;       // 0 to 5
    uint8_t     cycle_count;        // 1 to 6
    uint8_t     midi_channel;       // 0 = Internal FM Synth; 1-15 = External MIDI
    bool        muted;
    bool        soloed;
    uint8_t     reserved[3];
} Track;

// 3. Pattern State: Exactly 4,672 bytes (~4.56 KB)
typedef struct {
    Track    tracks[NUM_TRACKS];    // 776 * 6 = 4,656 bytes
    uint16_t bpm_x10;               // 300 to 3000 (30.0 - 300.0 BPM)
    uint8_t  swing;                 // 50% to 75%
    uint8_t  master_scale;
    uint8_t  master_root;
    uint8_t  master_length_override;// Global loop stutter
    uint8_t  reserved[10];
} Pattern;

_Static_assert(sizeof(Pattern) == 4672, "Pattern must be 4672 bytes");

// 4. Double-Buffered Workspace: 9,344 bytes (~9.12 KB)
typedef struct {
    Pattern active_pattern;   // Live playback state
    Pattern snapshot_pattern; // Revert buffer for [TEMP] mode
    uint8_t active_bank_idx;  // 0 to 5
    uint8_t active_pattern_idx;// 0 to 5
} SequencerRAM;

```

#### Core Algorithmic Subsystems

1. **Euclidean Engine:**
* Run Bjorklund's distribution for 6 tracks simultaneously using an integer bitmask algorithm that outputs a 64-bit integer representing step triggers.
* Apply circular shift for `rotate`:
```c
uint64_t rotated_mask = (mask >> r) | (mask << (steps - r));

```




2. **Pace & Ratchet Accumulator:**
* Evaluate `repeats` (1 to 16) within each Euclidean pulse.
* Compute non-linear ratchet intervals using a $Q15$ fixed-point table mapping `pace` values (negative = accelerating/bouncing ball, positive = decelerating).


3. **Harmony & Voicing Engine:**
* Maintain a global pitch array of scale intervals for all 16 scale types.
* If a track enables **Master Harmony Link**, project its chord and scale root relative to Track 1's tonal center.
* Apply voice inversion algorithms (`Voicing` parameter) to redistribute note octaves within a defined range.


4. **Deterministic LFSR Random Engine:**
* Utilize a 16-bit Galois LFSR with configurable cycle loop periods (1, 2, 3, 4, 6 cycles).
* Modulate only the parameters tagged in the `random_targets` bitfield.



---

### Phase 3: Dual Output Routing & Event Pipeline

```
           [192 PPQN Hardware Timer ISR]
                         │
                         ▼
             [Tick Counter & Clock Sync]
                         │
        ┌────────────────┴────────────────┐
        ▼                                 ▼
[Euclidean Step Evaluator]    [Micro-Timing Ring Queue]
(6 Tracks evaluated on ticks) (Ratchets, Pace, Swings, Delays)
        │                                 │
        └────────────────┬────────────────┘
                         ▼
             [Note & CC Dispatcher]
                         │
        ┌────────────────┴────────────────┐
        ▼                                 ▼
 [Track 0 (Default)]            [Tracks 1 through 5]
Direct C-Call to FM Core       Non-blocking FreeRTOS Queue
synth_note_on(v, n, vel)                  │
                               ┌──────────┴──────────┐
                               ▼                     ▼
                         [UART TRS-MIDI]       [TinyUSB Device]

```

1. **Internal Voice Engine Dispatch:**
* Route Track 0 directly to Felucca’s internal 6-operator FM synth engine.
* Invoke `synth_note_on(voice, note, velocity)` and `synth_note_off(voice)` directly, avoiding MIDI serialization overhead.
* Use a 6-voice polyphonic allocation model with lowest-velocity or oldest-note stealing.


2. **External Serial Output:**
* Push Note-On, Note-Off, and CC events for Tracks 1 through 5 into a ring buffer.
* The serial output task reads the queue and dispatches bytes to the physical TRS MIDI UART and USB-MIDI endpoints.



---

### Phase 4: State Machine, Performance Modes, and Storage

1. **`[TEMP]` Mode Implementation:**
* Entering `[TEMP]`: Keep `snapshot_pattern` intact; route all knob turns, track mutes, and macro changes into `active_pattern`.
* Releasing `[TEMP]`: Execute `memcpy(&sequencer.active_pattern, &sequencer.snapshot_pattern, sizeof(Pattern))`. At 4,672 bytes on a Cortex-M core, this operation completes in under **6 microseconds**, ensuring zero transport interruption.
* Committing changes: If the user presses `[SAVE]`/`[COMMIT]` while holding `[TEMP]`, execute `memcpy(&sequencer.snapshot_pattern, &sequencer.active_pattern, sizeof(Pattern))`.


2. **Global Track Offsets:**
* Holding `[GLOBAL]` while adjusting an encoder applies a relative delta $\Delta$ across all 6 tracks simultaneously without overwriting individual track offsets.


3. **SPI Flash Layout (36 Patterns Total):**
* Divide a dedicated 256 KB SPI flash region into 6 sectors for the 6 banks:
* **Bank 0:** `0x00000` – `0x06FFF` (28.03 KB)
* **Bank 1:** `0x08000` – `0x0EFFF` (28.03 KB)
* **Bank 2:** `0x10000` – `0x16FFF` (28.03 KB)
* **Bank 3:** `0x18000` – `0x1EFFF` (28.03 KB)
* **Bank 4:** `0x20000` – `0x26FFF` (28.03 KB)
* **Bank 5:** `0x28000` – `0x2EFFF` (28.03 KB)


* Pattern loading requires streaming only 4.67 KB from SPI flash into RAM, taking $\sim 1.17\text{ ms}$ at a 32 MHz SPI bus rate.



---

### Phase 5: UI & Control Surface Adaptation

Map the 6-track $\times$ 6-cycle interface onto the FM-1's screen and physical controls:

* **Track Selection:** Use dedicated hardware buttons or a navigation encoder to cycle through **Tracks 1–6**.
* **Cycle Selection:** Use secondary button modifiers to select **Cycles 1–6** for parameter locking.
* **Display Paging:**

| Page | Encoder 1 (Turn / Shift) | Encoder 2 (Turn / Shift) | Encoder 3 (Turn / Shift) | Encoder 4 (Turn / Shift) |
| --- | --- | --- | --- | --- |
| **1: Rhythm** | Steps / Max Length | Pulses / Rotate | Division / Gate Len | Cycle Count / Step Mod |
| **2: Ratchet** | Repeats / Offset | Time / Pace | Velocity / Accent | Probability / Groove |
| **3: Harmony** | Pitch / Master Trans | Root / Scale | Voicing / Style | Phrase / Range |
| **4: Mod / CC** | Rnd Target / Channel | Rnd Depth / Rnd Rate | CC Number / Slew | CC Value / Loop Sync |

---

### Concrete Verification Checklist

1. **Struct Packing & Alignment Assertion:**
* Execute compile-time verification:
```c
static_assert(sizeof(Pattern) == 4672, "Pattern struct padding mismatch");
static_assert(sizeof(CycleParams) == 128, "CycleParams struct padding mismatch");

```




2. **Flash Boundary Verification:**
* Verify that each bank's storage allocation (28,032 bytes) does not span across non-contiguous 64 KB flash erase sectors.


3. **Interrupt Jitter Profiling:**
* Measure the 192 PPQN timer ISR execution duration via a scoped GPIO pin under worst-case load (all 6 tracks triggering simultaneous 16-step ratchets and internal FM audio synthesizing 6 voices).
* Confirm ISR execution time remains strictly $< 80\ \mu\text{s}$.


4. **Snapshot Revert Latency:**
* Benchmark the execution time of the `[TEMP]` revert `memcpy` operation using the system cycle counter (`DWT->CYCCNT`) to confirm it completes within the target $< 10\ \mu\text{s}$ window.


5. **Direct Synthesis Polyphony Stress Test:**
* Run Track 0 at 16 repeats per pulse with short sustain while playing 4-note polyphonic chords. Confirm that voice-stealing routines trigger cleanly without audio clicks, pop artifacts, or buffer underruns.