/* SPDX-License-Identifier: MIT */
#include "bowed_model.h"
#include "pm_math.h"

#define PM_NECK_MASK (PM_BOWED_NECK_CAPACITY - 1u)
#define PM_BRIDGE_MASK (PM_BOWED_BRIDGE_CAPACITY - 1u)
#define PM_BOW_GLIDE 0.003f             /* per sample: lengths and bow settings settle in about 15 ms */
#define PM_BOW_STATE_LIMIT 4.0f         /* nothing in the string may exceed this: a hard bound on the state */
#define PM_BOW_OUTPUT_GAIN 2.6f         /* the bridge's velocity wave to a full-scale voice, before the evening-out below */
#define PM_BOW_LOOP_LATENCY -0.03f      /* samples: what the stick-slip itself adds to the loop; measured, tests/engine */

void pm_bowed_default_params(pm_bowed_params_t *params)
{
    if (!params)
        return;
    params->bow_pressure = 0.5f;
    params->bow_speed = 0.6f;
    params->bow_position = 0.127f;
    params->brightness = 0.5f;
    params->attack_seconds = 0.04f;
    params->release_seconds = 0.08f;
    params->vibrato_depth = 0.0f;
    params->vibrato_rate_hz = 5.5f;
}

static float pm_or(float value, float lo, float hi, float fallback)
{
    /* A NaN fails both comparisons and becomes the fallback. */
    if (!(value >= lo))
        return value < lo ? lo : fallback;
    return value > hi ? hi : value;
}

static void pm_bowed_sanitize(pm_bowed_params_t *p)
{
    p->bow_pressure = pm_or(p->bow_pressure, 0.0f, 1.0f, 0.5f);
    p->bow_speed = pm_or(p->bow_speed, 0.0f, 1.0f, 0.5f);
    p->bow_position = pm_or(p->bow_position, 0.02f, 0.45f, 0.127f);
    p->brightness = pm_or(p->brightness, 0.0f, 1.0f, 0.5f);
    p->attack_seconds = pm_or(p->attack_seconds, 0.002f, 4.0f, 0.04f);
    p->release_seconds = pm_or(p->release_seconds, 0.005f, 4.0f, 0.08f);
    p->vibrato_depth = pm_or(p->vibrato_depth, 0.0f, 1.0f, 0.0f);
    p->vibrato_rate_hz = pm_or(p->vibrato_rate_hz, 0.5f, 12.0f, 5.5f);
}

/* Everything that follows from the parameters, the period and the bend. */
static void pm_bowed_apply(pm_bowed_voice_t *voice)
{
    const pm_bowed_params_t *p = &voice->params;
    float total;
    float bridge;
    /* The bridge reflects a little less of the highs: a one-pole lowpass with
     * its pole between 0.80 (dark) and 0.35 (bright), and 5 % loss overall. */
    voice->filter_pole = 0.80f - 0.45f * p->brightness;
    if (voice->period > 0.0f)           /* a short string loses less: the high notes still speak */
        voice->filter_pole /= 1.0f + PM_SAMPLE_RATE_F * voice->pitch_ratio / (voice->period * 1500.0f);
    voice->filter_gain = 0.95f;
    /* the filter is part of the loop: its delay at the note's own frequency
     * comes off the string's length, so the pitch holds across the range */
    voice->filter_delay = voice->period > 0.0f ?
        pm_onepole_delay(voice->filter_pole, PM_SAMPLE_RATE_F * voice->pitch_ratio / voice->period) :
        voice->filter_pole / (1.0f - voice->filter_pole);
    /* Friction: a steep curve (light bow) lets go easily; a flat one (heavy
     * bow) holds the string through more of the cycle. */
    voice->slope_target = 5.0f - 4.0f * p->bow_pressure;
    /* The bow's speed stays where the string still follows it once a cycle
     * (the Helmholtz motion): faster than this and it skates. Measured. */
    voice->velocity_target = (0.015f + 0.075f * voice->velocity) * (0.25f + 0.75f * p->bow_speed);
    voice->attack_step = 1.0f / (p->attack_seconds * PM_SAMPLE_RATE_F);
    voice->release_step = 1.0f / (p->release_seconds * PM_SAMPLE_RATE_F);
    voice->vibrato_step = PM_TWO_PI * p->vibrato_rate_hz / PM_SAMPLE_RATE_F;
    /* depth in semitones to a fraction of the period */
    voice->vibrato_depth = p->vibrato_depth * 0.0594631f;
    if (!(voice->period > 0.0f))
        return;
    /* A heavy bow and a long string both swing further: even the level out,
     * so pressure changes the tone more than the loudness. Measured. */
    voice->output_target = PM_BOW_OUTPUT_GAIN / (0.75f + p->bow_pressure) *
        (1.0f + PM_SAMPLE_RATE_F * voice->pitch_ratio / (voice->period * 3000.0f));
    total = voice->period / voice->pitch_ratio - voice->filter_delay - PM_BOW_LOOP_LATENCY;
    if (total < 4.0f)
        total = 4.0f;
    bridge = total * p->bow_position;
    if (bridge < 1.0f)
        bridge = 1.0f;
    if (bridge > (float)PM_BOWED_BRIDGE_CAPACITY - 4.0f)
        bridge = (float)PM_BOWED_BRIDGE_CAPACITY - 4.0f;
    voice->bridge_target = bridge;
    voice->neck_target = pm_clampf(total - bridge, 1.0f, (float)PM_BOWED_NECK_CAPACITY - 4.0f);
}

