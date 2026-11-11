/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: instrument body (modal resonator bank) and output stage
 * (pickup tone, drive, level, limiter).
 *
 * The body is a handful of two-pole resonators in parallel, each one mode of
 * the instrument's box: the air resonance, the top plate, the bridge hill.
 * All the strings of an instrument share one body, so it runs once on the
 * mix, not once per voice.
 *
 * Platform-independent DSP. No allocation, I/O, locks or platform headers.
 */
#ifndef FM1_BODY_MODEL_H
#define FM1_BODY_MODEL_H

#include <stdint.h>

#define PM_BODY_MAX_MODES 6u

typedef struct {
    float frequency_hz;     /* 20 .. 8000 */
    float q;                /* 1 .. 200: how long the mode rings */
    float gain;             /* linear, signed: neighbouring modes may be out of phase */
} pm_body_mode_t;

typedef struct {
    float b0[PM_BODY_MAX_MODES];
    float a1[PM_BODY_MAX_MODES];
    float a2[PM_BODY_MAX_MODES];
    float y1[PM_BODY_MAX_MODES];
    float y2[PM_BODY_MAX_MODES];
    float x1;
    float x2;
    float wet;              /* 0 (string only) .. 1 (through the body); ramped per block */
    float wet_target;
    uint32_t modes;
} pm_body_t;

void pm_body_init(pm_body_t *body);

/* Set the modes (count <= PM_BODY_MAX_MODES; 0 or NULL: no body). The modes'
 * ringing is cleared. Call from the task that owns the body. */
void pm_body_configure(pm_body_t *body, const pm_body_mode_t *modes, uint32_t count);

/* How much of the sound goes through the body, 0 .. 1; reached over the next block. */
void pm_body_set_wet(pm_body_t *body, float wet);

/* In place. */
void pm_body_process(pm_body_t *body, float *mono, uint32_t frames);

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
