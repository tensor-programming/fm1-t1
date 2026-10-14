/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Torso T-1 Architecture - Phase 5: UI & Control Surface Adaptation Implementation */

#include "torso_t1_ui.h"

#if !defined(FELUCCA_ID) && !defined(FM1_SYS_H)
#include <stdio.h>
#include <string.h>
#else
int snprintf(char *str, unsigned size, const char *format, ...);
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
#endif

static inline int32_t t1_abs(int32_t x)
{
    return (x < 0) ? -x : x;
}

T1UIState t1_ui_state;

/* ---------------- Musical & Parameter Lookup Tables for UI --- */
static const char *const SCALE_NAMES[16] = {
    "CHROM", "MAJOR", "MINOR", "DORIAN", "PHRYG", "LYDIAN", "MIXOL", "AEOL",
    "LOCR",  "P-MAJ", "P-MIN", "BLUES",  "WHOLE", "H-MIN", "M-MIN", "DIM"
};

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static const char *const VOICING_NAMES[12] = {
    "ROOT", "INV 1", "INV 2", "INV 3", "DROP2", "DROP3",
    "SPRD", "OCT+",  "OCT-",  "SPRD W","2OCT+", "2OCT-"
};

static const char *const STYLE_NAMES[8] = {
    "UP", "DOWN", "UP-DN", "DN-UP", "CNVRG", "DIVRG", "RAND", "ORDER"
};

static const char *const PHRASE_NAMES[8] = {
    "FLAT", "SINE", "TRI", "SAW U", "SAW D", "ARCH", "INV A", "RAND"
};

static const char *const DIV_NAMES[T1_DIV_COUNT] = {
    "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
    "1/8T", "1/16T", "1/32T", "1/8D", "1/16D"
};

/* ---------------- Lifecycle & Initialization --- */

void t1_ui_init(void)
{
    memset(&t1_ui_state, 0, sizeof(t1_ui_state));
    t1_ui_state.current_page   = T1_PAGE_RHYTHM;
    t1_ui_state.selected_track = 0;
    t1_ui_state.selected_cycle = 0;
    t1_ui_state.edit_step      = 0xFF; /* Normal track mode */
    t1_ui_state.hot_encoder    = 0xFF;
}

/* ---------------- Navigation & Selection API --- */

void t1_ui_select_track(uint8_t track_idx)
{
    if (track_idx >= NUM_TRACKS) track_idx = NUM_TRACKS - 1u;
    t1_ui_state.selected_track = track_idx;
}

void t1_ui_next_track(void)
{
    t1_ui_state.selected_track = (uint8_t)((t1_ui_state.selected_track + 1u) % NUM_TRACKS);
}

void t1_ui_prev_track(void)
{
    t1_ui_state.selected_track = (uint8_t)((t1_ui_state.selected_track + NUM_TRACKS - 1u) % NUM_TRACKS);
}

void t1_ui_select_cycle(uint8_t cycle_idx)
{
    if (cycle_idx >= NUM_CYCLES) cycle_idx = NUM_CYCLES - 1u;
    t1_ui_state.selected_cycle = cycle_idx;
    t1_sequencer_ram.active_pattern.tracks[t1_ui_state.selected_track].active_cycle = cycle_idx;
}

void t1_ui_set_page(T1UIPage page)
{
    if (page >= T1_PAGE_COUNT) page = (T1UIPage)(T1_PAGE_COUNT - 1u);
    t1_ui_state.current_page = page;
}

void t1_ui_next_page(void)
{
    t1_ui_state.current_page = (T1UIPage)((t1_ui_state.current_page + 1u) % T1_PAGE_COUNT);
}

void t1_ui_prev_page(void)
{
    t1_ui_state.current_page = (T1UIPage)((t1_ui_state.current_page + T1_PAGE_COUNT - 1u) % T1_PAGE_COUNT);
}

void t1_ui_set_shift(bool active)
{
    t1_ui_state.shift_active = active;
}

