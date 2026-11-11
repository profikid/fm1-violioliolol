/* SPDX-License-Identifier: MIT */
#include "string_model.h"
#include "pm_math.h"

#define PM_MIN_FEEDBACK 0.80f
#define PM_MAX_FEEDBACK 0.9998f
#define PM_MAX_T60_FEEDBACK 0.99995f
#define PM_DELAY_MASK (PM_STRING_DELAY_CAPACITY - 1u)
#define PM_MAX_DELAY ((float)PM_STRING_DELAY_CAPACITY - 4.0f)
#define PM_MIN_DELAY 3.0f                /* the interpolation reads four samples around the point */
#define PM_GLIDE 0.004f                 /* per sample: a bend settles in about 10 ms */
#define PM_SLAP_SECONDS 0.03f
#define PM_SLAP_LIMIT 0.35f

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
    voice->held_feedback = 0.995f;
    voice->release_feedback = 0.0f;
    voice->loss_b = 0.5f;
    voice->damping_state = 0.0f;
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

/* The loop filters for the current parameters and period; returns their
 * low-frequency delay in samples. */
static float pm_loop_filters(pm_string_voice_t *voice)
{
    const pm_string_params_t *p = &voice->params;
    float delay;
    voice->loss_b = 0.5f * (1.0f - p->brightness);
    delay = voice->loss_b;
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
            delay += (float)PM_STRING_DISPERSION_STAGES * per_stage;
        }
    }
    return delay;
}

