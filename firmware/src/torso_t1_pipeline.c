/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Torso T-1 Architecture - Phase 3: Dual Output Routing & Event Pipeline Implementation */

#include "torso_t1_pipeline.h"

#if !defined(FELUCCA_ID) && !defined(FM1_SYS_H)
#include <string.h>
#else
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
#endif

/* Global runtime instances */
T1VoiceAllocator t1_voice_allocator;
T1TrackRuntime   t1_track_runtimes[NUM_TRACKS];

/* ---------------- Division Timing in 192 PPQN Ticks --- */
uint32_t t1_division_ticks(uint8_t division)
{
    switch (division) {
        case T1_DIV_1_1:   return 192u * 4u; /* 1/1 Whole Note = 768 ticks */
        case T1_DIV_1_2:   return 192u * 2u; /* 1/2 Half Note = 384 ticks */
        case T1_DIV_1_4:   return 192u;      /* 1/4 Quarter Note = 192 ticks */
        case T1_DIV_1_8:   return 96u;       /* 1/8 Eighth Note = 96 ticks */
        case T1_DIV_1_16:  return 48u;       /* 1/16 Sixteenth Note = 48 ticks */
        case T1_DIV_1_32:  return 24u;       /* 1/32 Thirty-second Note = 24 ticks */
        case T1_DIV_1_8T:  return 64u;       /* 1/8 Triplet = 64 ticks (3 per 1/4 note) */
        case T1_DIV_1_16T: return 32u;       /* 1/16 Triplet = 32 ticks (3 per 1/8 note) */
        case T1_DIV_1_32T: return 16u;       /* 1/32 Triplet = 16 ticks (3 per 1/16 note) */
        case T1_DIV_1_8D:  return 144u;      /* 1/8 Dotted = 144 ticks */
        case T1_DIV_1_16D: return 72u;       /* 1/16 Dotted = 72 ticks */
        default:           return 48u;       /* Default 1/16 */
    }
}

/* Calculate step duration applying swing (50% to 75%) */
static inline uint32_t t1_calc_swung_len(uint32_t base_len, uint8_t swing, uint8_t step_idx)
{
    if (swing <= 50u) {
        return base_len;
    }
    if (swing > 75u) {
        swing = 75u;
    }
    /* Delta swing in ticks: swing range 50..75 maps to 0..25% shift */
    uint32_t delta = (base_len * (uint32_t)(swing - 50u)) / 100u;
    if ((step_idx & 1u) == 0) {
        return base_len + delta; /* Even step lengthened */
    } else {
        return (base_len > delta) ? (base_len - delta) : 1u; /* Odd step shortened */
    }
}

/* ---------------- Enhanced 6-Voice FM Polyphonic Allocator --- */
void t1_pipeline_kill_all_voices(void)
{
    for (uint8_t i = 0; i < T1_FM_POLYPHONY; i++) {
        if (t1_voice_allocator.slots[i].active) {
            synth_note_off(i);
            t1_voice_allocator.slots[i].active = false;
        }
    }
}

int8_t t1_pipeline_free_voice(uint8_t note)
{
    for (uint8_t i = 0; i < T1_FM_POLYPHONY; i++) {
        if (t1_voice_allocator.slots[i].active && t1_voice_allocator.slots[i].note == note) {
            t1_voice_allocator.slots[i].active = false;
            t1_voice_allocator.total_note_offs++;
            return (int8_t)i;
        }
    }
    return -1;
}