void t1_ui_set_temp(bool active)
{
    if (active && !t1_ui_state.temp_active) {
        t1_temp_enter();
    } else if (!active && t1_ui_state.temp_active) {
        t1_temp_revert();
    }
    t1_ui_state.temp_active = active;
}

void t1_ui_set_global(bool active)
{
    if (active && !t1_ui_state.global_active) {
        t1_global_enter();
    } else if (!active && t1_ui_state.global_active) {
        t1_global_exit();
    }
    t1_ui_state.global_active = active;
}

void t1_ui_set_edit_step(uint8_t step_idx)
{
    t1_ui_state.edit_step = step_idx;
}

/* ---------------- Physical Encoder Handling (4 Knobs) --- */

void t1_ui_on_encoder_turn(uint8_t encoder_idx, int8_t steps)
{
    if (encoder_idx >= 4 || steps == 0) return;

    t1_ui_state.hot_encoder = encoder_idx;
    t1_ui_state.hot_timeout = 30;

    /* Handle [GLOBAL] modifier: route to global relative offset dispatcher */
    if (t1_ui_state.global_active) {
        T1ParameterID pid = T1_PARAM_STEPS;

        switch (t1_ui_state.current_page) {
            case T1_PAGE_RHYTHM:
                if (encoder_idx == 0) pid = T1_PARAM_STEPS;
                else if (encoder_idx == 1) pid = t1_ui_state.shift_active ? T1_PARAM_ROTATE : T1_PARAM_PULSES;
                else if (encoder_idx == 2) pid = t1_ui_state.shift_active ? T1_PARAM_SUSTAIN : T1_PARAM_DIVISION;
                else if (encoder_idx == 3) pid = T1_PARAM_STEPS;
                break;
            case T1_PAGE_RATCHET:
                if (encoder_idx == 0) pid = t1_ui_state.shift_active ? T1_PARAM_OFFSET : T1_PARAM_REPEATS;
                else if (encoder_idx == 1) pid = t1_ui_state.shift_active ? T1_PARAM_PACE : T1_PARAM_TIME;
                else if (encoder_idx == 2) pid = T1_PARAM_VELOCITY;
                else if (encoder_idx == 3) pid = T1_PARAM_PROBABILITY;
                break;
            case T1_PAGE_HARMONY:
                if (encoder_idx == 0) pid = T1_PARAM_PITCH;
                else if (encoder_idx == 1) pid = t1_ui_state.shift_active ? T1_PARAM_SCALE : T1_PARAM_ROOT;
                else if (encoder_idx == 2) pid = t1_ui_state.shift_active ? T1_PARAM_STYLE : T1_PARAM_VOICING;
                else if (encoder_idx == 3) pid = t1_ui_state.shift_active ? T1_PARAM_RANGE : T1_PARAM_PHRASE;
                break;
            case T1_PAGE_MOD_CC:
                if (encoder_idx == 1) pid = T1_PARAM_RANDOM_DEPTH;
                else if (encoder_idx == 2 && t1_ui_state.shift_active) pid = T1_PARAM_DELAY;
                break;
            default:
                break;
        }

        t1_global_apply_delta(pid, steps);
        return;
    }

    Pattern *pat = &t1_sequencer_ram.active_pattern;
    Track *trk = &pat->tracks[t1_ui_state.selected_track];
    uint8_t c_idx = trk->active_cycle % NUM_CYCLES;
    CycleParams *cp = &trk->cycles[c_idx];

    /* Parameter Locking: if editing a step, lock the parameter to that step */
    if (t1_ui_state.edit_step != 0xFF) {
        uint8_t step = t1_ui_state.edit_step;
        if (t1_ui_state.current_page == T1_PAGE_HARMONY && encoder_idx == 0) {
            /* Lock Pitch */
            int16_t cur = 60;
            t1_plock_get(cp, step, 57 /* P_PITCH */, &cur);
            cur += steps;
            if (cur < 0) cur = 0;
            if (cur > 127) cur = 127;
            t1_plock_set(cp, step, 57 /* P_PITCH */, cur);
        } else if (t1_ui_state.current_page == T1_PAGE_RATCHET && encoder_idx == 2) {
            /* Lock Velocity */
            int16_t cur = cp->velocity ? cp->velocity : 100;
            t1_plock_get(cp, step, 67 /* P_VEL */, &cur);
            cur += steps;
            if (cur < 1) cur = 1;
            if (cur > 127) cur = 127;
            t1_plock_set(cp, step, 67 /* P_VEL */, cur);
        } else if (t1_ui_state.current_page == T1_PAGE_RATCHET && encoder_idx == 3) {
            /* Lock Probability */
            int16_t cur = cp->probability;
            t1_plock_get(cp, step, 68 /* P_PROB */, &cur);
            cur += steps;
            if (cur < 0) cur = 0;
            if (cur > 100) cur = 100;
            t1_plock_set(cp, step, 68 /* P_PROB */, cur);
        }
        return;
    }

    /* Normal Track & Cycle Editing */
    bool shift = t1_ui_state.shift_active;

    switch (t1_ui_state.current_page) {
        /* ---------------- Page 1: Rhythm --- */
        case T1_PAGE_RHYTHM:
            if (encoder_idx == 0) {
                if (!shift) {
                    /* Steps (1..64) */
                    int16_t v = (int16_t)cp->steps + steps;
                    if (v < 1) v = 1;
                    if (v > 64) v = 64;
                    cp->steps = (uint8_t)v;
                    if (cp->pulses > cp->steps) cp->pulses = cp->steps;
                    t1_pipeline_invalidate_masks();
                } else {
                    /* Master Length Override (0..64) */
                    int16_t v = (int16_t)pat->master_length_override + steps;
                    if (v < 0) v = 0;
                    if (v > 64) v = 64;
                    pat->master_length_override = (uint8_t)v;
                }
            } else if (encoder_idx == 1) {
                if (!shift) {
                    /* Pulses (0..steps) */
                    int16_t v = (int16_t)cp->pulses + steps;
                    if (v < 0) v = 0;
                    uint8_t max_p = cp->steps ? cp->steps : 16u;
                    if (v > max_p) v = max_p;
                    cp->pulses = (uint8_t)v;
                    t1_pipeline_invalidate_masks();
                } else {
                    /* Rotate (-64..+64) */
                    int16_t v = (int16_t)cp->rotate + steps;
                    if (v < -64) v = -64;
                    if (v > 64) v = 64;
                    cp->rotate = (int8_t)v;
                    t1_pipeline_invalidate_masks();
                }
            } else if (encoder_idx == 2) {
                if (!shift) {
                    /* Division (0..T1_DIV_COUNT-1) */
                    int16_t v = (int16_t)cp->division + steps;
                    if (v < 0) v = 0;
                    if (v >= T1_DIV_COUNT) v = T1_DIV_COUNT - 1;
                    cp->division = (uint8_t)v;
                } else {
                    /* Gate Length / Sustain (1..255%) */
                    int16_t v = (int16_t)cp->sustain + steps * 5;
                    if (v < 1) v = 1;
                    if (v > 255) v = 255;
                    cp->sustain = (uint8_t)v;
                }
            } else if (encoder_idx == 3) {
                if (!shift) {
                    /* Active Cycle Selection (0..5) */
                    int16_t v = (int16_t)trk->active_cycle + steps;
                    if (v < 0) v = 0;
                    if (v >= NUM_CYCLES) v = NUM_CYCLES - 1;
                    trk->active_cycle = (uint8_t)v;
                    t1_ui_state.selected_cycle = (uint8_t)v;
                    t1_pipeline_invalidate_masks();
                } else {
                    /* Step Mod: Toggle bits in manual mask */
                    cp->active_mask_lo ^= (1u << (t1_abs(steps) % 32));
                    t1_pipeline_invalidate_masks();
                }
            }
            break;

        /* ---------------- Page 2: Ratchet --- */
        case T1_PAGE_RATCHET:
            if (encoder_idx == 0) {
                if (!shift) {
                    /* Repeats (1..16) */
                    int16_t v = (int16_t)cp->repeats + steps;
                    if (v < 1) v = 1;
                    if (v > 16) v = 16;
                    cp->repeats = (uint8_t)v;
                } else {
                    /* Offset: velocity slope (-64..+63) */
                    int16_t v = (int16_t)cp->offset + steps;
                    if (v < -64) v = -64;
                    if (v > 63) v = 63;
                    cp->offset = (int8_t)v;
                }
            } else if (encoder_idx == 1) {
                if (!shift) {
                    /* Time: gate density (1..127) */
                    int16_t v = (int16_t)cp->time + steps * 2;
                    if (v < 1) v = 1;
                    if (v > 127) v = 127;
                    cp->time = (uint8_t)v;
                } else {
                    /* Pace: log/exp curve (-64..+63) */
                    int16_t v = (int16_t)cp->pace + steps;
                    if (v < -64) v = -64;
                    if (v > 63) v = 63;
                    cp->pace = (int8_t)v;
                }
            } else if (encoder_idx == 2) {
                if (!shift) {
                    /* Base Velocity (1..127) */
                    int16_t v = (int16_t)cp->velocity + steps;
                    if (v < 1) v = 1;
                    if (v > 127) v = 127;
                    cp->velocity = (uint8_t)v;
                } else {
                    /* Accent Delta (0..127) */
                    int16_t v = (int16_t)cp->accent + steps;
                    if (v < 0) v = 0;
                    if (v > 127) v = 127;
                    cp->accent = (uint8_t)v;
                }
            } else if (encoder_idx == 3) {
                if (!shift) {
                    /* Probability (0..100%) */
                    int16_t v = (int16_t)cp->probability + steps * 2;
                    if (v < 0) v = 0;
                    if (v > 100) v = 100;
                    cp->probability = (uint8_t)v;
                } else {
                    /* Groove Template (0..15) */
                    int16_t v = (int16_t)cp->groove_id + steps;
                    if (v < 0) v = 0;
                    if (v > 15) v = 15;
                    cp->groove_id = (uint8_t)v;
                }
            }
            break;

        /* ---------------- Page 3: Harmony --- */
        case T1_PAGE_HARMONY:
            if (encoder_idx == 0) {
                if (!shift) {
                    /* Pitch Offset (-48..+48) */
                    int16_t v = (int16_t)cp->pitch_offset + steps;
                    if (v < -48) v = -48;
                    if (v > 48) v = 48;
                    cp->pitch_offset = (int8_t)v;
                } else {
                    /* Master Transpose (0..11) */
                    int16_t v = (int16_t)pat->master_root + steps;
                    v = ((v % 12) + 12) % 12;
                    pat->master_root = (uint8_t)v;
                }
            } else if (encoder_idx == 1) {
                if (!shift) {
                    /* Scale Root Note (0..11) */
                    int16_t v = (int16_t)cp->root_note + steps;
                    v = ((v % 12) + 12) % 12;
                    cp->root_note = (uint8_t)v;
                } else {
                    /* Scale Selection (0..15) */
                    int16_t v = (int16_t)cp->scale_id + steps;
                    if (v < 0) v = 0;
                    if (v > 15) v = 15;
                    cp->scale_id = (uint8_t)v;
                }
            } else if (encoder_idx == 2) {
                if (!shift) {
                    /* Voicing Style (0..11) */
                    int16_t v = (int16_t)cp->voicing + steps;
                    if (v < 0) v = 0;
                    if (v > 11) v = 11;
                    cp->voicing = (uint8_t)v;
                } else {
                    /* Arp Style Direction (0..7) */
                    int16_t v = (int16_t)cp->style + steps;
                    if (v < 0) v = 0;
                    if (v > 7) v = 7;
                    cp->style = (uint8_t)v;
                }
            } else if (encoder_idx == 3) {
                if (!shift) {
                    /* Melodic Phrase Contour (0..7) */
                    int16_t v = (int16_t)cp->phrase + steps;
                    if (v < 0) v = 0;
                    if (v > 7) v = 7;
                    cp->phrase = (uint8_t)v;
                } else {
                    /* Excursion Range (0..127) */
                    int16_t v = (int16_t)cp->range + steps;
                    if (v < 0) v = 0;
                    if (v > 127) v = 127;
                    cp->range = (uint8_t)v;
                }
            }
            break;

        /* ---------------- Page 4: Mod / CC --- */
        case T1_PAGE_MOD_CC:
            if (encoder_idx == 0) {
                if (!shift) {
                    /* Random Targets Bitmask */
                    cp->random_targets = (uint16_t)(cp->random_targets ^ (1u << (t1_abs(steps) % 8)));
                } else {
                    /* MIDI Channel (0..15) */
                    int16_t v = (int16_t)trk->midi_channel + steps;
                    if (v < 0) v = 0;
                    if (v > 15) v = 15;
                    trk->midi_channel = (uint8_t)v;
                }
            } else if (encoder_idx == 1) {
                if (!shift) {
                    /* Random Depth (0..127) */
                    int16_t v = (int16_t)cp->random_depth + steps;
                    if (v < 0) v = 0;
                    if (v > 127) v = 127;
                    cp->random_depth = (uint8_t)v;
                } else {
                    /* Random Rate (0..15) */
                    int16_t v = (int16_t)cp->random_rate + steps;
                    if (v < 0) v = 0;
                    if (v > 15) v = 15;
                    cp->random_rate = (uint8_t)v;
                }
            } else if (encoder_idx == 2) {
                if (!shift) {
                    /* CC Number (0..127) */
                    int16_t v = (int16_t)cp->cc_target + steps;
                    if (v < 0) v = 0;
                    if (v > 127) v = 127;
                    cp->cc_target = (uint8_t)v;
                } else {
                    /* Delay Nudge (-12..+12 ticks) */
                    int16_t v = (int16_t)cp->delay_ticks + steps;
                    if (v < -12) v = -12;
                    if (v > 12) v = 12;
                    cp->delay_ticks = (int8_t)v;
                }
            } else if (encoder_idx == 3) {
                if (!shift) {
                    /* CC Base Value (0..127) */
                    int16_t v = (int16_t)cp->cc_value + steps;
                    if (v < 0) v = 0;
                    if (v > 127) v = 127;
                    cp->cc_value = (uint8_t)v;
                } else {
                    /* Polyphonic Chord Size (1..4 voices) */
                    int16_t v = (int16_t)cp->chord_size + steps;
                    if (v < 1) v = 1;
                    if (v > 4) v = 4;
                    cp->chord_size = (uint8_t)v;
                }
            }
            break;

        default:
            break;
    }
}

