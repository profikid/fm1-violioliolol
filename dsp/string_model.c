/* SPDX-License-Identifier: MIT */
#include "string_model.h"

#define PM_MIN_FEEDBACK 0.80f
#define PM_MAX_FEEDBACK 0.9998f

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

void pm_string_init(pm_string_voice_t *voice, uint32_t seed)
{
    uint32_t i;
    if (!voice)
        return;
    for (i = 0; i < PM_STRING_DELAY_CAPACITY; ++i)
        voice->delay[i] = 0.0f;
    voice->delay_samples = 0.0f;
    voice->feedback = 0.995f;
    voice->damping_state = 0.0f;
    voice->output_level = 0.0f;
    voice->rng = seed ? seed : 0x6D2B79F5u;
    voice->write_index = 0u;
    voice->active = 0u;
}

int pm_string_note_on(pm_string_voice_t *voice, float frequency_hz,
                      float decay, float velocity)
{
    uint32_t i;
    uint32_t excitation_start;
    if (!voice || !(frequency_hz >= 21.6f) || !(frequency_hz <= 22050.0f))
        return -1;
    if (decay < PM_MIN_FEEDBACK)
        decay = PM_MIN_FEEDBACK;
    if (decay > PM_MAX_FEEDBACK)
        decay = PM_MAX_FEEDBACK;
    if (velocity < 0.0f)
        velocity = 0.0f;
    if (velocity > 1.0f)
        velocity = 1.0f;

    voice->delay_samples = (float)PM_STRING_SAMPLE_RATE / frequency_hz;
    if (voice->delay_samples >= (float)PM_STRING_DELAY_CAPACITY)
        return -1;
    voice->feedback = decay;
    voice->output_level = velocity;
    voice->damping_state = 0.0f;
    voice->write_index = 0u;

    /* Excite only the active delay span. The unused tail must start silent,
     * otherwise the first several periods contain unrelated stale noise. */
    for (i = 0; i < PM_STRING_DELAY_CAPACITY; ++i)
        voice->delay[i] = 0.0f;
    excitation_start = PM_STRING_DELAY_CAPACITY -
                       ((uint32_t)voice->delay_samples + 1u);
    for (i = excitation_start; i < PM_STRING_DELAY_CAPACITY; ++i)
        voice->delay[i] = pm_noise(&voice->rng) * velocity;

    voice->active = (uint8_t)(velocity > 0.0f);
    return 0;
}

void pm_string_note_off(pm_string_voice_t *voice)
{
    (void)voice;
}

void pm_string_render(pm_string_voice_t *voice, float *mono, uint32_t frames)
{
    uint32_t n;
    if (!voice || !mono || !voice->active)
        return;

    for (n = 0; n < frames; ++n) {
        float read_position = (float)voice->write_index - voice->delay_samples;
        float fraction;
        float delayed;
        float filtered;
        uint32_t i0;
        uint32_t i1;

        while (read_position < 0.0f)
            read_position += (float)PM_STRING_DELAY_CAPACITY;
        i0 = (uint32_t)read_position;
        if (i0 >= PM_STRING_DELAY_CAPACITY)
            i0 = 0u;
        i1 = i0 + 1u;
        if (i1 == PM_STRING_DELAY_CAPACITY)
            i1 = 0u;
        fraction = read_position - (float)i0;
        delayed = voice->delay[i0] +
                  (voice->delay[i1] - voice->delay[i0]) * fraction;

        /* Two-point averaging is the classic Karplus-Strong loss filter. */
        filtered = 0.5f * (delayed + voice->damping_state);
        voice->damping_state = delayed;
        voice->delay[voice->write_index] = filtered * voice->feedback;
        mono[n] += delayed * voice->output_level;

        ++voice->write_index;
        if (voice->write_index == PM_STRING_DELAY_CAPACITY)
            voice->write_index = 0u;
    }
}

uint32_t pm_string_voice_bytes(void)
{
    return (uint32_t)sizeof(pm_string_voice_t);
}
