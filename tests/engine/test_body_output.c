/* SPDX-License-Identifier: MIT
 * Native tests of the body resonator and the output stage: DSP-009, DSP-016, RT-010.
 */
#include "body_model.h"
#include "test_util.h"

#define SECOND 44100u
static float buffer[SECOND * 2u];

static void clear(float *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i)
        x[i] = 0.0f;
}

/* DSP-009: a mode rings at its frequency, for about as long as its Q says, and stops. */
static int test_body(void)
{
    static const pm_body_mode_t ONE[] = {{440.0f, 30.0f, 1.0f}};
    static const pm_body_mode_t VIOLIN[] = {
        {275.0f, 18.0f, 1.0f}, {460.0f, 22.0f, -0.9f}, {550.0f, 20.0f, 0.8f},
        {700.0f, 10.0f, 0.4f}, {1050.0f, 8.0f, 0.4f}, {2600.0f, 3.0f, 0.6f}};
    static const pm_body_mode_t BAD[] = {
        {(float)NAN, 10.0f, 1.0f}, {440.0f, (float)NAN, 1.0f}, {440.0f, 10.0f, (float)INFINITY},
        {5.0f, 10.0f, 1.0f}, {30000.0f, 10.0f, 1.0f}, {440.0f, 0.1f, 1.0f}};
    pm_body_t body;
    double ring, at_mode, off_mode, dry_rms, wet_rms;
    uint32_t i, state = 77u;

    pm_body_init(&body);
    pm_body_configure(&body, ONE, 1u);
    pm_body_set_wet(&body, 1.0f);
    clear(buffer, SECOND);
    pm_body_process(&body, buffer, 256u);       /* the wet ramp reaches its target */
    buffer[0] = 1.0f;
    pm_body_process(&body, buffer, SECOND);
    ring = t_peak_hz(buffer + 64u, 16384u, 300.0, 600.0);
    CHECK(fabs(t_cents(ring, 440.0)) < 15.0, "a mode should ring at its frequency");
    /* Q 30 at 440 Hz: 60 dB down after about 30 * 2.2 / 440 = 0.15 s */
    CHECK(t_peak(buffer + 2205u, 2205u) > 1.0e-6, "a mode should still ring after 50 ms");
    CHECK(t_peak(buffer + 22050u, 22050u) < 1.0e-6 * t_peak(buffer + 1u, 2205u) + 1.0e-12,
          "a mode must decay: the body cannot ring for ever");

    /* a steady tone at the mode comes out stronger than one between modes */
    pm_body_configure(&body, ONE, 1u);
    for (i = 0; i < SECOND; ++i)
        buffer[i] = 0.25f * (float)sin(2.0 * 3.14159265358979 * 440.0 * (double)i / TEST_SR);
    pm_body_process(&body, buffer, SECOND);
    at_mode = t_rms(buffer + 22050u, 22050u);
    pm_body_configure(&body, ONE, 1u);
    for (i = 0; i < SECOND; ++i)
        buffer[i] = 0.25f * (float)sin(2.0 * 3.14159265358979 * 1500.0 * (double)i / TEST_SR);
    pm_body_process(&body, buffer, SECOND);
    off_mode = t_rms(buffer + 22050u, 22050u);
    CHECK(at_mode > 2.0 * off_mode, "the body should favour its modes");

    /* wet 0 is the string alone, exactly; no modes is no body */
    pm_body_configure(&body, VIOLIN, 6u);
    pm_body_set_wet(&body, 0.0f);
    clear(buffer, 512u);
    pm_body_process(&body, buffer, 256u);
    for (i = 0; i < 256u; ++i)
        buffer[i] = t_uniform(&state) - 0.5f;
    {
        float copy[256];
        for (i = 0; i < 256u; ++i)
            copy[i] = buffer[i];
        pm_body_process(&body, buffer, 256u);
        for (i = 0; i < 256u; ++i)
            CHECK(buffer[i] == copy[i], "a body turned off must not touch the sound");
        pm_body_configure(&body, 0, 0u);
        pm_body_set_wet(&body, 1.0f);
        pm_body_process(&body, buffer, 256u);
        for (i = 0; i < 256u; ++i)
            CHECK(buffer[i] == copy[i], "no modes: no body");
    }

    /* a full body on full-scale noise: bounded, finite, and not louder than about the string */
    pm_body_configure(&body, VIOLIN, 6u);
    pm_body_set_wet(&body, 1.0f);
    for (i = 0; i < SECOND; ++i)
        buffer[i] = 2.0f * t_uniform(&state) - 1.0f;
    dry_rms = t_rms(buffer, SECOND);
    pm_body_process(&body, buffer, SECOND);
    wet_rms = t_rms(buffer, SECOND);
    CHECK(t_finite(buffer, SECOND), "the body must remain finite");
    CHECK(wet_rms < 1.5 * dry_rms && wet_rms > 0.1 * dry_rms, "the body should colour, not amplify");

    /* modes that are not numbers, or out of range, are left out */
    pm_body_configure(&body, BAD, 6u);
    CHECK(body.modes == 0u, "invalid modes must be left out");
    pm_body_configure(&body, VIOLIN, 100u);
    CHECK(body.modes == PM_BODY_MAX_MODES, "the mode count is capped");
    pm_body_set_wet(&body, (float)NAN);
    CHECK(body.wet_target == 0.0f, "a wet amount that is not a number is none");
    printf("  body: ok (mode at %.1f Hz for 440; at/off mode x%.1f; full body %.2fx rms)\n",
           ring, at_mode / off_mode, wet_rms / dry_rms);
    return 0;
}

