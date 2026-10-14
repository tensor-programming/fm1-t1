# Torso T-1 Algorithmic Sequencer User Guide & Reference Manual

The Torso T-1 algorithmic sequencer upgrade transforms the Felucca groovebox into a generative sequencing workstation inspired by the Torso Electronics T-1, while preserving 100% of Felucca's existing synthesis engines (FM6, ANALOG, DRUM, PHYS, NOISE, SLICE), DSP effects pipeline, preset management, and UI pages.

---

## 1. Overview & Architecture

### Dual Output Routing Architecture
- **Track 0 (Direct Internal Synthesis):** Directly drives Felucca's 6-operator FM core synthesizer engine using a 6-voice polyphonic allocation model with lowest-velocity or oldest-note stealing.
- **Tracks 1–5 (External MIDI Out):** Serialized in real time through dedicated circular ring buffers to hardware 3.5mm TRS-MIDI UART and USB-MIDI class-compliant endpoints on configurable MIDI channels (1–16).

```
                      +---------------------------------------+
                      |       192 PPQN Hardware Master Clock  |
                      +-------------------+-------------------+
                                          |
                        +-----------------+-----------------+
                        | 6-Track Algorithmic State Machine |
                        +-----------------+-----------------+
                                          |
                 +------------------------+------------------------+
                 |                                                 |
         [ Track 0 ]                                       [ Tracks 1 - 5 ]
                 |                                                 |
                 v                                                 v
   +---------------------------+                     +---------------------------+
   |  Direct Synthesis Bridge  |                     |  MIDI Serial Ring Buffer  |
   | (Lowest-Velocity Stealing)|                     |  (TRS-MIDI UART & USB)    |
   +-------------+-------------+                     +-------------+-------------+
                 |                                                 |
                 v                                                 v
      Felucca 6-OP FM Synth Engine                         External Synthesizers / DAW
```

### Core Specifications
- **Timing Resolution:** 192 Pulses Per Quarter Note (PPQN). At 120.0 BPM, 1 tick = 2.604 ms; at 300.0 BPM, 1 tick = 1.041 ms.
- **Tracks & Cycles:** 6 independent tracks, each containing 6 individual parameter cycles (36 cycle workspaces per pattern).
- **Storage Capacity:** 6 Banks $\times$ 6 Patterns = 36 Patterns (28,032 bytes per bank) stored across dedicated 32 KB blocks on SPI NOR flash.
- **Real-Time Memory:** 9,344 bytes double-buffered SRAM workspace (`active_pattern` and `snapshot_pattern`). Zero heap allocations during performance.

---

## 2. M-VAVE FM-1 Physical Control Surface Mapping

Because the M-VAVE FM-1 hardware has its own silkscreened buttons, the Torso T-1 virtual controls and modifiers map directly to physical FM-1 controls as follows:

| Torso T-1 Function | M-VAVE FM-1 Physical Control | Gesture / Action |
| :--- | :--- | :--- |
| **`[TEMP]` (Snapshot)** | **`SAVE`** (or **`LFO`**) | **Hold `SAVE`** (or **Hold `LFO`**) while tweaking knobs `K1`–`K4`. Releasing reverts instantly; pressing `OCT+` commits. |
| **`[GLOBAL]` (All Tracks)** | **`GLO`** button | **Hold `GLO`** while turning knobs `K1`–`K4` to adjust all 6 tracks simultaneously with relative deltas. |
| **`[SHIFT]` (Secondary Row)** | **`OCT+`** button | **Hold `OCT+`** while turning knobs `K1`–`K4` to access secondary parameters (MaxLen, Rotate, Pace, Accent, etc.). |
| **`[TRACK]` (Track 1–6)** | **Keys 1–6** | Press bottom piano keys 1 through 6 to switch active sequencer track. |
| **`[CYCLES]` (Cycle 1–6)** | **Keys 7–12** / **Knob 4** | Turn Knob 4 on Page 1 or press bottom piano keys 7 through 12 to switch active cycle (C1–C6). |
| **`[P-LOCK]` (Step Lock)** | **Keys 1–16** | **Hold Step Key** (1–16) and tweak any knob to parameter-lock that step. Release key to exit. |
| **Page Navigation** | **`SEQ`** / **`OCT-`** / **`OCT+`** | Tap **`SEQ`** to cycle through the 4 T-1 pages: RHYTHM, RATCHET, HARMONY, MOD/CC. |
| **Play / Stop** | **`PLAY`** | Starts or stops the 192 PPQN sequencer master clock. |
| **Record / Automate** | **`REC`** | Arms real-time parameter recording and step capture. |