void pm_bowed_reset(pm_bowed_voice_t *voice)
{
    uint32_t i;
    if (!voice)
        return;
    for (i = 0; i < PM_BOWED_NECK_CAPACITY; ++i)
        voice->neck[i] = 0.0f;
    for (i = 0; i < PM_BOWED_BRIDGE_CAPACITY; ++i)
        voice->bridge[i] = 0.0f;
    voice->filter_state = 0.0f;
    voice->envelope = 0.0f;
    voice->vibrato_sin = 0.0f;
    voice->vibrato_cos = 1.0f;
    voice->vibrato_onset = 0.0f;
    voice->neck_index = 0u;
    voice->bridge_index = 0u;
    voice->active = 0u;
    voice->bowing = 0u;
}

void pm_bowed_init(pm_bowed_voice_t *voice)
{
    if (!voice)
        return;
    voice->period = 0.0f;
    voice->pitch_ratio = 1.0f;
    voice->neck_delay = 1.0f;
    voice->bridge_delay = 1.0f;
    voice->neck_target = 1.0f;
    voice->bridge_target = 1.0f;
    voice->table_slope = 3.0f;
    voice->max_velocity = 0.0f;
    voice->velocity = 0.0f;
    voice->output_gain = 0.0f;
    voice->output_target = 0.0f;
    pm_bowed_default_params(&voice->params);
    pm_bowed_reset(voice);
    pm_bowed_apply(voice);
}

void pm_bowed_configure(pm_bowed_voice_t *voice, const pm_bowed_params_t *params)
{
    if (!voice || !params)
        return;
    voice->params = *params;
    pm_bowed_sanitize(&voice->params);
    pm_bowed_apply(voice);
}

int pm_bowed_note_on(pm_bowed_voice_t *voice, float frequency_hz, float velocity)
{
    if (!voice || !(frequency_hz >= PM_BOWED_MIN_HZ) || !(frequency_hz <= PM_BOWED_MAX_HZ) ||
        !(velocity >= 0.0f) || !(velocity <= 1.0f))
        return -1;
    voice->period = PM_SAMPLE_RATE_F / frequency_hz;
    voice->velocity = velocity;
    pm_bowed_apply(voice);
    if (!voice->active) {
        /* A string at rest starts at its length; a sounding one glides (legato). */
        voice->neck_delay = voice->neck_target;
        voice->bridge_delay = voice->bridge_target;
        voice->table_slope = voice->slope_target;
        voice->max_velocity = voice->velocity_target;
        voice->output_gain = voice->output_target;
        voice->vibrato_onset = 0.0f;
    }
    voice->bowing = (uint8_t)(velocity > 0.0f);
    voice->active = (uint8_t)(voice->active || velocity > 0.0f);
    return 0;
}

void pm_bowed_note_off(pm_bowed_voice_t *voice)
{
    if (voice)
        voice->bowing = 0u;
}

void pm_bowed_set_pitch_ratio(pm_bowed_voice_t *voice, float ratio)
{
    if (!voice || !(ratio > 0.0f))
        return;
    voice->pitch_ratio = pm_clampf(ratio, 0.25f, 4.0f);
    pm_bowed_apply(voice);
}