int8_t t1_pipeline_alloc_voice(uint8_t note, uint8_t velocity)
{
    t1_voice_allocator.voice_age_counter++;
    t1_voice_allocator.total_note_ons++;

    /* 1. Retrigger check: reuse voice already playing this exact note */
    for (uint8_t i = 0; i < T1_FM_POLYPHONY; i++) {
        if (t1_voice_allocator.slots[i].active && t1_voice_allocator.slots[i].note == note) {
            t1_voice_allocator.slots[i].velocity = velocity;
            t1_voice_allocator.slots[i].age      = t1_voice_allocator.voice_age_counter;
            return (int8_t)i;
        }
    }

    /* 2. Find any free voice */
    for (uint8_t i = 0; i < T1_FM_POLYPHONY; i++) {
        if (!t1_voice_allocator.slots[i].active) {
            t1_voice_allocator.slots[i].active   = true;
            t1_voice_allocator.slots[i].note     = note;
            t1_voice_allocator.slots[i].velocity = velocity;
            t1_voice_allocator.slots[i].age      = t1_voice_allocator.voice_age_counter;
            return (int8_t)i;
        }
    }

    /* 3. Voice Stealing: Steal lowest velocity (or oldest note) */
    uint8_t steal_idx = 0;
    uint8_t lowest_vel = t1_voice_allocator.slots[0].velocity;
    uint32_t oldest_age = t1_voice_allocator.slots[0].age;

    for (uint8_t i = 1; i < T1_FM_POLYPHONY; i++) {
        if (t1_voice_allocator.slots[i].velocity < lowest_vel ||
           (t1_voice_allocator.slots[i].velocity == lowest_vel && t1_voice_allocator.slots[i].age < oldest_age)) {
            steal_idx  = i;
            lowest_vel = t1_voice_allocator.slots[i].velocity;
            oldest_age = t1_voice_allocator.slots[i].age;
        }
    }

    /* Clean release of stolen voice before reusing */
    synth_note_off(steal_idx);
    t1_voice_allocator.voice_steals++;

    t1_voice_allocator.slots[steal_idx].active   = true;
    t1_voice_allocator.slots[steal_idx].note     = note;
    t1_voice_allocator.slots[steal_idx].velocity = velocity;
    t1_voice_allocator.slots[steal_idx].age      = t1_voice_allocator.voice_age_counter;
    return (int8_t)steal_idx;
}

/* ---------------- Pipeline Initialization --- */
void t1_pipeline_init(void)
{
    memset(&t1_voice_allocator, 0, sizeof(t1_voice_allocator));
    memset(t1_track_runtimes, 0, sizeof(t1_track_runtimes));

    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        t1_lfsr_init(&t1_track_runtimes[t].lfsr, (uint16_t)(0x1234u + t * 0x1111u), 0);
        t1_track_runtimes[t].current_step_len = 48u; /* Default 1/16 */
        t1_track_runtimes[t].tick_accum       = 0;
        t1_track_runtimes[t].step_index       = 0xFF; /* Evaluate step 0 on tick 0 */
    }
}

void t1_pipeline_reset_transport(void)
{
    t1_pipeline_kill_all_voices();
    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        t1_track_runtimes[t].step_index    = 0xFF;
        t1_track_runtimes[t].pulse_counter = 0;
        t1_track_runtimes[t].cycle_pass    = 0;
        t1_track_runtimes[t].tick_accum    = 0;
    }
}

void t1_pipeline_invalidate_masks(void)
{
    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        t1_track_runtimes[t].mask_valid = false;
    }
}