---

## 3. Control Surface & Navigation

### Track & Cycle Selection
- **Select Track (1–6):** Press Piano Keys 1 through 6, or use `t1_ui_select_track(index)`.
- **Select Cycle (1–6):** Select active cycle (C1–C6) using Encoder 4 (Page 1), Piano Keys 7–12, or `t1_ui_select_cycle(index)`. Each cycle stores independent Euclidean steps, ratchets, scales, and parameter locks.

### Display Paging (4 Dedicated Pages)
Navigate through the 4 pages by tapping **`SEQ`** or using `t1_ui_next_page()` / `t1_ui_prev_page()`:

```
[ Page 1: RHYTHM ] ---> [ Page 2: RATCHET ] ---> [ Page 3: HARMONY ] ---> [ Page 4: MOD/CC ]
```

---

## 3. Rotary Encoder Parameter Matrix

Each page maps the 4 physical rotary encoders to a primary parameter (normal turn) and a secondary parameter (held **[SHIFT]** turn):

### Page 1: RHYTHM (Euclidean Pattern & Division)
| Knob | Turn (Primary) | Shift + Turn (Secondary) | Range / Description |
| :--- | :--- | :--- | :--- |
| **Encoder 1** | **STEPS** | **MAX LEN** | Steps: `1..64`. Max Length: Master pattern loop override (`0..64`, `0` = Off). |
| **Encoder 2** | **PULSES** | **ROTATE** | Pulses: Björklund Euclidean density (`0..Steps`). Rotate: Step offset (`-64..+64`). |
| **Encoder 3** | **DIVISION** | **GATE LEN** | Division: 11 rhythmic rates (`1/1` to `1/32`, triplets, dotted). Gate: `1..255%`. |
| **Encoder 4** | **CYCLE** | **STEP MOD** | Cycle: Active cycle (`C1..C6`). Step Mod: Manual pulse bitmask toggle. |

### Page 2: RATCHET (Repeats, Timing & Pacing)
| Knob | Turn (Primary) | Shift + Turn (Secondary) | Range / Description |
| :--- | :--- | :--- | :--- |
| **Encoder 1** | **REPEATS** | **OFFSET** | Repeats: Ratchet burst subdivision (`1..16`). Offset: Velocity slope (`-64..+63`). |
| **Encoder 2** | **TIME** | **PACE** | Time: Gate density (`1..127`). Pace: Non-linear $Q15$ curve (`-64..+63`). |
| **Encoder 3** | **VELOCITY** | **ACCENT** | Velocity: Base velocity (`1..127`). Accent: Accent boost added to accented pulses (`0..127`). |
| **Encoder 4** | **PROB** | **GROOVE** | Probability: Step firing chance (`0..100%`). Groove: Groove template (`G0..G15`). |

### Page 3: HARMONY (Scales, Voicing & Arpeggiator)
| Knob | Turn (Primary) | Shift + Turn (Secondary) | Range / Description |
| :--- | :--- | :--- | :--- |
| **Encoder 1** | **PITCH** | **M-TRANS** | Pitch: Track pitch offset (`-48..+48 st`). Master Transpose: Song root (`C..B`). |
| **Encoder 2** | **ROOT** | **SCALE** | Root: Track root note (`C..B`). Scale: Quantization scale (`0..15`, see list below). |
| **Encoder 3** | **VOICING** | **STYLE** | Voicing: Diatonic chord voicing (`0..11`). Style: Arpeggiator direction (`0..7`). |
| **Encoder 4** | **PHRASE** | **RANGE** | Phrase: Melodic contour shape (`0..7`). Range: Pitch excursion limit (`0..127 st`). |

