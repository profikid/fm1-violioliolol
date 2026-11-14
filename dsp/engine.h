/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: the instrument engine.
 *
 * Twelve preallocated voices, each a plucked or a bowed string; a
 * deterministic allocator with click-safe stealing; the MIDI performance
 * controls (velocity, pitch bend, CC1, CC7, CC11, CC64, channel pressure);
 * the body, a room, and the output stage with headroom and a limiter. Each
 * note varies a little (where it is plucked, how hard, a few cents), as a
 * player's do; the variation is deterministic.
 *
 * Two contexts use it:
 *   the audio task   owns every bit of live DSP state. It calls
 *                    pm_engine_render, and may call the pm_engine_* functions
 *                    marked "audio" below (MIDI read in the render).
 *   the control task (the UI) only posts: pm_engine_post and
 *                    pm_engine_post_patch put a message in a bounded
 *                    single-producer queue the render drains at the start of
 *                    a block. It never touches a voice.
 *
 * Nothing here allocates, blocks, logs or does I/O; every loop is bounded.
 * Platform-independent: no platform headers.
 */
#ifndef FM1_ENGINE_H
#define FM1_ENGINE_H

#include <stdint.h>
#include "string_model.h"
#include "bowed_model.h"
#include "body_model.h"
#include "instrument.h"

#define PM_MAX_VOICES 12u
#define PM_ENGINE_BLOCK 256u
#define PM_QUEUE_SIZE 64u               /* a power of two */
#define PM_FADE_FRAMES 96u              /* a stolen voice fades out over this: 2.2 ms */
#define PM_MIX_HEADROOM 0.3f            /* every voice is mixed in at this gain */
#define PM_DEFAULT_POLYPHONY 8u
#define PM_DEFAULT_ROOM 34u             /* a small room, lightly */
#define PM_DEFAULT_ROOM_SIZE 48u
#define PM_DEFAULT_ROOM_TONE 64u
#define PM_DEFAULT_HUMAN 40u

enum { PM_VOICE_IDLE, PM_VOICE_HELD, PM_VOICE_SUSTAINED, PM_VOICE_RELEASED, PM_VOICE_STEALING };

/* Messages from the control task. a: 16 bits, b: 8 bits. */
enum {
    PM_EV_NOTE_ON = 1,      /* a note, b velocity 1..127 */
    PM_EV_NOTE_OFF,         /* a note */
    PM_EV_PARAM,            /* a PM_P_*, b value 0..127: the patch's own value */
    PM_EV_CONTROL,          /* a controller number, b value: as MIDI's */
    PM_EV_BEND,             /* a 0..16383, 8192 centre */
    PM_EV_PRESSURE,         /* b 0..127 */
    PM_EV_PATCH,            /* the patch given to pm_engine_post_patch */
    PM_EV_ALL_OFF,          /* every voice fades out now */
    PM_EV_POLYPHONY,        /* b 1..PM_MAX_VOICES */
    PM_EV_BENCH,            /* b 0..PM_MAX_VOICES: that many voices held and kept running (0: off) */
    PM_EV_SHED,             /* the render is over budget: one voice and one of polyphony less */
    PM_EV_BEND_RANGE,       /* b 1..24 semitones */
    PM_EV_ROOM              /* a 0 amount, 1 size, 2 tone, 3 humanize; b 0..127 */
};

typedef struct {
    union {
        pm_string_voice_t string;
        pm_bowed_voice_t bowed;
    } model;
    uint32_t age;           /* the allocation count when it started: the oldest has the smallest */
    uint32_t seed;
    uint32_t fade_left;     /* frames of fade still to go while stealing */
    uint32_t quiet_blocks;
    float base_note;        /* the note it plays, with a course's detune */
    uint8_t state;          /* PM_VOICE_* */
    uint8_t engine;         /* the model in the union: PM_ENGINE_*, or 255 before the first note */
    uint8_t note;
    uint8_t string;         /* which string of a course */
    uint8_t pending;        /* a note waits for the fade to end */
    uint8_t pending_note;
    uint8_t pending_velocity;
    uint8_t pending_string;
    uint8_t pending_off;    /* and it was already let go */
    uint8_t bench;          /* a benchmark voice: no allocation, no stealing */
} pm_engine_voice_t;

