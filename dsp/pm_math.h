/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: the few maths the models need, in single precision.
 *
 * The FM-1 build has no libm and no double precision, so the DSP uses these
 * instead of <math.h>. Everything is static inline: no state, no calls.
 */
#ifndef FM1_PM_MATH_H
#define FM1_PM_MATH_H

#include <stdint.h>

#define PM_SAMPLE_RATE_F 44100.0f
#define PM_PI 3.14159265358979f
#define PM_TWO_PI 6.28318530717959f

static inline float pm_absf(float x)
{
    return x < 0.0f ? -x : x;
}

static inline float pm_clampf(float x, float lo, float hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

/* 1 when x is a finite number (not NaN, not infinite). */
static inline int pm_finite(float x)
{
    union { float f; uint32_t u; } bits;
    bits.f = x;
    return (bits.u & 0x7F800000u) != 0x7F800000u;
}

/* sin(x), x in radians, any magnitude a float period count can hold
 * (|x| < 1e6 keeps full accuracy). Error below 1e-6 after reduction. */
static inline float pm_sinf(float x)
{
    float turns = x * (1.0f / PM_TWO_PI);
    float x2;
    int32_t whole = (int32_t)turns;
    turns -= (float)whole;              /* -1 .. 1 */
    if (turns > 0.5f)
        turns -= 1.0f;
    else if (turns < -0.5f)
        turns += 1.0f;                  /* -0.5 .. 0.5 */
    if (turns > 0.25f)
        turns = 0.5f - turns;
    else if (turns < -0.25f)
        turns = -0.5f - turns;          /* -0.25 .. 0.25 */
    x = turns * PM_TWO_PI;              /* -pi/2 .. pi/2 */
    x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 *
           (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
}

static inline float pm_cosf(float x)
{
    return pm_sinf(x + 0.5f * PM_PI);
}

/* 2^x. Relative error below 2e-7 for |x| <= 100; clamped outside. */
static inline float pm_exp2f(float x)
{
    union { float f; uint32_t u; } scale;
    int32_t whole;
    float f;
    if (!(x > -100.0f))
        return 0.0f;
    if (x > 100.0f)
        x = 100.0f;
    whole = (int32_t)(x + (x < 0.0f ? -0.5f : 0.5f));   /* nearest */
    f = (x - (float)whole) * 0.693147181f;              /* -0.347 .. 0.347 */
    scale.u = (uint32_t)(whole + 127) << 23;
    return scale.f * (1.0f + f * (1.0f + f * (0.5f + f * (1.0f / 6.0f + f *
           (1.0f / 24.0f + f * (1.0f / 120.0f + f * (1.0f / 720.0f)))))));
}

/* log2(x) for x > 0 (0 or less, or not a number: -100). Error below 1e-6 over the normal range. */
static inline float pm_log2f(float x)
{
    union { float f; uint32_t u; } bits;
    float m, t;
    int32_t e;
    if (!(x > 0.0f))
        return -100.0f;
    bits.f = x;
    e = (int32_t)((bits.u >> 23) & 0xFFu) - 127;
    bits.u = (bits.u & 0x007FFFFFu) | 0x3F800000u;   /* mantissa in [1, 2) */
    m = bits.f;
    if (m > 1.41421356f) {                              /* centre it on 1: [0.707, 1.414) */
        m *= 0.5f;
        ++e;
    }
    t = (m - 1.0f) / (m + 1.0f);                        /* ln m = 2 atanh t, |t| < 0.172 */
    m = t * t;
    return (float)e + 2.88539008f * t * (1.0f + m * (1.0f / 3.0f + m * (1.0f / 5.0f + m * (1.0f / 7.0f + m * (1.0f / 9.0f)))));
}

/* e^x */
static inline float pm_expf(float x)
{
    return pm_exp2f(x * 1.44269504f);
}

/* sqrt(x) for x >= 0; relative error below 1e-6 (two Newton steps on a bit-level guess). */
static inline float pm_sqrtf(float x)
{
    union { float f; uint32_t u; } guess;
    float r;
    if (!(x > 0.0f))
        return 0.0f;
    guess.f = x;
    guess.u = 0x5F375A86u - (guess.u >> 1);             /* about 1 / sqrt(x) */
    r = guess.f;
    r = r * (1.5f - 0.5f * x * r * r);
    r = r * (1.5f - 0.5f * x * r * r);
    r = r * (1.5f - 0.5f * x * r * r);
    return x * r;
}

/* atan(x) for x >= 0, radians; error below 2e-5. */
static inline float pm_atanf(float x)
{
    const int32_t flip = x > 1.0f;
    float x2, r;
    if (!(x > 0.0f))
        return 0.0f;
    if (flip)
        x = 1.0f / x;
    x2 = x * x;
    r = x * (0.99997726f + x2 * (-0.33262347f + x2 * (0.19354346f + x2 *
        (-0.11643287f + x2 * (0.05265332f + x2 * -0.01172120f)))));
    return flip ? 0.5f * PM_PI - r : r;
}

/* The delay, in samples, a one-pole lowpass y = (1 - p) x + p y[n-1] gives a
 * sine of the given frequency (0 <= p < 1). */
static inline float pm_onepole_delay(float pole, float frequency_hz)
{
    const float w = PM_TWO_PI * frequency_hz / PM_SAMPLE_RATE_F;
    const float den = 1.0f - pole * pm_cosf(w);
    if (!(w > 1.0e-4f) || !(den > 1.0e-6f))
        return pole / (1.0f - pole);
    return pm_atanf(pole * pm_sinf(w) / den) / w;
}

/* Frequency of a MIDI note (69 = A4 = 440 Hz), fractional notes allowed. */
static inline float pm_note_hz(float note)
{
    return 440.0f * pm_exp2f((note - 69.0f) * (1.0f / 12.0f));
}

/* Loop gain per period that gives a decay of 60 dB in t60 seconds. */
static inline float pm_t60_gain(float frequency_hz, float t60_seconds)
{
    if (!(t60_seconds > 0.0f) || !(frequency_hz > 0.0f))
        return 0.0f;
    return pm_exp2f(-9.96578428f / (frequency_hz * t60_seconds));
}

#endif