static void pm_set_feedbacks(pm_string_voice_t *voice, float legacy_feedback)
{
    const pm_string_params_t *p = &voice->params;
    if (p->decay_t60 > 0.0f) {
        voice->held_feedback = pm_t60_gain(voice->frequency_hz, p->decay_t60);
        if (voice->held_feedback > PM_MAX_T60_FEEDBACK)
            voice->held_feedback = PM_MAX_T60_FEEDBACK;
    } else {
        voice->held_feedback = legacy_feedback;
    }
    voice->release_feedback = 0.0f;
    if (p->release_t60 > 0.0f) {
        voice->release_feedback = pm_t60_gain(voice->frequency_hz, p->release_t60);
        if (voice->release_feedback > voice->held_feedback)
            voice->release_feedback = voice->held_feedback;
    }
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
    /* The string may not swing further than the limit: it rattles instead. */
    voice->buzz_release_limit = p->buzz > 0.0f ? 1.0f - 0.85f * p->buzz : 0.0f;
    voice->buzz_limit = voice->buzz_release_limit;
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
    voice->filter_delay = pm_loop_filters(voice);
    voice->delay_samples = pm_delay_for(voice, period);
    voice->delay_target = voice->delay_samples;
    pm_set_feedbacks(voice, decay);
    voice->feedback = voice->held_feedback;
    pm_set_taps(voice);
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
     * pluck), so starting a note costs nothing beyond this reset. Its
     * pluck-position echo is the same signal, inverted and late: a second
     * generator from the same seed. Velocity scales the excitation; the
     * string is linear, so the output follows it linearly. */
    hardness = pm_clampf(p->hardness + p->velocity_to_hardness * (velocity - 0.5f), 0.0f, 1.0f);
    voice->style = p->style;
    gain = 1.0f;
    voice->buzz_hold = 0u;
    if (voice->style == PM_EXCITE_FINGER) {
        hardness *= 0.45f;
    } else if (voice->style == PM_EXCITE_SLAP) {
        if (hardness < 0.7f)
            hardness = 0.7f;
        gain = 1.25f;
        voice->buzz_hold = (uint32_t)(PM_SLAP_SECONDS * (float)PM_STRING_SAMPLE_RATE);
        voice->buzz_limit = PM_SLAP_LIMIT;
    }
    if (voice->style == PM_EXCITE_HAMMER) {
        /* A felt hammer stays on the string longer than a hard one. */
        uint32_t length = 8u + (uint32_t)((1.0f - hardness) * 120.0f);
        if ((float)length > 0.5f * period)
            length = (uint32_t)(0.5f * period);
        if (length < 2u)
            length = 2u;
        voice->excite_length = length;
        voice->excite_lp = 1.0f;
        gain = 1.0f;
    } else {
        voice->excite_length = (uint32_t)period + 1u;
        coefficient = 0.02f + 0.98f * hardness * hardness;
        voice->excite_lp = coefficient;
        /* A lowpassed burst carries less energy: bring it back, within reason. */
        gain *= pm_clampf(pm_sqrtf((2.0f - coefficient) / coefficient), 1.0f, 4.0f);
    }
    voice->excite_gain = gain * velocity;
    voice->excite_state[0] = 0.0f;
    voice->excite_state[1] = 0.0f;
    voice->excite_rng[0] = voice->rng;
    voice->excite_rng[1] = voice->rng;
    (void)pm_random(&voice->rng);       /* the next note gets another burst */
    voice->excite_phase = 0u;
    voice->excite_left[0] = voice->excite_length;
    voice->excite_left[1] = 0u;
    voice->excite_wait = 0u;
    if (p->pluck_position > 0.0f) {
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
        const float a = voice->dispersion_a;
        const float old_loss = voice->loss_b;
        voice->loss_b = 0.5f * (1.0f - voice->params.brightness);
        voice->filter_delay += voice->loss_b - old_loss;
        voice->dispersion_a = a;
    }
    voice->delay_target = pm_delay_for(voice, voice->period / ratio);
    pm_set_feedbacks(voice, voice->held_feedback);
    voice->feedback = voice->released && voice->release_feedback > 0.0f ?
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
        voice->feedback = voice->release_feedback;
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
    } else {
        const float noise = pm_noise(&voice->excite_rng[k]);
        voice->excite_state[k] += voice->excite_lp * (noise - voice->excite_state[k]);
        sample = voice->excite_state[k];
        if (voice->style == PM_EXCITE_PICK) {
            /* The pick's release: a click on top of the burst. */
            const uint32_t phase = voice->excite_length - voice->excite_left[k];
            if (phase == 0u)
                sample += 0.6f;
            else if (phase == 2u)
                sample -= 0.6f;
        }
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
            const float step = (voice->delay_target - voice->delay_samples) * PM_GLIDE;
            if (pm_absf(step) < 1.0e-6f)
                voice->delay_samples = voice->delay_target;
            else
                voice->delay_samples += step;
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

        /* One-zero loss filter; b = 0.5 is the classic Karplus-Strong average. */
        filtered = delayed + voice->loss_b * (voice->damping_state - delayed);
        voice->damping_state = delayed;

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
            if (voice->buzz_hold && --voice->buzz_hold == 0u)
                voice->buzz_limit = voice->buzz_release_limit;
        }

        written = filtered;
        if (voice->excite_left[0])
            written += pm_excitation(voice, 0u) * voice->excite_gain;
        if (voice->excite_wait)
            --voice->excite_wait;
        else if (voice->excite_left[1])
            written -= pm_excitation(voice, 1u) * voice->excite_gain;
        voice->delay[voice->write_index] = written;

        out = written;
        if (voice->pickup_tap) {
            /* A pickup hears the string at one point: the wave and its
             * reflection, a comb with notches at the harmonics that have a
             * node there. */
            const float tapped = voice->delay[(voice->write_index - voice->pickup_tap) & PM_DELAY_MASK];
            out += voice->pickup_blend * (0.7f * (written - tapped) - written);
        }
        mono[n] += out * voice->output_level;

        voice->write_index = (uint16_t)((voice->write_index + 1u) & PM_DELAY_MASK);
    }
}

uint32_t pm_string_voice_bytes(void)
{
    return (uint32_t)sizeof(pm_string_voice_t);
}
