/* SPDX-License-Identifier: MIT */
#include "body_model.h"
#include "pm_math.h"

#define PM_ANTI_DENORMAL 1.0e-18f

void pm_body_init(pm_body_t *body)
{
    uint32_t i;
    if (!body)
        return;
    for (i = 0; i < PM_BODY_MAX_MODES; ++i) {
        body->b0[i] = 0.0f;
        body->a1[i] = 0.0f;
        body->a2[i] = 0.0f;
        body->y1[i] = 0.0f;
        body->y2[i] = 0.0f;
    }
    body->x1 = 0.0f;
    body->x2 = 0.0f;
    body->wet = 0.0f;
    body->wet_target = 0.0f;
    body->modes = 0u;
}

void pm_body_configure(pm_body_t *body, const pm_body_mode_t *modes, uint32_t count)
{
    uint32_t i;
    if (!body)
        return;
    if (!modes)
        count = 0u;
    if (count > PM_BODY_MAX_MODES)
        count = PM_BODY_MAX_MODES;
    for (i = 0; i < PM_BODY_MAX_MODES; ++i) {
        body->y1[i] = 0.0f;
        body->y2[i] = 0.0f;
    }
    body->x1 = 0.0f;
    body->x2 = 0.0f;
    body->modes = 0u;
    for (i = 0; i < count; ++i) {
        /* Written so that a NaN in a mode leaves it out. */
        float f = modes[i].frequency_hz;
        float q = modes[i].q;
        float g = modes[i].gain;
        float r;
        uint32_t k;
        if (!(f >= 20.0f) || !(f <= 8000.0f) || !(q >= 1.0f) || !(g >= -4.0f) || !(g <= 4.0f))
            continue;
        if (q > 200.0f)
            q = 200.0f;
        /* A two-pole resonator with zeros at DC and Nyquist: unity gain at
         * its peak, so the mode's gain is what it adds there. r < 1 always:
         * every mode decays. */
        r = pm_expf(-PM_PI * f / (q * PM_SAMPLE_RATE_F));
        k = body->modes++;
        body->a1[k] = 2.0f * r * pm_cosf(PM_TWO_PI * f / PM_SAMPLE_RATE_F);
        body->a2[k] = r * r;
        body->b0[k] = 0.5f * (1.0f - r * r) * g;
    }
}

void pm_body_set_wet(pm_body_t *body, float wet)
{
    if (!body)
        return;
    body->wet_target = wet >= 0.0f ? pm_clampf(wet, 0.0f, 1.0f) : 0.0f;
}

void pm_body_process(pm_body_t *body, float *mono, uint32_t frames)
{
    uint32_t n, k;
    float wet, step;
    if (!body || !mono || !frames)
        return;
    if (!body->modes || (body->wet == 0.0f && body->wet_target == 0.0f)) {
        body->wet = body->wet_target;
        return;
    }
    wet = body->wet;
    step = (body->wet_target - wet) / (float)frames;
    for (n = 0; n < frames; ++n) {
        const float x = mono[n] + PM_ANTI_DENORMAL;
        const float in = x - body->x2;
        float sum = 0.0f;
        for (k = 0; k < body->modes; ++k) {
            const float y = body->b0[k] * in + body->a1[k] * body->y1[k] - body->a2[k] * body->y2[k];
            body->y2[k] = body->y1[k];
            body->y1[k] = y;
            sum += y;
        }
        body->x2 = body->x1;
        body->x1 = x;
        wet += step;
        /* the string itself stays in the sound: a body adds to it */
        mono[n] = x * (1.0f - 0.6f * wet) + sum * wet;
    }
    body->wet = body->wet_target;
}

void pm_output_init(pm_output_t *out)
{
    if (!out)
        return;
    out->tone = 1.0f;
    out->tone_state = 0.0f;
    out->drive = 0.0f;
    out->drive_gain = 1.0f;
    out->level = 1.0f;
    out->level_target = 1.0f;
    out->envelope = 0.0f;
    out->reduction = 1.0f;
}

void pm_output_set_tone(pm_output_t *out, float tone)
{
    if (!out)
        return;
    tone = tone >= 0.0f ? pm_clampf(tone, 0.0f, 1.0f) : 1.0f;
    /* 0: about 500 Hz; 1: open */
    out->tone = tone >= 0.999f ? 1.0f : 0.07f + 0.93f * tone * tone;
}

void pm_output_set_drive(pm_output_t *out, float drive)
{
    if (!out)
        return;
    drive = drive >= 0.0f ? pm_clampf(drive, 0.0f, 1.0f) : 0.0f;
    out->drive = drive;
    out->drive_gain = 1.0f + 30.0f * drive * drive;
}

void pm_output_set_level(pm_output_t *out, float level)
{
    if (!out)
        return;
    out->level_target = level >= 0.0f ? pm_clampf(level, 0.0f, 4.0f) : 0.0f;
}

void pm_output_process(pm_output_t *out, float *mono, uint32_t frames)
{
    uint32_t n;
    float level, step, reduction = 1.0f;
    if (!out || !mono || !frames)
        return;
    level = out->level;
    step = (out->level_target - level) / (float)frames;
    for (n = 0; n < frames; ++n) {
        float x = mono[n];
        float magnitude;
        if (!pm_finite(x))
            x = 0.0f;                   /* nothing upstream should make one; never pass it on */
        if (out->tone < 1.0f) {
            out->tone_state += out->tone * (x - out->tone_state) + PM_ANTI_DENORMAL;
            x = out->tone_state;
        }
        if (out->drive > 0.0f) {
            /* x / (1 + |x|): bounded by 1 whatever comes in. The driven
             * signal is crossfaded in, at about the clean one's level. */
            const float driven = x * out->drive_gain;
            x += out->drive * (0.5f * driven / (1.0f + pm_absf(driven)) - x);
        }
        level += step;
        x *= level;
        /* The limiter: a peak follower with an instant attack and a slow
         * release; above the threshold the gain is exactly what brings the
         * peak back to it. No look-ahead, so a sudden peak is shaped rather
         * than anticipated, but the result is always within the threshold. */
        magnitude = pm_absf(x);
        out->envelope *= 0.9995f;
        if (magnitude > out->envelope)
            out->envelope = magnitude;
        if (out->envelope > PM_OUTPUT_THRESHOLD) {
            const float gain = PM_OUTPUT_THRESHOLD / out->envelope;
            x *= gain;
            if (gain < reduction)
                reduction = gain;
        }
        mono[n] = x;
    }
    out->level = out->level_target;
    out->reduction = reduction;
    if (!pm_finite(out->envelope) || !pm_finite(out->tone_state)) {
        out->envelope = 0.0f;
        out->tone_state = 0.0f;
    }
}
