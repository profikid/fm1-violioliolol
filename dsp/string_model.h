/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: plucked / struck string voice.
 *
 * An extended Karplus-Strong digital waveguide: a fractional delay loop
 * (third-order Lagrange interpolation) with a one-zero loss filter, an allpass dispersion chain (stiffness), a
 * pluck-position comb on the excitation, a pickup-position comb on the
 * output and an optional amplitude limit in the loop (fret buzz, slap).
 *
 * Platform-independent DSP. No allocation, I/O, locks or platform headers.
 */
#ifndef FM1_STRING_MODEL_H
#define FM1_STRING_MODEL_H

#include <stdint.h>

#define PM_STRING_SAMPLE_RATE 44100u
#define PM_STRING_DELAY_CAPACITY 2048u
#define PM_STRING_DISPERSION_STAGES 3u

/* Excitation styles. */
enum {
    PM_EXCITE_NOISE = 0,   /* plain noise burst: the classic Karplus-Strong pluck */
    PM_EXCITE_FINGER,      /* soft, rounded pluck */
    PM_EXCITE_PICK,        /* bright pluck with a short click */
    PM_EXCITE_SLAP,        /* hard, bright, the string rattles on the frets at first */
    PM_EXCITE_HAMMER,      /* struck: a smooth pulse instead of noise */
    PM_EXCITE_COUNT
};

/* Physical parameters of a plucked string. All are clamped when applied. */
typedef struct {
    float decay_t60;            /* seconds to decay 60 dB while held; <= 0 keeps the note_on feedback */
    float release_t60;          /* seconds to decay 60 dB after note-off; <= 0: note-off does nothing */
    float brightness;           /* 0 (two-point average, dark) .. 1 (no high-frequency loss) */
    float stiffness;            /* 0 (ideal string) .. 1 (stiff: partials go sharp) */
    float pluck_position;       /* 0 (off) .. 0.5: where along the string it is excited */
    float hardness;             /* 0 (soft) .. 1 (hard): the excitation's brightness */
    float velocity_to_hardness; /* 0 .. 1: how much velocity adds to hardness */
    float pickup_position;      /* 0 (off) .. 0.5: where along the string the pickup sits */
    float pickup_blend;         /* 0 (string output) .. 1 (pickup output) */
    float buzz;                 /* 0 (off) .. 1: amplitude limit in the loop */
    uint8_t style;              /* PM_EXCITE_* */
} pm_string_params_t;

typedef struct {
    float delay[PM_STRING_DELAY_CAPACITY];
    float delay_samples;        /* the delay line's current length (the loop less its filters) */
    float delay_target;         /* where delay_samples glides to (pitch bend, vibrato) */
    float period;               /* the note's period in samples, unbent */
    float frequency_hz;
    float feedback;
    float held_feedback;
    float release_feedback;
    float loss_b;               /* loss filter: y = (1 - b) x + b x[n-1] */
    float damping_state;
    float dispersion_a;
    float dispersion_x[PM_STRING_DISPERSION_STAGES];
    float dispersion_y[PM_STRING_DISPERSION_STAGES];
    float filter_delay;         /* low-frequency delay of the loss and dispersion filters, samples */
    float output_level;
    float excite_gain;
    float excite_lp;            /* excitation lowpass coefficient, 0 .. 1 */
    float excite_state[2];
    float pickup_blend;
    float buzz_limit;           /* 0: off */
    float buzz_release_limit;
    uint32_t rng;
    uint32_t excite_rng[2];
    uint32_t excite_left[2];    /* samples of excitation still to come: direct, and its pluck-position echo */
    uint32_t excite_wait;       /* samples until the echo starts */
    uint32_t excite_length;
    uint32_t excite_phase;
    uint32_t buzz_hold;         /* samples the slap's rattle lasts */
    uint32_t pickup_tap;        /* samples behind the write position; 0: off */
    uint16_t write_index;
    uint8_t active;
    uint8_t released;
    uint8_t style;
    pm_string_params_t params;
} pm_string_voice_t;

/* The parameters of the first prototype: noise burst, two-point loss, no extras. */
void pm_string_default_params(pm_string_params_t *params);

/* Initialise a voice. Call from control context, or from the audio task that owns the voice. */
void pm_string_init(pm_string_voice_t *voice, uint32_t seed);

/* Set the parameters the next note starts with. A sounding note keeps its
 * excitation; pm_string_update applies what can change while it rings. */
void pm_string_configure(pm_string_voice_t *voice, const pm_string_params_t *params);

/* Apply brightness, decay, pickup and buzz to a sounding note. Bounded, no reset. */
void pm_string_update(pm_string_voice_t *voice, const pm_string_params_t *params);

/* Start/retrigger a plucked string.
 * frequency_hz: 21.6 Hz .. 22.05 kHz is accepted; the string itself reaches
 *        about 12.6 kHz (its shortest delay) and plays that above it.
 * decay: feedback coefficient, clamped internally to 0.80 .. 0.9998; used when
 *        the parameters' decay_t60 is not set.
 * velocity: linear output level, 0 .. 1.
 * Returns 0 on success, -1 for invalid input.
 */
int pm_string_note_on(pm_string_voice_t *voice, float frequency_hz,
                      float decay, float velocity);

/* Release: damps the string when the parameters' release_t60 is set; a no-op
 * otherwise (the naturally decaying prototype). */
void pm_string_note_off(pm_string_voice_t *voice);

/* Bend the sounding note: ratio multiplies its frequency (1 = unbent),
 * 0.25 .. 4. The delay glides to the new length; the string is not reset. */
void pm_string_set_pitch_ratio(pm_string_voice_t *voice, float ratio);

/* 1 while the excitation is still being fed into the string. */
int pm_string_exciting(const pm_string_voice_t *voice);

/* Add frames to a mono accumulator. Safe for a preallocated audio buffer. */
void pm_string_render(pm_string_voice_t *voice, float *mono, uint32_t frames);

/* Number of bytes required by one voice; useful in memory budget reports. */
uint32_t pm_string_voice_bytes(void);

#endif