/* ---------------- Master 192 PPQN Pipeline Step Evaluator --- */
void t1_pipeline_tick(uint32_t current_tick)
{
    const Pattern *pat = &t1_sequencer_ram.active_pattern;

    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        const Track *trk = &pat->tracks[t];
        if (trk->muted) {
            continue;
        }

        uint8_t c_idx = trk->active_cycle % NUM_CYCLES;
        const CycleParams *cp = &trk->cycles[c_idx];
        T1TrackRuntime *rt = &t1_track_runtimes[t];

        /* Update cached Euclidean mask if invalid */
        if (!rt->mask_valid) {
            rt->active_mask = t1_euclidean_eval_cycle(cp);
            rt->mask_valid  = true;
        }

        /* Check if starting step 0 or current step duration has completed */
        if (rt->step_index == 0xFF) {
            rt->step_index = 0;
            uint32_t base_len = t1_division_ticks(cp->division);
            rt->current_step_len = t1_calc_swung_len(base_len, pat->swing, 0);
            rt->tick_accum = 0;
        } else if (rt->tick_accum >= rt->current_step_len) {
            uint8_t total_steps = cp->steps ? cp->steps : 16u;
            rt->step_index = (uint8_t)((rt->step_index + 1u) % total_steps);

            if (rt->step_index == 0) {
                rt->cycle_pass++;
                t1_lfsr_on_cycle_boundary(&rt->lfsr);
            }

            /* Calculate next step length with division and pattern swing */
            uint32_t base_len = t1_division_ticks(cp->division);
            rt->current_step_len = t1_calc_swung_len(base_len, pat->swing, rt->step_index);
            rt->tick_accum = 0;
        } else {
            rt->tick_accum++;
            continue;
        }

        uint8_t total_steps = cp->steps ? cp->steps : 16u;

        /* Check if this step is a Euclidean pulse */
        bool has_pulse = (rt->active_mask & (1ULL << rt->step_index)) != 0;

        if (has_pulse) {
            /* 1. Stochastic Probability Check */
            uint8_t prob = cp->probability;
            int16_t locked_prob;
            if (t1_plock_get(cp, rt->step_index, 68 /* P_PROB */, &locked_prob)) {
                prob = (uint8_t)locked_prob;
            }

            bool trigger_pulse = true;
            if (prob < 100u) {
                uint16_t roll = t1_lfsr_next(&rt->lfsr) % 100u;
                if (roll >= prob) {
                    trigger_pulse = false;
                }
            }

            if (trigger_pulse) {
                /* 2. Pitch & Voicing Evaluation */
                int16_t note = 60; /* Middle C base */
                note += cp->pitch_offset;

                /* Melodic phrase contour */
                note += t1_harmony_eval_phrase(cp->phrase, cp->range, rt->step_index, total_steps);

                /* Arpeggio style direction */
                note += (int16_t)t1_harmony_eval_style(cp->style, rt->pulse_counter, 7u);

                /* Master Harmony Link: project relative to pattern master root */
                uint8_t root = cp->root_note;
                uint8_t scale = cp->scale_id;
                if (pat->master_scale > 0) {
                    root = pat->master_root;
                    scale = pat->master_scale;
                }

                /* Quantize to musical scale */
                uint8_t q_note = t1_harmony_quantize(note, root, scale);

                /* Multi-voice chord count (1..4 voices) */
                uint8_t chord_cnt = cp->chord_size ? cp->chord_size : 1u;
                if (chord_cnt > 4u) chord_cnt = 4u;

                /* 4. Velocity & Accent */
                uint8_t vel = cp->velocity ? cp->velocity : 100u;
                int16_t locked_vel;
                if (t1_plock_get(cp, rt->step_index, 67 /* P_VEL */, &locked_vel)) {
                    vel = (uint8_t)locked_vel;
                }

                /* 5. Non-linear Ratchet & Pace Evaluation */
                T1RatchetPattern rat;
                t1_pace_eval_ratchet(cp, rt->current_step_len, &rat);

                /* Micro-delay offset (-12 to +12 ticks) */
                int32_t delay = (int32_t)cp->delay_ticks;

                /* Schedule all repeats into the Micro-Queue */
                uint8_t channel = trk->midi_channel;

                for (uint8_t k = 0; k < rat.repeats; k++) {
                    int32_t on_time = (int32_t)current_tick + delay + (int32_t)rat.tick_offsets[k];
                    if (on_time < (int32_t)current_tick) on_time = (int32_t)current_tick;

                    uint32_t gate_len = rat.gate_ticks[k];
                    if (cp->sustain > 0) {
                        gate_len = (gate_len * (uint32_t)cp->sustain) / 100u;
                    }
                    if (gate_len == 0) gate_len = 1u;

                    uint32_t off_time = (uint32_t)on_time + gate_len;

                    /* Arpeggio style direction advances per repeat */
                    int16_t rep_base = note;
                    if (cp->style > 0) {
                        rep_base += (int16_t)t1_harmony_eval_style(cp->style, (uint32_t)(rt->pulse_counter * rat.repeats + k), 7u);
                    }
                    uint8_t rep_q_note = t1_harmony_quantize(rep_base, root, scale);

                    uint8_t rep_chord_notes[4];
                    t1_harmony_eval_chord(root, scale, rep_q_note, chord_cnt, cp->voicing, rep_chord_notes);

                    /* Parameter Lock Overrides for Note 0 */
                    int16_t locked_note;
                    if (t1_plock_get(cp, rt->step_index, 57 /* P_PITCH */, &locked_note)) {
                        rep_chord_notes[0] = (uint8_t)locked_note;
                    }

                    for (uint8_t v = 0; v < chord_cnt; v++) {
                        /* Note-On Event */
                        T1MicroEvent ev_on = {
                            .tick      = (uint32_t)on_time,
                            .sub_tick  = rat.sub_ticks[k],
                            .track     = t,
                            .type      = T1_EVENT_NOTE_ON,
                            .note      = rep_chord_notes[v],
                            .velocity  = rat.velocities[k],
                            .channel   = channel,
                            .cc_num    = 0,
                            .cc_val    = 0
                        };
                        t1_micro_queue_push(&ev_on);

                        /* Note-Off Event */
                        T1MicroEvent ev_off = {
                            .tick      = off_time,
                            .sub_tick  = 0,
                            .track     = t,
                            .type      = T1_EVENT_NOTE_OFF,
                            .note      = rep_chord_notes[v],
                            .velocity  = 0,
                            .channel   = channel,
                            .cc_num    = 0,
                            .cc_val    = 0
                        };
                        t1_micro_queue_push(&ev_off);
                    }
                }


                /* 6. MIDI CC Event */
                if (cp->cc_target > 0) {
                    int32_t cc_val = cp->cc_value;
                    /* Optional LFSR CC modulation */
                    cc_val = t1_lfsr_modulate(&rt->lfsr, cp->random_targets, T1_RND_TARGET_CC,
                                              cc_val, cp->random_depth, 0, 127);
                    T1MicroEvent ev_cc = {
                        .tick      = current_tick,
                        .sub_tick  = 0,
                        .track     = t,
                        .type      = T1_EVENT_CC,
                        .note      = 0,
                        .velocity  = 0,
                        .channel   = channel,
                        .cc_num    = cp->cc_target,
                        .cc_val    = (uint8_t)cc_val
                    };
                    t1_micro_queue_push(&ev_cc);
                }

                rt->pulse_counter++;
            }
        }

        rt->tick_accum++;
    }


    /* ---------------- Note & CC Dispatcher (Dual Output Routing) --- */
    while (t1_micro_queue.head != t1_micro_queue.tail) {
        const T1MicroEvent *evt = &t1_micro_queue.events[t1_micro_queue.tail];

        /* Only process events that are due */
        if (evt->tick > current_tick) {
            break;
        }

        if (evt->track == 0) {
            /*
             * Track 0 (Default): Direct C-Call to FM Core
             * Bypasses MIDI serialization overhead completely.
             */
            if (evt->type == T1_EVENT_NOTE_ON) {
                int8_t voice = t1_pipeline_alloc_voice(evt->note, evt->velocity);
                if (voice >= 0) {
                    synth_note_on((uint8_t)voice, evt->note, evt->velocity);
                }
            } else if (evt->type == T1_EVENT_NOTE_OFF) {
                int8_t voice = t1_pipeline_free_voice(evt->note);
                if (voice >= 0) {
                    synth_note_off((uint8_t)voice);
                }
            }
        } else {
            /*
             * Tracks 1 through 5: Non-blocking Queue to Serial/USB MIDI
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
    }
}

/* ---------------- MIDI Worker Output Flush Routine --- */
void t1_pipeline_flush_midi(void (*uart_out)(uint8_t byte), void (*usb_out)(uint32_t packet))
{
    t1_midi_worker_flush_uart(uart_out);
    t1_midi_worker_flush_usb(usb_out);
}