### Page 4: MOD / CC (Random Modulation & Output)
| Knob | Turn (Primary) | Shift + Turn (Secondary) | Range / Description |
| :--- | :--- | :--- | :--- |
| **Encoder 1** | **RND TGT** | **CHANNEL** | Random Target: Bitmask of randomized params. Channel: Output MIDI channel (`1..16`). |
| **Encoder 2** | **RND DEPTH** | **RND RATE** | Random Depth: Amount (`0..127`). Random Rate: Galois LFSR loop period (`/1../16` cycles). |
| **Encoder 3** | **CC NUM** | **SLEW/DEL** | CC Number: Target MIDI CC (`0..127`). Slew/Delay: Micro-timing nudge (`-12..+12 tk`). |
| **Encoder 4** | **CC VAL** | **VOICES** | CC Value: Base output (`0..127`). Voices: Polyphonic chord size (`1..4` voices). |

---

## 4. Performance Modes & Live Modifiers

### 1. `[TEMP]` Snapshot Mode (Physical: Hold `SAVE` or Hold `LFO`)
The `[TEMP]` snapshot mode allows risk-free live improvisation and build-ups:
1. **Engage `[TEMP]`:** Press and hold **`SAVE`** (or **`LFO`**). The screen header updates to show `[TEMP]`.
2. **Improvise Live:** Tweak encoders `K1`–`K4`, change divisions, mutate scales, increase ratchets, or change pulses. Edits apply immediately to `active_pattern` and are heard in real time.
3. **Revert (Punch-out):** Simply **release the held button** (`SAVE` or `LFO`). The firmware performs an instant microsecond memory block copy from `snapshot_pattern` (`0.081 µs` latency), instantaneously restoring the pattern without any timing hiccups or clicks.
4. **Commit (Keep edits):** If you like your live edits and want to make them permanent, press **`OCT+`** while still holding the button. This commits `active_pattern` into `snapshot_pattern`.

### 2. `[GLOBAL]` Relative Parameter Offsets (Physical: Hold `GLO`)
Adjust multiple tracks simultaneously while preserving individual musical differences:
1. **Hold `GLO`:** The screen header displays `[GLOB]`.
2. **Turn Any Encoder (`K1`–`K4`):** Turning an encoder applies a relative delta $\Delta$ across **all 6 tracks** simultaneously.
   - Example: If Track 1 has 16 steps and Track 2 has 12 steps, turning the Steps encoder $+4$ results in Track 1 having 20 steps and Track 2 having 16 steps.
   - Clamping respects parameter boundaries (e.g. 1..64 steps).

### 3. Step Parameter Locking (P-Locks) (Physical: Hold Step Keys 1–16)
Lock parameter values to individual steps:
1. **Hold a Step Key (1–16):** The header indicates `[P-LOCK:step]`.
2. **Turn Any Knob:** Turn any supported parameter knob (e.g. Pitch, Velocity, Probability) to lock an override value for that step.
3. Fast SRAM stores up to 11 parameter locks $\times$ 8 bytes (88 bytes total) per cycle.
4. **Release Key:** Release the step key to return to normal track editing.

---

## 5. Algorithmic Modules Reference

### Björklund Euclidean Rhythm ($E(p, s)$)
- Distributes $p$ pulses as evenly as possible across $s$ steps using the Euclidean greatest common divisor algorithm.
- Supports **Rotate** to circularly shift the rhythm without altering pulse spacing.
- Supports **Step Mod** manual mask overrides for muting or forcing individual steps.

### Non-Linear Pace ($Q15$ Fixed-Point)
- Adjust the timing curvature of ratchets:
  - **Pace = 0:** Linear spacing.
  - **Pace < 0:** Logarithmic acceleration (events start slow, then speed up).
  - **Pace > 0:** Exponential deceleration (events burst quickly, then slow down).