/* DSP-016 and RT-010: whatever goes in, what comes out is finite and inside full scale. */
static int test_output(void)
{
    pm_output_t out;
    uint32_t i, state = 4321u, trial;
    double clean_highs, dark_highs, quiet, driven, peak;

    pm_output_init(&out);
    for (trial = 0; trial < 6u; ++trial) {
        const float scale = trial == 0u ? 0.5f : trial == 1u ? 2.0f : trial == 2u ? 30.0f : 1000.0f;
        pm_output_set_drive(&out, trial == 4u ? 1.0f : 0.0f);
        pm_output_set_level(&out, trial == 5u ? 4.0f : 1.0f);
        for (i = 0; i < SECOND; ++i) {
            buffer[i] = scale * (2.0f * t_uniform(&state) - 1.0f);
            if (trial >= 3u && (i % 997u) == 0u)
                buffer[i] = (i % 3u) == 0u ? (float)NAN : (i % 3u) == 1u ? (float)INFINITY : -(float)INFINITY;
        }
        pm_output_process(&out, buffer, SECOND);
        CHECK(t_finite(buffer, SECOND), "the output must be finite whatever comes in");
        peak = t_peak(buffer, SECOND);
        CHECK(peak <= (double)PM_OUTPUT_THRESHOLD + 1.0e-4, "the limiter must hold the output inside its threshold");
    }
    CHECK(out.reduction < 1.0f, "the limiter reports that it worked");

    /* below the threshold the limiter does nothing at all */
    pm_output_init(&out);
    for (i = 0; i < 4096u; ++i)
        buffer[i] = 0.5f * (float)sin(2.0 * 3.14159265358979 * 440.0 * (double)i / TEST_SR);
    {
        static float copy[4096];
        for (i = 0; i < 4096u; ++i)
            copy[i] = buffer[i];
        pm_output_process(&out, buffer, 4096u);
        for (i = 0; i < 4096u; ++i)
            CHECK(buffer[i] == copy[i], "a signal under the threshold passes untouched");
        CHECK(out.reduction == 1.0f, "and the limiter reports no reduction");
    }

    /* tone: a lowpass */
    pm_output_init(&out);
    for (i = 0; i < 8192u; ++i)
        buffer[i] = 0.2f * (2.0f * t_uniform(&state) - 1.0f);
    pm_output_process(&out, buffer, 8192u);
    clean_highs = t_tone(buffer, 8192u, 8000.0) + t_tone(buffer, 8192u, 9000.0) + t_tone(buffer, 8192u, 10000.0);
    pm_output_init(&out);
    pm_output_set_tone(&out, 0.0f);
    state = 4321u;
    for (i = 0; i < SECOND; ++i)
        (void)t_uniform(&state);
    for (i = 0; i < 8192u; ++i)
        buffer[i] = 0.2f * (2.0f * t_uniform(&state) - 1.0f);
    pm_output_process(&out, buffer, 8192u);
    dark_highs = t_tone(buffer, 8192u, 8000.0) + t_tone(buffer, 8192u, 9000.0) + t_tone(buffer, 8192u, 10000.0);
    CHECK(dark_highs < 0.2 * clean_highs, "the tone control should take the highs out");

    /* drive: adds harmonics, stays bounded, keeps about the level */
    pm_output_init(&out);
    for (i = 0; i < 16384u; ++i)
        buffer[i] = 0.3f * (float)sin(2.0 * 3.14159265358979 * 220.0 * (double)i / TEST_SR);
    quiet = t_tone(buffer, 16384u, 660.0);
    pm_output_set_drive(&out, 1.0f);
    pm_output_process(&out, buffer, 16384u);
    driven = t_tone(buffer, 16384u, 660.0);
    CHECK(driven > 0.02 && driven > 100.0 * quiet, "drive should add harmonics");
    CHECK(t_rms(buffer, 16384u) < 0.6 && t_rms(buffer, 16384u) > 0.1, "drive should keep about the level");

    /* a level change is ramped over the block: no step */
    pm_output_init(&out);
    for (i = 0; i < 512u; ++i)
        buffer[i] = 0.5f;
    pm_output_set_level(&out, 0.0f);
    pm_output_process(&out, buffer, 256u);
    CHECK(t_max_step(buffer, 256u) < 0.01, "a level change must be ramped");
    CHECK(fabs((double)buffer[255]) < 0.01, "and reach its target by the end of the block");
    pm_output_set_level(&out, (float)NAN);
    pm_output_set_tone(&out, (float)NAN);
    pm_output_set_drive(&out, (float)NAN);
    pm_output_process(&out, buffer + 256u, 256u);
    CHECK(t_finite(buffer + 256u, 256u), "settings that are not numbers must not reach the output");
    printf("  output: ok (limiter holds %.2f; tone %.0f dB at 8-10 kHz; drive 3rd harmonic %.3f)\n",
           (double)PM_OUTPUT_THRESHOLD, t_db(dark_highs / clean_highs), driven);
    return 0;
}

int main(void)
{
    if (test_body() || test_output())
        return 1;
    printf("body and output: PASS\n");
    return 0;
}
