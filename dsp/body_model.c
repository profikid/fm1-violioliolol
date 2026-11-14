/* SPDX-License-Identifier: MIT */
#include "body_model.h"
#include "pm_math.h"

#define PM_ANTI_DENORMAL 1.0e-18f

void pm_body_init(pm_body_t *body)
{
    uint32_t i;
    if (!body)
        return;
    for (i = 0; i < PM_BODY_BANK; ++i) {
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
    body->norm = 1.0f;
    body->modes = 0u;
    body->signature = 0u;
}

/* A two-pole resonator with zeros at DC and Nyquist: unity gain at its peak,
 * so the mode's gain is what it adds there. r < 1 always: every mode decays. */
static void pm_body_mode(pm_body_t *body, float f, float q, float g)
{
    const float r = pm_expf(-PM_PI * f / (q * PM_SAMPLE_RATE_F));
    const uint32_t k = body->modes++;
    body->a1[k] = 2.0f * r * pm_cosf(PM_TWO_PI * f / PM_SAMPLE_RATE_F);
    body->a2[k] = r * r;
    body->b0[k] = 0.5f * (1.0f - r * r) * g;
}

static uint32_t pm_body_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void pm_body_configure(pm_body_t *body, const pm_body_mode_t *modes, uint32_t count)
{
    pm_body_mode_t sig[PM_BODY_MAX_MODES];
    uint32_t i, k, valid = 0u, state = 0x9E3779B9u;
    float lowest = 8000.0f, energy = 0.0f;
    if (!body)
        return;
    if (!modes)
        count = 0u;
    if (count > PM_BODY_MAX_MODES)
        count = PM_BODY_MAX_MODES;
    for (i = 0; i < PM_BODY_BANK; ++i) {
        body->y1[i] = 0.0f;
        body->y2[i] = 0.0f;
    }
    body->x1 = 0.0f;
    body->x2 = 0.0f;
    body->modes = 0u;
    body->signature = 0u;
    body->norm = 1.0f;
    for (i = 0; i < count; ++i) {
        /* Written so that a NaN in a mode leaves it out. */
        float f = modes[i].frequency_hz;
        float q = modes[i].q;
        float g = modes[i].gain;
        if (!(f >= 20.0f) || !(f <= 8000.0f) || !(q >= 1.0f) || !(g >= -4.0f) || !(g <= 4.0f))
            continue;
        if (q > 200.0f)
            q = 200.0f;
        sig[valid].frequency_hz = f;
        sig[valid].q = q;
        sig[valid].gain = g;
        ++valid;
        if (f < lowest)
            lowest = f;
        pm_body_mode(body, f, q, g);
        state += (uint32_t)f * 2654435761u;   /* each body its own cloud, the same every time */
    }
    body->signature = valid;
    if (!valid)
        return;

    /* The cloud: from 1.4 times the lowest signature mode to 7.5 kHz, spaced
     * evenly in log frequency with a little jitter, each mode's gain the
     * signature modes' profile there (a bump half an octave wide round each,
     * over a floor that rolls off above 5 kHz), with a random sign, and damped
     * more the higher it is (Q 30 at 200 Hz to 8 at 7 kHz). */
    {
        const uint32_t cloud = PM_BODY_BANK - valid;
        float lo = 1.4f * lowest, octaves;
        if (lo < 150.0f)
            lo = 150.0f;
        octaves = pm_log2f(7500.0f / lo);
        for (k = 0; k < cloud && octaves > 0.0f; ++k) {
            const float jitter = (float)(pm_body_random(&state) >> 8) * (1.0f / 16777216.0f) - 0.5f;
            const float f = lo * pm_exp2f(octaves * ((float)k + 0.5f + 0.7f * jitter) / (float)cloud);
            float profile = 0.12f, q, g;
            uint32_t m;
            for (m = 0; m < valid; ++m) {
                const float d = pm_log2f(f / sig[m].frequency_hz) * 2.0f;   /* in half octaves */
                profile += pm_absf(sig[m].gain) * pm_expf(-0.5f * d * d);
            }
            profile /= 1.0f + (f * (1.0f / 5000.0f)) * (f * (1.0f / 5000.0f));
            q = pm_clampf(30.0f - 6.0f * pm_log2f(f * (1.0f / 200.0f)), 8.0f, 30.0f);
            g = 0.7f * profile * ((pm_body_random(&state) & 1u) ? 1.0f : -1.0f);
            if (f < 0.45f * PM_SAMPLE_RATE_F)
                pm_body_mode(body, f, q, g);
        }
    }

    /* The bank's impulse response energy is its power gain for white noise:
     * bring it to one. 4096 samples cover every mode's decay to -60 dB but
     * the sharpest signature ones, whose tails add little. */
    {
        float y1[PM_BODY_BANK], y2[PM_BODY_BANK];
        uint32_t n;
        for (k = 0; k < body->modes; ++k)
            y1[k] = y2[k] = 0.0f;
        for (n = 0; n < 4096u; ++n) {
            const float in = (n == 0u) ? 1.0f : (n == 2u) ? -1.0f : 0.0f;
            float sum = 0.0f;
            for (k = 0; k < body->modes; ++k) {
                const float y = body->b0[k] * in + body->a1[k] * y1[k] - body->a2[k] * y2[k];
                y2[k] = y1[k];
                y1[k] = y;
                sum += y;
            }
            energy += sum * sum;
        }
        body->norm = energy > 1.0e-12f ? 1.0f / pm_sqrtf(energy) : 1.0f;
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
        /* what is heard is mostly what the body radiates; a little of the
         * string's own force stays, as the bridge's own radiation */
        mono[n] = x * (1.0f - 0.85f * wet) + sum * body->norm * wet;
    }
    body->wet = body->wet_target;
}

/* ----------------------------------------------------------------- room --- */

static const uint16_t PM_ROOM_BASE[PM_ROOM_LINES] = {563, 701, 857, 997, 1129, 1283, 1427, 1601};

void pm_room_init(pm_room_t *room)
{
    uint32_t i, k;
    if (!room)
        return;
    for (k = 0; k < PM_ROOM_LINES; ++k) {
        for (i = 0; i < PM_ROOM_CAPACITY; ++i)
            room->line[k][i] = 0.0f;
        room->damp[k] = 0.0f;
        room->gain[k] = 0.0f;
        room->length[k] = PM_ROOM_BASE[k];
    }
    room->index = 0u;
    room->tone = 1.0f;
    room->wet = 0.0f;
    room->wet_target = 0.0f;
}

void pm_room_set(pm_room_t *room, float amount, float size, float tone)
{
    uint32_t i, k;
    float t60, scale;
    if (!room)
        return;
    amount = amount >= 0.0f ? pm_clampf(amount, 0.0f, 1.0f) : 0.0f;
    size = size >= 0.0f ? pm_clampf(size, 0.0f, 1.0f) : 0.5f;
    tone = tone >= 0.0f ? pm_clampf(tone, 0.0f, 1.0f) : 0.5f;
    /* a little goes a long way: the amount is a curve, at most 0.55 of room */
    room->wet_target = 0.55f * amount * amount;
    room->tone = 0.12f + 0.88f * tone * tone;
    scale = 0.45f + 0.55f * size;
    t60 = 0.25f + 1.95f * size * size;
    for (k = 0; k < PM_ROOM_LINES; ++k) {
        const uint32_t length = (uint32_t)((float)PM_ROOM_BASE[k] * scale);
        if (length != room->length[k]) {        /* another room: start it quiet */
            for (i = 0; i < PM_ROOM_CAPACITY; ++i)
                room->line[k][i] = 0.0f;
            room->damp[k] = 0.0f;
            room->length[k] = length;
        }
        /* -60 dB in t60 seconds, for a signal going round this line */
        room->gain[k] = pm_exp2f(-9.96578428f * (float)length / (t60 * PM_SAMPLE_RATE_F));
    }
}

void pm_room_process(pm_room_t *room, float *mono, uint32_t frames)
{
    uint32_t n, k;
    float wet, step;
    if (!room || !mono || !frames)
        return;
    if (room->wet == 0.0f && room->wet_target == 0.0f)
        return;
    wet = room->wet;
    step = (room->wet_target - wet) / (float)frames;
    for (n = 0; n < frames; ++n) {
        float y[PM_ROOM_LINES], sum = 0.0f, out = 0.0f;
        const float in = mono[n] + PM_ANTI_DENORMAL;
        const uint32_t at = room->index;
        for (k = 0; k < PM_ROOM_LINES; ++k) {
            const float v = room->line[k][(at - room->length[k]) & (PM_ROOM_CAPACITY - 1u)];
            room->damp[k] += room->tone * (v - room->damp[k]);
            y[k] = room->damp[k] * room->gain[k];
            sum += y[k];
        }
        /* Householder: every line feeds every other, energy kept (|gain| < 1
         * makes it decay); the input goes in with alternating signs */
        sum *= 2.0f / (float)PM_ROOM_LINES;
        for (k = 0; k < PM_ROOM_LINES; ++k) {
            room->line[k][at & (PM_ROOM_CAPACITY - 1u)] = y[k] - sum + ((k & 1u) ? -in : in);
            out += (k & 2u) ? -y[k] : y[k];
        }
        room->index = at + 1u;
        wet += step;
        mono[n] = in + out * 0.35f * wet;
    }
    room->wet = room->wet_target;
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
