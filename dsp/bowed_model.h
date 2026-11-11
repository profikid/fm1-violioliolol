/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: nonlinear bowed-string voice.
 *
 * A digital waveguide split at the bow: one delay line from the bow to the
 * nut and back, one from the bow to the bridge and back. At the bow a
 * bounded friction curve decides, each sample, how much of the difference
 * between the bow's velocity and the string's is passed to the string: the
 * string sticks to the bow, slips, and sticks again (stick-slip), which is
 * what sustains the tone. After J. O. Smith's bowed-string waveguide and its
 * STK realisation; the friction curve is the usual bounded approximation
 * (|x| + 0.75)^-4.
 *
 * Platform-independent DSP. No allocation, I/O, locks or platform headers.
 */
#ifndef FM1_BOWED_MODEL_H
#define FM1_BOWED_MODEL_H

#include <stdint.h>

#define PM_BOWED_NECK_CAPACITY 2048u
#define PM_BOWED_BRIDGE_CAPACITY 1024u
#define PM_BOWED_MIN_HZ 24.0f
#define PM_BOWED_MAX_HZ 8000.0f

typedef struct {
    float bow_pressure;     /* 0 (light, airy) .. 1 (heavy, harsh): the friction curve's steepness */
    float bow_speed;        /* 0 .. 1: the bow's velocity, the energy put in */
    float bow_position;     /* 0.02 .. 0.45: the contact point, as a fraction of the string from the bridge */
    float brightness;       /* 0 (dark) .. 1 (bright): the bridge's reflection filter */
    float attack_seconds;   /* the bow reaching its speed */
    float release_seconds;  /* the bow leaving the string */
    float vibrato_depth;    /* semitones, 0 .. 1 */
    float vibrato_rate_hz;  /* 0.5 .. 12 */
} pm_bowed_params_t;

typedef struct {
    float neck[PM_BOWED_NECK_CAPACITY];
    float bridge[PM_BOWED_BRIDGE_CAPACITY];
    float period;           /* the note's period in samples, unbent */
    float pitch_ratio;      /* bend */
    float neck_delay;       /* current lengths; they glide to the targets */
    float bridge_delay;
    float neck_target;
    float bridge_target;
    float filter_pole;      /* bridge reflection: one-pole lowpass */
    float filter_gain;
    float filter_state;
    float filter_delay;     /* its low-frequency delay, samples */
    float table_slope;      /* friction curve steepness, from the pressure */
    float slope_target;
    float max_velocity;     /* the bow's speed at full envelope */
    float velocity_target;
    float envelope;         /* 0 .. 1: the bow's speed envelope */
    float attack_step;
    float release_step;
    float vibrato_sin;      /* a two-state sine: no table, no trig in the loop */
    float vibrato_cos;
    float vibrato_step;
    float vibrato_depth;    /* as a fraction of the period */
    float vibrato_onset;    /* 0 .. 1: vibrato comes in after the attack */
    float velocity;         /* the note's velocity, 0 .. 1 */
    float output_gain;      /* the bridge's velocity wave to a full-scale voice; glides */
    float output_target;
    uint16_t neck_index;
    uint16_t bridge_index;
    uint8_t active;
    uint8_t bowing;         /* the bow is on the string */
    pm_bowed_params_t params;
} pm_bowed_voice_t;

void pm_bowed_default_params(pm_bowed_params_t *params);

/* Initialise a voice. Call from control context, or from the audio task that owns the voice. */
void pm_bowed_init(pm_bowed_voice_t *voice);

/* Set the parameters; a sounding note takes them at once, gliding (pressure,
 * speed, position, brightness, vibrato). Bounded, no reset. */
void pm_bowed_configure(pm_bowed_voice_t *voice, const pm_bowed_params_t *params);

/* Put the bow on the string. A voice that is already sounding changes pitch
 * without a reset (legato). velocity scales the bow's speed, 0 .. 1.
 * Returns 0 on success, -1 for invalid input. */
int pm_bowed_note_on(pm_bowed_voice_t *voice, float frequency_hz, float velocity);

/* Lift the bow: the string rings out through the bridge's losses. */
void pm_bowed_note_off(pm_bowed_voice_t *voice);

/* Bend the sounding note: ratio multiplies its frequency (1 = unbent), 0.25 .. 4. */
void pm_bowed_set_pitch_ratio(pm_bowed_voice_t *voice, float ratio);

/* Silence the voice and clear the string. */
void pm_bowed_reset(pm_bowed_voice_t *voice);

/* Add frames to a mono accumulator. */
void pm_bowed_render(pm_bowed_voice_t *voice, float *mono, uint32_t frames);

uint32_t pm_bowed_voice_bytes(void);

#endif