### Harmony & Scale Quantization
The harmony engine downward-quantizes raw pitches into the selected scale:
- `0`: Chromatic
- `1`: Major (Ionian)
- `2`: Natural Minor (Aeolian)
- `3`: Dorian
- `4`: Phrygian
- `5`: Lydian
- `6`: Mixolydian
- `7`: Aeolian
- `8`: Locrian
- `9`: Pentatonic Major
- `10`: Pentatonic Minor
- `11`: Blues Scale
- `12`: Whole Tone
- `13`: Harmonic Minor
- `14`: Melodic Minor
- `15`: Diminished (Half-Whole)

### Voicing Styles & Inversions
Chord generation supports 12 diatonic voicing strategies:
- `0: ROOT` — Root position
- `1: INV 1` — 1st Inversion (lowest note shifted $+1$ octave)
- `2: INV 2` — 2nd Inversion
- `3: INV 3` — 3rd Inversion
- `4: DROP2` — Drop-2 voicing (2nd highest voice dropped 1 octave)
- `5: DROP3` — Drop-3 voicing (3rd highest voice dropped 1 octave)
- `6: SPRD`  — Spread voicing (alternating octaves)
- `7: OCT+`  — Transposed up 1 octave
- `8: OCT-`  — Transposed down 1 octave
- `9: SPRD W`— Wide spread voicing
- `10: 2OCT+`— Transposed up 2 octaves
- `11: 2OCT-`— Transposed down 2 octaves

### Melodic Contours & Arpeggiator Styles
- **Styles (8 Directions):** `UP`, `DOWN`, `UP-DN`, `DN-UP`, `CNVRG` (converge to center), `DIVRG` (diverge to edges), `RAND` (random permutation), `ORDER` (order of entry).
- **Phrase Contours (8 Shapes):** `FLAT`, `SINE`, `TRI` (triangle), `SAW U` (saw up), `SAW D` (saw down), `ARCH`, `INV A` (inverted arch), `RAND` (random drift).

### Deterministic Galois LFSR (Pseudo-Random)
- Generates reproducible random modulation sequences using a 16-bit Galois LFSR with polynomial $x^{16} + x^{14} + x^{13} + x^{11} + 1$.
- **Cycle Period Locking:** When `random_rate` is set, the LFSR automatically resets its seed at cycle loop boundaries ($1, 2, 3, 4, 6$ cycles), creating looping pseudo-random variations that repeat every $N$ musical cycles.

---

## 6. Flash Storage & Memory Layout

Patterns are stored across 6 dedicated 32 KB flash sectors strictly aligned to prevent 64 KB sector erase boundary crossings:

| Bank | Start Address | End Address | Stored Size | Sector Boundary Safety |
| :--- | :--- | :--- | :--- | :--- |
| **Bank 0** | `0x00000` | `0x06FFF` | 28,032 B | Fits within Sector 0 (`0x00000..0x0FFFF`) |
| **Bank 1** | `0x08000` | `0x0EFFF` | 28,032 B | Fits within Sector 0 (`0x00000..0x0FFFF`) |
| **Bank 2** | `0x10000` | `0x16FFF` | 28,032 B | Fits within Sector 1 (`0x10000..0x1FFFF`) |
| **Bank 3** | `0x18000` | `0x1EFFF` | 28,032 B | Fits within Sector 1 (`0x10000..0x1FFFF`) |
| **Bank 4** | `0x20000` | `0x26FFF` | 28,032 B | Fits within Sector 2 (`0x20000..0x2FFFF`) |
| **Bank 5** | `0x28000` | `0x2EFFF` | 28,032 B | Fits within Sector 2 (`0x20000..0x2FFFF`) |

- **Streaming Speed:** Pattern load streams 4,672 bytes from flash in **1.17 ms** at a 32 MHz SPI clock rate.