typedef struct {
    pm_engine_voice_t voice[PM_MAX_VOICES];
    pm_body_t body;
    pm_room_t room;
    pm_output_t output;
    float scratch[PM_ENGINE_BLOCK];
    pm_patch_t patch;                   /* the playing patch: the audio task's */
    uint8_t value[PM_PARAM_COUNT];      /* its values as the controllers have moved them */
    pm_patch_t patch_pending;           /* the control task's hand-over */
    volatile uint8_t patch_lock;
    volatile uint32_t queue[PM_QUEUE_SIZE];
    volatile uint32_t queue_head;       /* the control task writes */
    volatile uint32_t queue_tail;       /* the audio task reads */
    uint32_t age_counter;
    float bend_ratio;
    float vibrato_phase;
    float last_ratio;
    uint16_t bend;                      /* 0..16383 */
    uint8_t mod;
    uint8_t pressure;
    uint8_t volume;
    uint8_t expression;
    uint8_t sustain;
    uint8_t room_amount;                /* 0..127 each: the room, and how much each note varies */
    uint8_t room_size;
    uint8_t room_tone;
    uint8_t human;
    uint32_t human_rng;
    uint8_t polyphony;
    uint8_t bench;
    uint8_t dirty;                      /* the values changed: push them to the models */
    /* read by the control task (single bytes and words: no tearing) */
    volatile uint8_t active_voices;
    volatile uint8_t polyphony_now;
    volatile uint8_t limiter_q8;        /* the limiter's gain in the last block, 255 = not limiting */
    volatile uint8_t program_request;   /* a MIDI program change: its number + 1, 0 none; the UI clears it */
    volatile uint32_t steals;
    volatile uint32_t sheds;
    volatile uint32_t faults;           /* voices stopped because their output left the bounds */
    volatile uint32_t dropped;          /* messages lost to a full queue */
    volatile uint32_t midi_params;      /* bit PM_P_*: MIDI changed the patch's value (CC16..23) */
} pm_engine_t;

/* Control context, before the audio starts. */
void pm_engine_init(pm_engine_t *engine, uint32_t seed);

/* ---- the control task ---- */
/* 0 = queued; -1 = the queue is full (counted in dropped). */
int pm_engine_post(pm_engine_t *engine, uint32_t type, uint32_t a, uint32_t b);
/* Hand a patch over; the render takes it at its next block. 0 = queued. */
int pm_engine_post_patch(pm_engine_t *engine, const pm_patch_t *patch);

/* ---- the audio task ---- */
/* One block: drain the queue, render the voices, body, output stage. out:
 * mono, every sample finite and within -1 .. 1. frames: any count. */
void pm_engine_render(pm_engine_t *engine, float *out, uint32_t frames);
/* A MIDI channel message, as it arrived (status with its channel, two data bytes). */
void pm_engine_midi(pm_engine_t *engine, uint32_t status, uint32_t data1, uint32_t data2);
void pm_engine_note_on(pm_engine_t *engine, uint32_t note, uint32_t velocity);
void pm_engine_note_off(pm_engine_t *engine, uint32_t note);
void pm_engine_control(pm_engine_t *engine, uint32_t controller, uint32_t value);
void pm_engine_set_patch(pm_engine_t *engine, const pm_patch_t *patch);
void pm_engine_all_off(pm_engine_t *engine);
/* The render ran over its budget: fade the voice a new note would take, and
 * one of polyphony less (never below two). */
void pm_engine_shed(pm_engine_t *engine);

uint32_t pm_engine_bytes(void);
uint32_t pm_engine_voice_slot_bytes(void);

#endif