/* ---------------- Screen Rendering Metadata API --- */

void t1_ui_get_header_string(char *dst, uint32_t max_len)
{
    if (!dst || max_len == 0) return;

    const char *page_title = "RHYTHM";
    switch (t1_ui_state.current_page) {
        case T1_PAGE_RHYTHM:  page_title = "RHYTHM"; break;
        case T1_PAGE_RATCHET: page_title = "RATCHET"; break;
        case T1_PAGE_HARMONY: page_title = "HARMONY"; break;
        case T1_PAGE_MOD_CC:  page_title = "MOD/CC"; break;
        default: break;
    }

    const char *mode_tag = "";
    if (t1_ui_state.temp_active) {
        mode_tag = " [TEMP]";
    } else if (t1_ui_state.global_active) {
        mode_tag = " [GLOB]";
    } else if (t1_ui_state.shift_active) {
        mode_tag = " [SHFT]";
    }

    if (t1_ui_state.edit_step != 0xFF) {
        snprintf(dst, max_len, "T%u:C%u [P-LOCK:%u] %s%s",
                 t1_ui_state.selected_track + 1u,
                 t1_ui_state.selected_cycle + 1u,
                 t1_ui_state.edit_step + 1u,
                 page_title, mode_tag);
    } else {
        snprintf(dst, max_len, "T%u:C%u %s%s",
                 t1_ui_state.selected_track + 1u,
                 t1_ui_state.selected_cycle + 1u,
                 page_title, mode_tag);
    }
}

