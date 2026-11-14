/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: plucked / struck string voice.
 *
 * An extended Karplus-Strong digital waveguide: a fractional delay loop
 * (third-order Lagrange interpolation) with a one-pole loss filter designed
 * from two decay times, so the high partials die faster than the low ones, an allpass dispersion chain (stiffness), a
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
    PM_EXCITE_FINGER,      /* the string pulled aside by a finger: a triangle with a broad, rounded corner */
    PM_EXCITE_PICK,        /* by a pick: a sharp corner, and a little scrape */
    PM_EXCITE_SLAP,        /* by the thumb, hard: the string rattles on the frets at first */
    PM_EXCITE_HAMMER,      /* struck: a smooth pulse instead of noise */
    PM_EXCITE_COUNT
};

/* Physical parameters of a plucked string. All are clamped when applied. */
typedef struct {
    float decay_t60;            /* seconds to decay 60 dB while held; <= 0 keeps the note_on feedback */
    float release_t60;          /* seconds to decay 60 dB after note-off; <= 0: note-off does nothing */
    float brightness;           /* 0 .. 1: how long the highs ring next to the fundamental. The
                                 * decay at 3 kHz is 1/32 (0) to all (1) of decay_t60's */
    float stiffness;            /* 0 (ideal string) .. 1 (stiff: partials go sharp) */
    float pluck_position;       /* 0 (off) .. 0.5: where along the string it is excited */
    float hardness;             /* 0 (soft) .. 1 (hard): the contact's width, the excitation's brightness */
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
    float feedback;             /* the loop's gain now; it glides to feedback_target */
    float feedback_target;
    float held_feedback;
    float release_feedback;
    float loss_a;               /* loss filter: y = (1 - a) x + a y[n-1], then times the feedback */
    float damping_state;
    float bright_ratio;         /* T60 at 3 kHz over T60 at the fundamental */
    float dispersion_a;
    float dispersion_x[PM_STRING_DISPERSION_STAGES];
    float dispersion_y[PM_STRING_DISPERSION_STAGES];
    float filter_delay;         /* low-frequency delay of the loss and dispersion filters, samples */
    float output_level;
    float excite_gain;
    float excite_lp;            /* excitation lowpass coefficient, 0 .. 1 */
    float excite_state[2];
    float excite_state2[2];     /* the shape's second lowpass stage */
    float excite_apex;          /* the shaped pluck: where the triangle peaks, 0 .. 1 */
    uint32_t excite_span;       /* its length, one period; what follows is the rounding's tail,
                                 * which wraps onto the next period as the string carries it */
    float excite_noise;         /* and how much scrape is mixed into it */
    float out_prev;             /* the string's last output: the bridge force is its change */
    float out_scale;            /* the force at a level independent of the pitch; 0: the
                                 * noise style, whose output is the string itself */
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
