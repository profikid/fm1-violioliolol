/* SPDX-License-Identifier: MIT */
#include "string_model.h"
#include "pm_math.h"

#define PM_MIN_FEEDBACK 0.80f
#define PM_MAX_FEEDBACK 0.9998f
#define PM_MAX_T60_FEEDBACK 0.99995f
#define PM_DELAY_MASK (PM_STRING_DELAY_CAPACITY - 1u)
#define PM_MAX_DELAY ((float)PM_STRING_DELAY_CAPACITY - 4.0f)
#define PM_MIN_DELAY 3.0f                /* the interpolation reads four samples around the point */
#define PM_MAX_GLIDE 0.25f
#define PM_GLIDE 0.004f                 /* per sample: a bend settles in about 10 ms */
#define PM_SLAP_SECONDS 0.03f
#define PM_SLAP_LIMIT 0.35f
#define PM_BRIGHT_HZ 3000.0f            /* the highs' decay is set at this frequency (or 3 f0, if higher) */
#define PM_MAX_LOSS_POLE 0.995f

static uint32_t pm_random(uint32_t *state)
{
    uint32_t x = *state;
    if (x == 0u)
        x = 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float pm_noise(uint32_t *state)
{
    /* Use the upper 24 bits so the conversion is well behaved on single FPU. */
    const uint32_t value = pm_random(state) >> 8;
    return ((float)value * (1.0f / 8388607.5f)) - 1.0f;
}

void pm_string_default_params(pm_string_params_t *params)
{
    if (!params)
        return;
    params->decay_t60 = 0.0f;
    params->release_t60 = 0.0f;
    params->brightness = 0.0f;
    params->stiffness = 0.0f;
    params->pluck_position = 0.0f;
    params->hardness = 1.0f;
    params->velocity_to_hardness = 0.0f;
    params->pickup_position = 0.0f;
    params->pickup_blend = 0.0f;
    params->buzz = 0.0f;
    params->style = PM_EXCITE_NOISE;
}

static void pm_sanitize(pm_string_params_t *p)
{
    /* The comparisons are written so that a NaN falls to the safe value. */
    if (!(p->decay_t60 > 0.0f) || !(p->decay_t60 <= 60.0f))
        p->decay_t60 = p->decay_t60 > 60.0f ? 60.0f : 0.0f;
    if (!(p->release_t60 > 0.0f) || !(p->release_t60 <= 60.0f))
        p->release_t60 = p->release_t60 > 60.0f ? 60.0f : 0.0f;
    p->brightness = p->brightness >= 0.0f ? pm_clampf(p->brightness, 0.0f, 1.0f) : 0.0f;
    p->stiffness = p->stiffness >= 0.0f ? pm_clampf(p->stiffness, 0.0f, 1.0f) : 0.0f;
    p->pluck_position = p->pluck_position >= 0.0f ? pm_clampf(p->pluck_position, 0.0f, 0.95f) : 0.0f;
    p->hardness = p->hardness >= 0.0f ? pm_clampf(p->hardness, 0.0f, 1.0f) : 0.0f;
    p->velocity_to_hardness = p->velocity_to_hardness >= 0.0f ?
                              pm_clampf(p->velocity_to_hardness, 0.0f, 1.0f) : 0.0f;
    p->pickup_position = p->pickup_position >= 0.0f ? pm_clampf(p->pickup_position, 0.0f, 0.95f) : 0.0f;
    p->pickup_blend = p->pickup_blend >= 0.0f ? pm_clampf(p->pickup_blend, 0.0f, 1.0f) : 0.0f;
    p->buzz = p->buzz >= 0.0f ? pm_clampf(p->buzz, 0.0f, 1.0f) : 0.0f;
    if (p->style >= PM_EXCITE_COUNT)
        p->style = PM_EXCITE_NOISE;
}

void pm_string_init(pm_string_voice_t *voice, uint32_t seed)
{
    uint32_t i;
    if (!voice)
        return;
    for (i = 0; i < PM_STRING_DELAY_CAPACITY; ++i)
        voice->delay[i] = 0.0f;
    voice->delay_samples = 0.0f;
    voice->delay_target = 0.0f;
    voice->period = 0.0f;
    voice->frequency_hz = 0.0f;
    voice->feedback = 0.995f;
    voice->feedback_target = 0.995f;
    voice->held_feedback = 0.995f;
    voice->release_feedback = 0.0f;
    voice->loss_a = 0.0f;
    voice->damping_state = 0.0f;
    voice->bright_ratio = 1.0f;
    voice->dispersion_a = 0.0f;
    for (i = 0; i < PM_STRING_DISPERSION_STAGES; ++i) {
        voice->dispersion_x[i] = 0.0f;
        voice->dispersion_y[i] = 0.0f;
    }
    voice->filter_delay = 0.5f;
    voice->output_level = 0.0f;
    voice->excite_gain = 0.0f;
    voice->excite_lp = 1.0f;
    voice->excite_state[0] = 0.0f;
    voice->excite_state[1] = 0.0f;
    voice->excite_state2[0] = 0.0f;
    voice->excite_state2[1] = 0.0f;
    voice->excite_apex = 0.5f;
    voice->excite_span = 1u;
    voice->excite_noise = 0.0f;
    voice->out_prev = 0.0f;
    voice->out_scale = 1.0f;
    voice->pickup_blend = 0.0f;
    voice->buzz_limit = 0.0f;
    voice->buzz_release_limit = 0.0f;
    voice->rng = seed ? seed : 0x6D2B79F5u;
    voice->excite_rng[0] = 0u;
    voice->excite_rng[1] = 0u;
    voice->excite_left[0] = 0u;
    voice->excite_left[1] = 0u;
    voice->excite_wait = 0u;
    voice->excite_length = 0u;
    voice->excite_phase = 0u;
    voice->buzz_hold = 0u;
    voice->pickup_tap = 0u;
    voice->write_index = 0u;
    voice->active = 0u;
    voice->released = 0u;
    voice->style = PM_EXCITE_NOISE;
    pm_string_default_params(&voice->params);
}

void pm_string_configure(pm_string_voice_t *voice, const pm_string_params_t *params)
{
    if (!voice || !params)
        return;
    voice->params = *params;
    pm_sanitize(&voice->params);
}

/* The highs' decay time over the fundamental's: 1/32 (brightness 0) .. 1 (1). */
static float pm_bright_ratio(float brightness)
{
    return pm_exp2f(-5.0f + 5.0f * brightness);
}

/* The dispersion chain for the current parameters and period; returns its
 * low-frequency delay in samples. */
static float pm_dispersion(pm_string_voice_t *voice)
{
    const pm_string_params_t *p = &voice->params;
    float delay = 0.0f;
    voice->dispersion_a = 0.0f;
    if (p->stiffness > 0.0f && voice->period > 8.0f) {
        /* Each first-order allpass delays the low partials more than the high
         * ones, so the partials go sharp, as a stiff string's do. The nearer
         * its coefficient is to -1, the lower the frequencies it spreads.
         * What it delays the fundamental comes off the delay line, so the
         * note stays in tune; the chain may take 40 % of the period at most. */
        const float w = PM_TWO_PI / voice->period;
        const float sin_w = pm_sinf(w), cos_w = pm_cosf(w);
        float a = 0.88f * p->stiffness;             /* the coefficient is -a */
        float per_stage = 1.0f + 2.0f * pm_atanf(a * sin_w / (1.0f - a * cos_w)) / w;
        const float room = 0.4f * voice->period / (float)PM_STRING_DISPERSION_STAGES;
        uint32_t guard;
        for (guard = 0; guard < 8u && per_stage > room; ++guard) {
            a *= 0.7f;
            per_stage = 1.0f + 2.0f * pm_atanf(a * sin_w / (1.0f - a * cos_w)) / w;
        }
        if (per_stage <= room && a > 0.001f) {
            voice->dispersion_a = -a;
            delay = (float)PM_STRING_DISPERSION_STAGES * per_stage;
        }
    }
    return delay;
}

/* The loss filter's gain at the fundamental, relative to its gain at DC. */
static float pm_loss_norm(float a, float frequency_hz)
{
    const float w = PM_TWO_PI * frequency_hz / PM_SAMPLE_RATE_F;
    return (1.0f - a) / pm_sqrtf(1.0f - 2.0f * a * pm_cosf(w) + a * a);
}

/* A loop gain for a T60, at the fundamental, with the loss filter's own
 * attenuation there taken out (so the fundamental decays in exactly t60). */
static float pm_loop_gain(const pm_string_voice_t *voice, float t60)
{
    float g = pm_t60_gain(voice->frequency_hz, t60) / pm_loss_norm(voice->loss_a, voice->frequency_hz);
    return g > PM_MAX_T60_FEEDBACK ? PM_MAX_T60_FEEDBACK : g;
}

/* The loss filter, a one-pole lowpass y = (1 - a) x + a y[n-1], and the loop
 * gains, from the decay times: the fundamental decays in decay_t60 (or as
 * the legacy feedback gives), and PM_BRIGHT_HZ in bright_ratio times that.
 * Per trip round the loop the filter must pass rho = G(T60 high) / G(T60 low)
 * of the low gain at w_h; for the one-pole that is the root in (0, 1) of
 * (1 - rho^2) a^2 - 2 (1 - rho^2 cos w_h) a + (1 - rho^2) = 0.
 * Returns the filter's delay at the fundamental, in samples. */
static float pm_set_loss(pm_string_voice_t *voice, float legacy_feedback)
{
    const pm_string_params_t *p = &voice->params;
    const float f0 = voice->frequency_hz;
    float t60, fh, rho, a = 0.0f;
    if (p->decay_t60 > 0.0f)
        t60 = p->decay_t60;
    else                                /* the feedback a legacy caller gave, as a T60: -3 / (f0 log10 g) */
        t60 = -9.96578428f / (f0 * pm_log2f(legacy_feedback));
    if (!(t60 > 0.001f))
        t60 = 0.001f;
    voice->bright_ratio = pm_bright_ratio(p->brightness);
    fh = 3.0f * f0 > PM_BRIGHT_HZ ? 3.0f * f0 : PM_BRIGHT_HZ;
    if (fh > 0.42f * PM_SAMPLE_RATE_F)
        fh = 0.42f * PM_SAMPLE_RATE_F;
    rho = pm_t60_gain(f0, t60 * voice->bright_ratio) / pm_t60_gain(f0, t60);
    if (fh > 1.5f * f0 && rho < 0.9999f) {
        const float r2 = rho * rho, c = pm_cosf(PM_TWO_PI * fh / PM_SAMPLE_RATE_F);
        const float A = 1.0f - r2, B = 1.0f - r2 * c;
        a = (B - pm_sqrtf(B * B - A * A)) / A;
        a = pm_clampf(a, 0.0f, PM_MAX_LOSS_POLE);
    }
    voice->loss_a = a;
    voice->held_feedback = pm_loop_gain(voice, t60);
    voice->release_feedback = 0.0f;
    if (p->release_t60 > 0.0f) {
        voice->release_feedback = pm_loop_gain(voice, p->release_t60);
        if (voice->release_feedback > voice->held_feedback)
            voice->release_feedback = voice->held_feedback;
    }
    return a > 0.0f ? pm_onepole_delay(a, f0) : 0.0f;
}

static void pm_set_taps(pm_string_voice_t *voice)
{
    const pm_string_params_t *p = &voice->params;
    voice->pickup_tap = 0u;
    voice->pickup_blend = 0.0f;
    if (p->pickup_position > 0.0f && p->pickup_blend > 0.0f) {
        uint32_t tap = (uint32_t)(p->pickup_position * voice->period + 0.5f);
        if (tap < 1u)
            tap = 1u;
        if (tap > PM_STRING_DELAY_CAPACITY - 2u)
            tap = PM_STRING_DELAY_CAPACITY - 2u;
        voice->pickup_tap = tap;
        voice->pickup_blend = p->pickup_blend;
    }
    /* The string may not swing further than the limit: it rattles instead.
     * On a sounding note a new limit is eased in (the render's glide), from
     * no limit if there was none, never stepped. */
    voice->buzz_release_limit = p->buzz > 0.0f ? 1.0f - 0.85f * p->buzz : 0.0f;
    if (!voice->active)
        voice->buzz_limit = voice->buzz_release_limit;
    else if (voice->buzz_limit == 0.0f && voice->buzz_release_limit > 0.0f)
        voice->buzz_limit = 4.0f;
}

static float pm_delay_for(const pm_string_voice_t *voice, float period)
{
    return pm_clampf(period - voice->filter_delay, PM_MIN_DELAY, PM_MAX_DELAY);
}

int pm_string_note_on(pm_string_voice_t *voice, float frequency_hz,
                      float decay, float velocity)
{
    const pm_string_params_t *p;
    uint32_t i;
    float period, hardness, coefficient, gain;
    if (!voice || !(frequency_hz >= 21.6f) || !(frequency_hz <= 22050.0f) ||
        !(decay >= 0.0f) || !(decay <= 1.0f) ||
        !(velocity >= 0.0f) || !(velocity <= 1.0f))
        return -1;
    if (decay < PM_MIN_FEEDBACK)
        decay = PM_MIN_FEEDBACK;
    if (decay > PM_MAX_FEEDBACK)
        decay = PM_MAX_FEEDBACK;

    period = (float)PM_STRING_SAMPLE_RATE / frequency_hz;
    if (period >= (float)PM_STRING_DELAY_CAPACITY)
        return -1;
    p = &voice->params;
    voice->period = period;
    voice->frequency_hz = frequency_hz;
    voice->filter_delay = pm_dispersion(voice) + pm_set_loss(voice, decay);
    voice->delay_samples = pm_delay_for(voice, period);
    voice->delay_target = voice->delay_samples;
    voice->feedback = voice->held_feedback;
    voice->feedback_target = voice->feedback;
    /* The force at the bridge is the string's slope there: the change of the
     * loop's output. Times period / 2 pi it keeps the fundamental's level of
     * the displacement, so the pitch does not change the loudness. */
    voice->out_scale = period * (1.0f / PM_TWO_PI);
    voice->out_prev = 0.0f;
    pm_set_taps(voice);
    voice->buzz_limit = voice->buzz_release_limit;   /* a new note: the string starts afresh */
    voice->output_level = 1.0f;
    voice->damping_state = 0.0f;
    voice->write_index = 0u;
    voice->released = 0u;
    for (i = 0; i < PM_STRING_DISPERSION_STAGES; ++i) {
        voice->dispersion_x[i] = 0.0f;
        voice->dispersion_y[i] = 0.0f;
    }
    /* The string starts at rest: whatever an earlier note left is gone. */
    for (i = 0; i < PM_STRING_DELAY_CAPACITY; ++i)
        voice->delay[i] = 0.0f;

    /* The excitation is fed in as the string plays (one period of it for a
     * pluck), so starting a note costs nothing beyond this reset. Velocity
     * scales it; the string is linear, so the output follows it linearly.
     *
     * Finger, pick and slap pull the string aside and let go: its first
     * period is the triangle the string is pulled into, peaking at the pluck
     * position (which gives each harmonic n its sin(n pi pos) / n^2, the
     * pluck position's notches included), with the corner rounded by how
     * wide the finger or pick is: two one-pole lowpasses over the shape, with
     * a time constant of that width. A pick adds a little scrape (noise).
     * The noise style is the classic Karplus-Strong burst, with the pluck
     * position as an inverted, late echo of it; hammer is a smooth pulse. */
    hardness = pm_clampf(p->hardness + p->velocity_to_hardness * (velocity - 0.5f), 0.0f, 1.0f);
    voice->style = p->style;
    gain = 1.0f;
    voice->buzz_hold = 0u;
    voice->excite_apex = p->pluck_position > 0.0f ? pm_clampf(p->pluck_position, 0.02f, 0.5f) : 0.25f;
    voice->excite_noise = 0.0f;
    if (voice->style == PM_EXCITE_HAMMER) {
        /* A felt hammer stays on the string longer than a hard one. */
        uint32_t length = 8u + (uint32_t)((1.0f - hardness) * 120.0f);
        if ((float)length > 0.5f * period)
            length = (uint32_t)(0.5f * period);
        if (length < 2u)
            length = 2u;
        voice->excite_length = length;
        voice->excite_lp = 1.0f;
        /* heard as the bridge force (the shape's slope times period / 2 pi),
         * the pulse peaks at 4 / length a sample: scale it to a force of
         * 1.5 whatever the string's length and the hammer's */
        gain = 1.5f * PM_TWO_PI * (float)length / (4.0f * period);
    } else if (voice->style == PM_EXCITE_NOISE) {
        voice->excite_length = (uint32_t)period + 1u;
        coefficient = 0.02f + 0.98f * hardness * hardness;
        voice->excite_lp = coefficient;
        /* A lowpassed burst carries less energy: bring it back, within reason. */
        gain *= pm_clampf(pm_sqrtf((2.0f - coefficient) / coefficient), 1.0f, 4.0f);
    } else {
        /* the contact's width as a fraction of the loop (twice the string):
         * a finger 0.5 .. 2.5 %, a thumb 0.3 .. 1 %, a pick 0.05 .. 0.4 % */
        float width;
        if (voice->style == PM_EXCITE_FINGER) {
            width = 0.005f + 0.02f * (1.0f - hardness);
            voice->excite_noise = 0.02f;
        } else if (voice->style == PM_EXCITE_SLAP) {
            width = 0.003f + 0.007f * (1.0f - hardness);
            voice->excite_noise = 0.06f;
            gain = 1.3f;
            voice->buzz_hold = (uint32_t)(PM_SLAP_SECONDS * (float)PM_STRING_SAMPLE_RATE);
            voice->buzz_limit = PM_SLAP_LIMIT;
        } else {
            width = 0.0005f + 0.0035f * (1.0f - hardness);
            voice->excite_noise = 0.03f + 0.05f * hardness;
        }
        width *= period;
        voice->excite_lp = width > 0.25f ? 1.0f - pm_expf(-1.0f / width) : 1.0f;
        /* the scrape is a force at the bridge: in the displacement the loop
         * carries, its size over the force's scale, period / 2 pi */
        voice->excite_noise *= PM_TWO_PI / period * 0.7f;
        voice->excite_span = (uint32_t)period;
        /* and the two lowpasses' tail, -80 dB down, at most another period */
        {
            uint32_t tail = (uint32_t)(12.0f * width) + 2u;
            if (tail > voice->excite_span)
                tail = voice->excite_span;
            voice->excite_length = voice->excite_span + tail;
        }
    }
    if (voice->style == PM_EXCITE_NOISE)
        voice->out_scale = 0.0f;
    voice->excite_gain = gain * velocity;
    voice->excite_state[0] = 0.0f;
    voice->excite_state[1] = 0.0f;
    voice->excite_state2[0] = 0.0f;
    voice->excite_state2[1] = 0.0f;
    voice->excite_rng[0] = voice->rng;
    voice->excite_rng[1] = voice->rng;
    (void)pm_random(&voice->rng);       /* the next note gets another burst */
    voice->excite_phase = 0u;
    voice->excite_left[0] = voice->excite_length;
    voice->excite_left[1] = 0u;
    voice->excite_wait = 0u;
    if (voice->style == PM_EXCITE_NOISE && p->pluck_position > 0.0f) {
        uint32_t wait = (uint32_t)(p->pluck_position * period + 0.5f);
        voice->excite_wait = wait < 1u ? 1u : wait;
        voice->excite_left[1] = voice->excite_length;
    }

    voice->active = (uint8_t)(velocity > 0.0f);
    return 0;
}

void pm_string_update(pm_string_voice_t *voice, const pm_string_params_t *params)
{
    float ratio;
    uint8_t style;
    if (!voice || !params)
        return;
    style = voice->params.style;
    voice->params = *params;
    pm_sanitize(&voice->params);
    if (!voice->active || !(voice->period > 0.0f))
        return;
    /* Keep the bend the note has, and the stiffness it started with (its
     * allpass states belong to that coefficient). */
    ratio = voice->period / (voice->delay_target + voice->filter_delay);
    {
        /* without a decay time in the parameters, the note keeps the feedback
         * it has; the filter's change of delay comes off the line */
        const float old_delay = voice->loss_a > 0.0f ? pm_onepole_delay(voice->loss_a, voice->frequency_hz) : 0.0f;
        const float legacy = voice->held_feedback * pm_loss_norm(voice->loss_a, voice->frequency_hz);
        voice->filter_delay += pm_set_loss(voice, legacy) - old_delay;
    }
    voice->delay_target = pm_delay_for(voice, voice->period / ratio);
    voice->feedback_target = voice->released && voice->release_feedback > 0.0f ?
                             voice->release_feedback : voice->held_feedback;
    {
        const uint32_t hold = voice->buzz_hold;
        pm_set_taps(voice);
        if (hold)
            voice->buzz_limit = PM_SLAP_LIMIT;
    }
    voice->params.style = style;
}

void pm_string_note_off(pm_string_voice_t *voice)
{
    if (!voice || !voice->active || voice->released)
        return;
    if (voice->release_feedback > 0.0f) {
        voice->released = 1u;
        voice->feedback_target = voice->release_feedback;
    }
}

void pm_string_set_pitch_ratio(pm_string_voice_t *voice, float ratio)
{
    if (!voice || !(voice->period > 0.0f) || !(ratio > 0.0f))
        return;
    ratio = pm_clampf(ratio, 0.25f, 4.0f);
    voice->delay_target = pm_delay_for(voice, voice->period / ratio);
}

int pm_string_exciting(const pm_string_voice_t *voice)
{
    return voice && (voice->excite_left[0] || voice->excite_left[1]);
}

/* One sample of generator k's excitation. */
static inline float pm_excitation(pm_string_voice_t *voice, uint32_t k)
{
    float sample;
    if (voice->style == PM_EXCITE_HAMMER) {
        /* A smooth bump, 4 t (1 - t): the hammer's contact force. */
        const float t = (float)(voice->excite_length - voice->excite_left[k]) /
                        (float)voice->excite_length;
        sample = 4.0f * t * (1.0f - t);
    } else if (voice->style == PM_EXCITE_NOISE) {
        const float noise = pm_noise(&voice->excite_rng[k]);
        voice->excite_state[k] += voice->excite_lp * (noise - voice->excite_state[k]);
        sample = voice->excite_state[k];
    } else {
        /* the triangle the string is pulled into, from rest and back to it
         * (its DC the bridge force does not hear), with a scrape */
        const uint32_t phase = voice->excite_length - voice->excite_left[k];
        const float t = (float)phase / (float)voice->excite_span;
        const float apex = voice->excite_apex;
        float shape = 0.0f;
        if (phase < voice->excite_span) {
            shape = t < apex ? t / apex : (1.0f - t) / (1.0f - apex);
            shape += voice->excite_noise * pm_noise(&voice->excite_rng[k]);
        }
        voice->excite_state[k] += voice->excite_lp * (shape - voice->excite_state[k]);
        voice->excite_state2[k] += voice->excite_lp * (voice->excite_state[k] - voice->excite_state2[k]);
        sample = voice->excite_state2[k];
    }
    --voice->excite_left[k];
    return sample;
}

void pm_string_render(pm_string_voice_t *voice, float *mono, uint32_t frames)
{
    uint32_t n;
    if (!voice || !mono || !voice->active)
        return;

    for (n = 0; n < frames; ++n) {
        float read_position;
        float fraction;
        float delayed;
        float filtered;
        float written;
        float out;
        uint32_t i0;

        if (voice->delay_samples != voice->delay_target) {
            /* a bend glides; at most a quarter sample a sample, so a big one
             * on a long string is not a burst of Doppler at the bridge */
            const float step = pm_clampf((voice->delay_target - voice->delay_samples) * PM_GLIDE,
                                         -PM_MAX_GLIDE, PM_MAX_GLIDE);
            if (pm_absf(step) < 1.0e-6f)
                voice->delay_samples = voice->delay_target;
            else
                voice->delay_samples += step;
            if (voice->out_scale > 0.0f)    /* the force's scale follows the string's length */
                voice->out_scale = (voice->delay_samples + voice->filter_delay) * (1.0f / PM_TWO_PI);
        }
        /* The fractional delay: a third-order Lagrange interpolation over
         * four samples. A linear one is itself a lowpass whose strength
         * depends on the fraction, so the same brightness would sound
         * different from note to note; this one is flat enough not to, and
         * unlike an allpass it glides without clicks. */
        read_position = (float)voice->write_index - voice->delay_samples;
        if (read_position < 0.0f)
            read_position += (float)PM_STRING_DELAY_CAPACITY;
        i0 = (uint32_t)read_position;
        fraction = read_position - (float)i0;
        {
            const float xm = voice->delay[(i0 - 1u) & PM_DELAY_MASK];
            const float x0 = voice->delay[i0 & PM_DELAY_MASK];
            const float x1 = voice->delay[(i0 + 1u) & PM_DELAY_MASK];
            const float x2 = voice->delay[(i0 + 2u) & PM_DELAY_MASK];
            const float fm = fraction - 1.0f, f2 = fraction - 2.0f, fp = fraction + 1.0f;
            delayed = fm * f2 * (0.5f * fp * x0 - (1.0f / 6.0f) * fraction * xm) +
                      fp * fraction * ((1.0f / 6.0f) * fm * x2 - 0.5f * f2 * x1);
        }

        /* the loss filter: a one-pole lowpass, the highs lose more each trip */
        voice->damping_state += (1.0f - voice->loss_a) * (delayed - voice->damping_state);
        filtered = voice->damping_state;
        /* a change of the loop's gain (a release, damping, a new decay) is
         * a finger coming down on the string: a few milliseconds, never a
         * step, which the bridge force would make a click of */
        if (voice->feedback != voice->feedback_target) {
            voice->feedback += (voice->feedback_target - voice->feedback) * 0.01f;
            if (pm_absf(voice->feedback_target - voice->feedback) < 1.0e-6f)
                voice->feedback = voice->feedback_target;
        }

        if (voice->dispersion_a != 0.0f) {
            const float a = voice->dispersion_a;
            uint32_t s;
            for (s = 0; s < PM_STRING_DISPERSION_STAGES; ++s) {
                const float y = a * filtered + voice->dispersion_x[s] - a * voice->dispersion_y[s];
                voice->dispersion_x[s] = filtered;
                voice->dispersion_y[s] = y;
                filtered = y;
            }
        }
        filtered *= voice->feedback;

        if (voice->buzz_limit > 0.0f) {
            /* The limit only ever takes energy out of the loop. */
            if (filtered > voice->buzz_limit)
                filtered = voice->buzz_limit;
            else if (filtered < -voice->buzz_limit)
                filtered = -voice->buzz_limit;
            /* the slap's rattle ends by easing the limit out over about
             * 10 ms: let go at once, the string would jump to where it is
             * held back from, a click */
            if (voice->buzz_hold) {
                --voice->buzz_hold;
            } else if (voice->buzz_limit != voice->buzz_release_limit) {
                const float target = voice->buzz_release_limit > 0.0f ? voice->buzz_release_limit : 4.0f;
                voice->buzz_limit += (target - voice->buzz_limit) * 0.0025f;
                if (pm_absf(target - voice->buzz_limit) < 0.002f)
                    voice->buzz_limit = voice->buzz_release_limit;
            }
        }

        written = filtered;
        if (voice->excite_left[0])
            written += pm_excitation(voice, 0u) * voice->excite_gain;
        if (voice->excite_wait)
            --voice->excite_wait;
        else if (voice->excite_left[1])
            written -= pm_excitation(voice, 1u) * voice->excite_gain;
        voice->delay[voice->write_index] = written;

        if (voice->out_scale > 0.0f) {
            /* The bridge force: the string's slope there, the change of what
             * the loop carries. A pickup hears the string's motion at one
             * point: the wave and its reflection, a comb with notches at the
             * harmonics that have a node there. Both are taken on the change
             * per sample, so a pickup moved while a note sounds switches
             * between two small values rather than stepping the output. */
            float d = written - voice->out_prev;
            voice->out_prev = written;
            if (voice->pickup_tap) {
                const uint32_t tap = (uint32_t)voice->write_index - voice->pickup_tap;
                const float d_tap = voice->delay[tap & PM_DELAY_MASK] - voice->delay[(tap - 1u) & PM_DELAY_MASK];
                d += voice->pickup_blend * (0.7f * (d - d_tap) - d);
            }
            out = d * voice->out_scale;
        } else {
            /* the classic noise burst is heard as the string itself */
            out = written;
            if (voice->pickup_tap) {
                const float tapped = voice->delay[(voice->write_index - voice->pickup_tap) & PM_DELAY_MASK];
                out += voice->pickup_blend * (0.7f * (written - tapped) - written);
            }
        }
        mono[n] += out * voice->output_level;

        voice->write_index = (uint16_t)((voice->write_index + 1u) & PM_DELAY_MASK);
    }
}

uint32_t pm_string_voice_bytes(void)
{
    return (uint32_t)sizeof(pm_string_voice_t);
}