void t1_ui_get_card_info(uint8_t col, char *out_label, char *out_val, const char **out_unit, int32_t *out_ratio)
{
    if (col >= 4 || !out_label || !out_val || !out_unit || !out_ratio) return;

    const Pattern *pat = &t1_sequencer_ram.active_pattern;
    const Track *trk = &pat->tracks[t1_ui_state.selected_track];
    uint8_t c_idx = trk->active_cycle % NUM_CYCLES;
    const CycleParams *cp = &trk->cycles[c_idx];
    bool shift = t1_ui_state.shift_active;

    *out_unit = "";
    *out_ratio = -1;

    switch (t1_ui_state.current_page) {
        case T1_PAGE_RHYTHM:
            if (col == 0) {
                if (!shift) {
                    snprintf(out_label, 16, "STEPS");
                    snprintf(out_val, 16, "%u", cp->steps);
                    *out_ratio = (int32_t)cp->steps * 1000 / 64;
                } else {
                    snprintf(out_label, 16, "MAX LEN");
                    if (pat->master_length_override > 0)
                        snprintf(out_val, 16, "%u", pat->master_length_override);
                    else
                        snprintf(out_val, 16, "OFF");
                }
            } else if (col == 1) {
                if (!shift) {
                    snprintf(out_label, 16, "PULSES");
                    snprintf(out_val, 16, "%u", cp->pulses);
                    uint8_t max_p = cp->steps ? cp->steps : 16u;
                    *out_ratio = (int32_t)cp->pulses * 1000 / max_p;
                } else {
                    snprintf(out_label, 16, "ROTATE");
                    snprintf(out_val, 16, "%+d", cp->rotate);
                }
            } else if (col == 2) {
                if (!shift) {
                    snprintf(out_label, 16, "DIVISION");
                    uint8_t div = (cp->division < T1_DIV_COUNT) ? cp->division : 4u;
                    snprintf(out_val, 16, "%s", DIV_NAMES[div]);
                } else {
                    snprintf(out_label, 16, "GATE LEN");
                    snprintf(out_val, 16, "%u", cp->sustain);
                    *out_unit = "%";
                    *out_ratio = (int32_t)cp->sustain * 1000 / 255;
                }
            } else if (col == 3) {
                if (!shift) {
                    snprintf(out_label, 16, "CYCLE");
                    snprintf(out_val, 16, "C%u", trk->active_cycle + 1u);
                    *out_ratio = (int32_t)(trk->active_cycle + 1u) * 1000 / NUM_CYCLES;
                } else {
                    snprintf(out_label, 16, "STEP MOD");
                    snprintf(out_val, 16, "0x%04X", (uint16_t)(cp->active_mask_lo & 0xFFFFu));
                }
            }
            break;

        case T1_PAGE_RATCHET:
            if (col == 0) {
                if (!shift) {
                    snprintf(out_label, 16, "REPEATS");
                    snprintf(out_val, 16, "%u", cp->repeats);
                    *out_ratio = (int32_t)cp->repeats * 1000 / 16;
                } else {
                    snprintf(out_label, 16, "OFFSET");
                    snprintf(out_val, 16, "%+d", cp->offset);
                }
            } else if (col == 1) {
                if (!shift) {
                    snprintf(out_label, 16, "TIME");
                    snprintf(out_val, 16, "%u", cp->time);
                    *out_ratio = (int32_t)cp->time * 1000 / 127;
                } else {
                    snprintf(out_label, 16, "PACE");
                    snprintf(out_val, 16, "%+d", cp->pace);
                }
            } else if (col == 2) {
                if (!shift) {
                    snprintf(out_label, 16, "VELOCITY");
                    snprintf(out_val, 16, "%u", cp->velocity);
                    *out_ratio = (int32_t)cp->velocity * 1000 / 127;
                } else {
                    snprintf(out_label, 16, "ACCENT");
                    snprintf(out_val, 16, "+%u", cp->accent);
                }
            } else if (col == 3) {
                if (!shift) {
                    snprintf(out_label, 16, "PROB");
                    snprintf(out_val, 16, "%u", cp->probability);
                    *out_unit = "%";
                    *out_ratio = (int32_t)cp->probability * 1000 / 100;
                } else {
                    snprintf(out_label, 16, "GROOVE");
                    snprintf(out_val, 16, "G%u", cp->groove_id);
                }
            }
            break;

        case T1_PAGE_HARMONY:
            if (col == 0) {
                if (!shift) {
                    snprintf(out_label, 16, "PITCH");
                    snprintf(out_val, 16, "%+d", cp->pitch_offset);
                    *out_unit = "st";
                } else {
                    snprintf(out_label, 16, "M-TRANS");
                    uint8_t r = pat->master_root % 12u;
                    snprintf(out_val, 16, "%s", NOTE_NAMES[r]);
                }
            } else if (col == 1) {
                if (!shift) {
                    snprintf(out_label, 16, "ROOT");
                    uint8_t r = cp->root_note % 12u;
                    snprintf(out_val, 16, "%s", NOTE_NAMES[r]);
                } else {
                    snprintf(out_label, 16, "SCALE");
                    uint8_t s = (cp->scale_id < 16u) ? cp->scale_id : 0u;
                    snprintf(out_val, 16, "%s", SCALE_NAMES[s]);
                }
            } else if (col == 2) {
                if (!shift) {
                    snprintf(out_label, 16, "VOICING");
                    uint8_t v = (cp->voicing < 12u) ? cp->voicing : 0u;
                    snprintf(out_val, 16, "%s", VOICING_NAMES[v]);
                } else {
                    snprintf(out_label, 16, "STYLE");
                    uint8_t s = (cp->style < 8u) ? cp->style : 0u;
                    snprintf(out_val, 16, "%s", STYLE_NAMES[s]);
                }
            } else if (col == 3) {
                if (!shift) {
                    snprintf(out_label, 16, "PHRASE");
                    uint8_t p = (cp->phrase < 8u) ? cp->phrase : 0u;
                    snprintf(out_val, 16, "%s", PHRASE_NAMES[p]);
                } else {
                    snprintf(out_label, 16, "RANGE");
                    snprintf(out_val, 16, "%u", cp->range);
                    *out_ratio = (int32_t)cp->range * 1000 / 127;
                }
            }
            break;

        case T1_PAGE_MOD_CC:
            if (col == 0) {
                if (!shift) {
                    snprintf(out_label, 16, "RND TGT");
                    snprintf(out_val, 16, "0x%02X", (uint8_t)cp->random_targets);
                } else {
                    snprintf(out_label, 16, "CHANNEL");
                    snprintf(out_val, 16, "CH %u", trk->midi_channel + 1u);
                }
            } else if (col == 1) {
                if (!shift) {
                    snprintf(out_label, 16, "RND DEPTH");
                    snprintf(out_val, 16, "%u", cp->random_depth);
                    *out_ratio = (int32_t)cp->random_depth * 1000 / 127;
                } else {
                    snprintf(out_label, 16, "RND RATE");
                    snprintf(out_val, 16, "/%u", cp->random_rate ? cp->random_rate : 1u);
                }
            } else if (col == 2) {
                if (!shift) {
                    snprintf(out_label, 16, "CC NUM");
                    snprintf(out_val, 16, "%u", cp->cc_target);
                } else {
                    snprintf(out_label, 16, "SLEW/DEL");
                    snprintf(out_val, 16, "%+d", cp->delay_ticks);
                    *out_unit = "tk";
                }
            } else if (col == 3) {
                if (!shift) {
                    snprintf(out_label, 16, "CC VALUE");
                    snprintf(out_val, 16, "%u", cp->cc_value);
                    *out_ratio = (int32_t)cp->cc_value * 1000 / 127;
                } else {
                    snprintf(out_label, 16, "VOICES");
                    uint8_t csize = cp->chord_size ? cp->chord_size : 1u;
                    snprintf(out_val, 16, "%u-VOICE", csize);
                }
            }
            break;

        default:
            break;
    }
}
