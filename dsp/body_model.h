/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: instrument body (modal resonator bank) and output stage
 * (pickup tone, drive, level, limiter).
 *
 * The body is a bank of two-pole resonators in parallel, each one mode of the
 * instrument's box. The instrument names its signature modes (the air
 * resonance, the top plate, the bridge hill); above the lowest of them the
 * bank is filled with a dense cloud of quickly damped modes, shaped by those
 * signature modes, as a wooden box's response is dense rather than a few
 * ringing tones. The whole is normalised to unit power gain, so how much body
 * changes the colour, not the loudness.
 * All the strings of an instrument share one body, so it runs once on the
 * mix, not once per voice.
 *
 * Platform-independent DSP. No allocation, I/O, locks or platform headers.
 */
#ifndef FM1_BODY_MODEL_H
#define FM1_BODY_MODEL_H

#include <stdint.h>

#define PM_BODY_MAX_MODES 6u            /* the signature modes an instrument names */
#define PM_BODY_BANK 24u               /* resonators in all: the signature modes and the cloud */

typedef struct {
    float frequency_hz;     /* 20 .. 8000 */
    float q;                /* 1 .. 200: how long the mode rings */
    float gain;             /* linear, signed: neighbouring modes may be out of phase */
} pm_body_mode_t;

typedef struct {
    float b0[PM_BODY_BANK];
    float a1[PM_BODY_BANK];
    float a2[PM_BODY_BANK];
    float y1[PM_BODY_BANK];
    float y2[PM_BODY_BANK];
    float x1;
    float x2;
    float wet;              /* 0 (string only) .. 1 (through the body); ramped per block */
    float wet_target;
    float norm;             /* the bank's output to unit power gain */
    uint32_t modes;         /* resonators in use, signature and cloud */
    uint32_t signature;     /* of which the first this many are the instrument's own */
} pm_body_t;

void pm_body_init(pm_body_t *body);

/* Set the signature modes (count <= PM_BODY_MAX_MODES; 0 or NULL: no body) and
 * fill the rest of the bank from them. The ringing is cleared. Bounded work
 * (a few thousand operations): call from the task that owns the body. */
void pm_body_configure(pm_body_t *body, const pm_body_mode_t *modes, uint32_t count);

/* How much of the sound goes through the body, 0 .. 1; reached over the next block. */
void pm_body_set_wet(pm_body_t *body, float wet);

/* In place. */
void pm_body_process(pm_body_t *body, float *mono, uint32_t frames);

/* The room: a feedback delay network of eight lines, Householder-mixed, each
 * with a lowpass for the air's and the walls' damping. A small room, not a
 * hall: about 13 to 36 ms of line, decaying in 0.25 .. 2.2 s. Without one,
 * every recording a string is compared against had one, and the dry model
 * sounds like a model. */
#define PM_ROOM_LINES 8u
#define PM_ROOM_CAPACITY 2048u          /* per line, a power of two */

typedef struct {
    float line[PM_ROOM_LINES][PM_ROOM_CAPACITY];
    float damp[PM_ROOM_LINES];          /* the lowpasses' states */
    float gain[PM_ROOM_LINES];          /* each line's feedback, from its length and the decay */
    uint32_t length[PM_ROOM_LINES];
    uint32_t index;
    float tone;                         /* lowpass coefficient: 1 open, lower is darker */
    float wet;                          /* ramped per block to wet_target */
    float wet_target;
} pm_room_t;

void pm_room_init(pm_room_t *room);
/* amount, size, tone: 0 .. 1 each. Size sets the lines' lengths and the decay
 * and clears the room (a new room); amount and tone change smoothly. */
void pm_room_set(pm_room_t *room, float amount, float size, float tone);
/* In place: the dry signal, plus the room. */
void pm_room_process(pm_room_t *room, float *mono, uint32_t frames);

/* The output stage: pickup tone (a one-pole lowpass), drive (a bounded soft
 * clipper), level, then a limiter that keeps the result inside -1 .. 1. */
typedef struct {
    float tone;             /* lowpass coefficient, 0 .. 1 (1: open) */
    float tone_state;
    float drive;            /* 0 (clean) .. 1 */
    float drive_gain;
    float level;            /* current gain; ramped per block to level_target */
    float level_target;
    float envelope;         /* the limiter's peak follower */
    float reduction;        /* its smallest gain in the last block, 0 .. 1 (1: not limiting) */
} pm_output_t;

#define PM_OUTPUT_THRESHOLD 0.85f   /* the limiter starts here; the output never exceeds 1 */

void pm_output_init(pm_output_t *out);
void pm_output_set_tone(pm_output_t *out, float tone);     /* 0 (dark) .. 1 (open) */
void pm_output_set_drive(pm_output_t *out, float drive);   /* 0 .. 1 */
void pm_output_set_level(pm_output_t *out, float level);   /* linear gain, 0 .. 4 */

/* In place; every output sample is finite and within -1 .. 1. */
void pm_output_process(pm_output_t *out, float *mono, uint32_t frames);

#endif
