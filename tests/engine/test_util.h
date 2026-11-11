/* SPDX-License-Identifier: MIT
 * Shared helpers for the native DSP tests. Analysis is done in double
 * precision here; the DSP under test is single precision only. */
#ifndef FM1_TEST_UTIL_H
#define FM1_TEST_UTIL_H

#include <math.h>
#include <stdio.h>
#include <stdint.h>

#define TEST_SR 44100.0

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", message, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* For checks inside helper functions that return a count of failures. */
#define EXPECT(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", message, __FILE__, __LINE__); \
        ++failures; \
    } \
} while (0)

static inline double t_peak(const float *x, uint32_t n)
{
    double peak = 0.0;
    uint32_t i;
    for (i = 0; i < n; ++i) {
        const double a = fabs((double)x[i]);
        if (a > peak)
            peak = a;
    }
    return peak;
}

static inline double t_rms(const float *x, uint32_t n)
{
    double sum = 0.0;
    uint32_t i;
    for (i = 0; i < n; ++i)
        sum += (double)x[i] * (double)x[i];
    return n ? sqrt(sum / (double)n) : 0.0;
}

static inline int t_finite(const float *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i)
        if (!isfinite(x[i]))
            return 0;
    return 1;
}

static inline double t_max_step(const float *x, uint32_t n)
{
    double step = 0.0;
    uint32_t i;
    for (i = 1; i < n; ++i) {
        const double d = fabs((double)x[i] - (double)x[i - 1u]);
        if (d > step)
            step = d;
    }
    return step;
}

/* Magnitude of the component at frequency hz (Hann-windowed DFT bin). */
static inline double t_tone(const float *x, uint32_t n, double hz)
{
    const double w = 2.0 * 3.14159265358979323846 * hz / TEST_SR;
    double re = 0.0, im = 0.0;
    uint32_t i;
    for (i = 0; i < n; ++i) {
        const double window = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * (double)i / (double)n);
        re += window * (double)x[i] * cos(w * (double)i);
        im -= window * (double)x[i] * sin(w * (double)i);
    }
    return 2.0 * sqrt(re * re + im * im) / (0.5 * (double)n);
}

/* The frequency in [lo, hi] where the spectrum peaks, to about 0.05 Hz. */
static inline double t_peak_hz(const float *x, uint32_t n, double lo, double hi)
{
    double best = lo, best_mag = -1.0, step = (hi - lo) / 200.0;
    int pass;
    for (pass = 0; pass < 3; ++pass) {
        double hz, from = pass ? best - 2.0 * step : lo, to = pass ? best + 2.0 * step : hi;
        if (pass)
            step /= 20.0;
        best_mag = -1.0;
        for (hz = from; hz <= to; hz += step) {
            const double mag = t_tone(x, n, hz);
            if (mag > best_mag) {
                best_mag = mag;
                best = hz;
            }
        }
    }
    return best;
}

/* Fundamental by normalised autocorrelation near an expected frequency
 * (searched 6 % either side), with parabolic interpolation. */
static inline double t_pitch(const float *x, uint32_t n, double expected_hz)
{
    static double r[4200];
    const double period = TEST_SR / expected_hz;
    int lo = (int)(period * 0.94), hi = (int)(period * 1.06) + 1, lag, best = 0;
    double best_r = -2.0, a, b, c, shift;
    if (lo < 2)
        lo = 2;
    if (hi > 4190)
        hi = 4190;
    for (lag = lo - 1; lag <= hi + 1; ++lag) {
        double s = 0.0, e1 = 0.0, e2 = 0.0;
        uint32_t i;
        for (i = 0; i + (uint32_t)lag < n; ++i) {
            s += (double)x[i] * (double)x[i + (uint32_t)lag];
            e1 += (double)x[i] * (double)x[i];
            e2 += (double)x[i + (uint32_t)lag] * (double)x[i + (uint32_t)lag];
        }
        r[lag] = s / sqrt(e1 * e2 + 1.0e-30);
    }
    for (lag = lo; lag <= hi; ++lag) {
        if (r[lag] > best_r) {
            best_r = r[lag];
            best = lag;
        }
    }
    a = r[best - 1];
    b = r[best];
    c = r[best + 1];
    shift = (a - 2.0 * b + c) != 0.0 ? 0.5 * (a - c) / (a - 2.0 * b + c) : 0.0;
    return TEST_SR / ((double)best + shift);
}

static inline double t_cents(double measured_hz, double expected_hz)
{
    return 1200.0 * log(measured_hz / expected_hz) / log(2.0);
}

static inline double t_db(double ratio)
{
    return 20.0 * log10(ratio + 1.0e-30);
}

/* A small deterministic generator for the fuzz tests. */
static inline uint32_t t_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline float t_uniform(uint32_t *state)   /* 0 .. 1 */
{
    return (float)(t_random(state) >> 8) * (1.0f / 16777216.0f);
}

#endif