static inline float pm_line_read(const float *line, uint32_t mask, uint32_t write_index, float delay)
{
    float position = (float)write_index - delay;
    float fraction;
    uint32_t i0;
    if (position < 0.0f)
        position += (float)(mask + 1u);
    i0 = (uint32_t)position;
    fraction = position - (float)i0;
    i0 &= mask;
    return line[i0] + (line[(i0 + 1u) & mask] - line[i0]) * fraction;
}

static inline float pm_bound(float x)
{
    return x > PM_BOW_STATE_LIMIT ? PM_BOW_STATE_LIMIT : x < -PM_BOW_STATE_LIMIT ? -PM_BOW_STATE_LIMIT : x;
}

void pm_bowed_render(pm_bowed_voice_t *voice, float *mono, uint32_t frames)
{
    uint32_t n;
    if (!voice || !mono || !voice->active)
        return;

    for (n = 0; n < frames; ++n) {
        float neck_delay;
        float bow_velocity;
        float bridge_out;
        float bridge_reflection;
        float nut_reflection;
        float delta;
        float friction;
        float new_velocity;

        /* the bow and the string's lengths glide to where they were asked to be */
        voice->neck_delay += (voice->neck_target - voice->neck_delay) * PM_BOW_GLIDE;
        voice->bridge_delay += (voice->bridge_target - voice->bridge_delay) * PM_BOW_GLIDE;
        voice->table_slope += (voice->slope_target - voice->table_slope) * PM_BOW_GLIDE;
        voice->max_velocity += (voice->velocity_target - voice->max_velocity) * PM_BOW_GLIDE;
        voice->output_gain += (voice->output_target - voice->output_gain) * PM_BOW_GLIDE;

        if (voice->bowing) {
            voice->envelope += voice->attack_step;
            if (voice->envelope > 1.0f)
                voice->envelope = 1.0f;
        } else {
            voice->envelope -= voice->release_step;
            if (voice->envelope < 0.0f)
                voice->envelope = 0.0f;
        }
        bow_velocity = voice->max_velocity * voice->envelope;

        /* vibrato: the finger rolls on the string, a little after the note starts */
        neck_delay = voice->neck_delay;
        if (voice->vibrato_depth > 0.0f) {
            voice->vibrato_sin += voice->vibrato_step * voice->vibrato_cos;
            voice->vibrato_cos -= voice->vibrato_step * voice->vibrato_sin;
            if (voice->vibrato_onset < 1.0f)
                voice->vibrato_onset += 1.0f / (0.35f * PM_SAMPLE_RATE_F);
            neck_delay += voice->vibrato_sin * voice->vibrato_depth * voice->vibrato_onset *
                          (voice->neck_delay + voice->bridge_delay);
            neck_delay = pm_clampf(neck_delay, 1.0f, (float)PM_BOWED_NECK_CAPACITY - 4.0f);
        }

        /* what comes back from the bridge (filtered, inverted) and from the nut (inverted) */
        bridge_out = pm_line_read(voice->bridge, PM_BRIDGE_MASK, voice->bridge_index, voice->bridge_delay);
        voice->filter_state = (1.0f - voice->filter_pole) * voice->filter_gain * bridge_out +
                              voice->filter_pole * voice->filter_state;
        bridge_reflection = -voice->filter_state;
        nut_reflection = -pm_line_read(voice->neck, PM_NECK_MASK, voice->neck_index, neck_delay);

        /* the bow: the friction curve passes the velocity difference to the
         * string, wholly while it sticks (near zero difference), barely when
         * it slips. The curve is at most 1, so the bow never adds more than
         * the difference it sees. */
        delta = bow_velocity - (bridge_reflection + nut_reflection);
        friction = pm_absf(delta * voice->table_slope) + 0.75f;
        friction *= friction;
        friction = 1.0f / (friction * friction);
        if (friction > 1.0f)
            friction = 1.0f;
        new_velocity = delta * friction;

        voice->neck[voice->neck_index] = pm_bound(bridge_reflection + new_velocity);
        voice->bridge[voice->bridge_index] = pm_bound(nut_reflection + new_velocity);
        voice->neck_index = (uint16_t)((voice->neck_index + 1u) & PM_NECK_MASK);
        voice->bridge_index = (uint16_t)((voice->bridge_index + 1u) & PM_BRIDGE_MASK);

        mono[n] += bridge_out * voice->output_gain;
    }
}

uint32_t pm_bowed_voice_bytes(void)
{
    return (uint32_t)sizeof(pm_bowed_voice_t);
}
