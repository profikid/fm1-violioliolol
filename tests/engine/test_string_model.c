/* SPDX-License-Identifier: MIT
 * Native tests of the plucked-string voice: DSP-001 .. DSP-008, DSP-010.
 */
#include <stdlib.h>
#include "string_model.h"
#include "test_util.h"

#define SECOND 44100u
static float buffer[SECOND * 4u];
static float other[SECOND * 4u];

static void clear(float *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i)
        x[i] = 0.0f;
}

/* Render seconds of one note with the given parameters into out. */
static int play(const pm_string_params_t *params, uint32_t seed, float hz, float velocity,
                float *out, uint32_t frames)
{
    static pm_string_voice_t voice;
    pm_string_init(&voice, seed);
    if (params)
        pm_string_configure(&voice, params);
    if (pm_string_note_on(&voice, hz, 0.997f, velocity) != 0)
        return -1;
    clear(out, frames);
    pm_string_render(&voice, out, frames);
    return 0;
}

/* The first prototype's contract, unchanged. */
static int test_prototype(void)
{
    static pm_string_voice_t voice, quiet_voice, loud_voice;
    static float output[4096];
    static float quiet_output[512];
    static float loud_output[512];
    float peak = 0.0f, energy = 0.0f, pitch_corr = 0.0f, pitch_energy = 0.0f;
    uint32_t i;

    clear(output, 4096u);
    clear(quiet_output, 512u);
    clear(loud_output, 512u);
    pm_string_init(&voice, 12345u);
    CHECK(pm_string_note_on(&voice, 110.0f, 0.997f, 0.8f) == 0, "valid note should start");
    CHECK(pm_string_note_on(&voice, 0.0f, 0.997f, 0.8f) == -1, "zero frequency should be rejected");
    CHECK(pm_string_note_on(&voice, 30000.0f, 0.997f, 0.8f) == -1, "a frequency above Nyquist should be rejected");
    CHECK(pm_string_note_on(&voice, (float)NAN, 0.997f, 0.8f) == -1, "NaN frequency should be rejected");
    CHECK(pm_string_note_on(&voice, 110.0f, (float)NAN, 0.8f) == -1, "NaN decay should be rejected");
    CHECK(pm_string_note_on(&voice, 110.0f, 0.997f, (float)INFINITY) == -1, "infinite velocity should be rejected");
    CHECK(pm_string_note_on(&voice, 110.0f, 0.997f, 1.5f) == -1, "velocity above 1 should be rejected");
    CHECK(pm_string_note_on(&voice, 110.0f, 0.997f, -0.1f) == -1, "negative velocity should be rejected");
    CHECK(pm_string_note_on(0, 110.0f, 0.997f, 0.5f) == -1, "a null voice should be rejected");

    /* Identical excitation must scale linearly with velocity, not velocity^2. */
    pm_string_init(&quiet_voice, 777u);
    pm_string_init(&loud_voice, 777u);
    CHECK(pm_string_note_on(&quiet_voice, 220.0f, 0.997f, 0.25f) == 0, "quarter-velocity note should start");
    CHECK(pm_string_note_on(&loud_voice, 220.0f, 0.997f, 0.50f) == 0, "half-velocity note should start");
    pm_string_render(&quiet_voice, quiet_output, 512u);
    pm_string_render(&loud_voice, loud_output, 512u);
    for (i = 0; i < 512u; ++i)
        CHECK(fabsf(loud_output[i] - 2.0f * quiet_output[i]) < 0.00001f,
              "doubling velocity should double output amplitude");

    pm_string_render(&voice, output, 4096u);
    for (i = 0; i < 4096u; ++i) {
        const float a = output[i] < 0.0f ? -output[i] : output[i];
        CHECK(isfinite(output[i]), "render must remain finite");
        if (a > peak)
            peak = a;
        energy += output[i] * output[i];
    }
    CHECK(peak > 0.001f, "plucked string must produce audio");
    CHECK(energy > 0.01f, "plucked string must contain energy");
    for (i = 1024u; i < 4096u - 401u; ++i) {
        pitch_corr += output[i] * output[i + 401u];
        pitch_energy += output[i] * output[i];
    }
    CHECK(pitch_energy > 0.01f, "pitch test window must contain energy");
    CHECK(pitch_corr / pitch_energy > 0.65f,
          "110 Hz note should show strong correlation near its 401-sample period");
    CHECK(pm_string_voice_bytes() >= sizeof(voice.delay), "voice memory report must include delay line");
    printf("  prototype contract: ok (voice state %lu bytes, peak %.4f)\n",
           (unsigned long)pm_string_voice_bytes(), (double)peak);
    return 0;
}

/* DSP-002: the fundamental is where it was asked to be, between integer delays too. */
static int test_tuning(void)
{
    static const double NOTES[] = {27.5, 41.2, 55.0, 82.41, 110.0, 146.83, 220.0, 329.63, 440.0, 659.26, 880.0, 1318.5, 1760.0};
    pm_string_params_t params;
    double worst = 0.0;
    uint32_t i, variant;
    for (variant = 0; variant < 3u; ++variant) {
        pm_string_default_params(&params);
        params.decay_t60 = 6.0f;
        params.brightness = variant == 0u ? 0.0f : variant == 1u ? 0.6f : 1.0f;
        for (i = 0; i < sizeof NOTES / sizeof NOTES[0]; ++i) {
            double cents;
            CHECK(play(&params, 99u + i, (float)NOTES[i], 0.8f, buffer, SECOND) == 0, "note should start");
            cents = t_cents(t_pitch(buffer + 4410u, 30000u, NOTES[i]), NOTES[i]);
            if (fabs(cents) > worst)
                worst = fabs(cents);
            CHECK(fabs(cents) < 3.0, "fundamental should be within 3 cents");
        }
    }
    printf("  tuning: ok (27.5 Hz .. 1760 Hz, worst %.2f cents)\n", worst);
    return 0;
}

/* DSP-003: decay follows the asked time, brightness controls the highs, nothing grows. */
static int test_damping(void)
{
    pm_string_params_t params;
    double early, late, drop, dark_highs, bright_highs;
    uint32_t i;
    double peak_first, peak_later;

    pm_string_default_params(&params);
    params.decay_t60 = 1.0f;
    params.brightness = 1.0f;           /* no extra loss: the decay is the feedback's alone */
    CHECK(play(&params, 5u, 220.0f, 0.8f, buffer, SECOND) == 0, "note should start");
    /* (the fundamental: the highs also lose a little to the fractional delay) */
    early = t_tone(buffer + 2205u, 4410u, 220.0);
    late = t_tone(buffer + 2205u + 22050u, 4410u, 220.0);
    drop = -t_db(late / early);
    CHECK(drop > 26.0 && drop < 34.0, "a 1 s T60 should lose 30 dB in half a second");

    params.decay_t60 = 4.0f;
    params.brightness = 0.0f;
    CHECK(play(&params, 5u, 220.0f, 0.8f, buffer, SECOND) == 0, "note should start");
    dark_highs = t_tone(buffer + 26460u, 8192u, 220.0 * 12.0) / t_tone(buffer + 26460u, 8192u, 220.0);
    params.brightness = 1.0f;
    CHECK(play(&params, 5u, 220.0f, 0.8f, buffer, SECOND) == 0, "note should start");
    bright_highs = t_tone(buffer + 26460u, 8192u, 220.0 * 12.0) / t_tone(buffer + 26460u, 8192u, 220.0);
    CHECK(bright_highs > 3.0 * dark_highs, "brightness should keep the high partials longer");

    /* the longest decay there is, for a long time: the level only ever falls */
    params.decay_t60 = 60.0f;
    params.brightness = 1.0f;
    params.stiffness = 1.0f;
    {
        static pm_string_voice_t voice;
        pm_string_init(&voice, 31u);
        pm_string_configure(&voice, &params);
        CHECK(pm_string_note_on(&voice, 82.41f, 0.9998f, 1.0f) == 0, "note should start");
        clear(buffer, SECOND);
        pm_string_render(&voice, buffer, SECOND);
        peak_first = t_peak(buffer, SECOND);
        for (i = 0; i < 60u; ++i) {
            clear(buffer, SECOND);
            pm_string_render(&voice, buffer, SECOND);
            CHECK(t_finite(buffer, SECOND), "a long render must remain finite");
        }
        peak_later = t_peak(buffer, SECOND);
        CHECK(peak_later <= peak_first * 1.001, "a string must never gain level by itself");
        CHECK(peak_later > 0.0, "the longest decay should still sound after a minute");
    }
    printf("  damping: ok (1 s T60 lost %.1f dB in 0.5 s; bright/dark highs %.1fx; 61 s stable)\n",
           drop, bright_highs / dark_highs);
    return 0;
}

/* DSP-004: excitation is reproducible from the seed, per style. */
static int test_excitation(void)
{
    pm_string_params_t params;
    uint32_t style, i;
    for (style = 0; style < PM_EXCITE_COUNT; ++style) {
        pm_string_default_params(&params);
        params.style = (uint8_t)style;
        params.hardness = 0.7f;
        params.decay_t60 = 3.0f;
        params.pluck_position = 0.2f;
        CHECK(play(&params, 4242u, 196.0f, 0.9f, buffer, 22050u) == 0, "note should start");
        CHECK(play(&params, 4242u, 196.0f, 0.9f, other, 22050u) == 0, "note should start");
        for (i = 0; i < 22050u; ++i)
            CHECK(buffer[i] == other[i], "the same seed must give the same samples");
        CHECK(t_finite(buffer, 22050u), "every style must remain finite");
        CHECK(t_rms(buffer, 22050u) > 0.01, "every style must sound");
        CHECK(t_peak(buffer, 22050u) < 6.0, "every style must stay bounded");
        CHECK(play(&params, 9999u, 196.0f, 0.9f, other, 22050u) == 0, "note should start");
        if (style == PM_EXCITE_HAMMER) {
            for (i = 0; i < 22050u; ++i)
                CHECK(buffer[i] == other[i], "a hammer strike has no noise in it: seeds do not matter");
        } else {
            uint32_t differ = 0u;
            for (i = 0; i < 22050u; ++i)
                differ += buffer[i] != other[i];
            /* (a slap's fret rattle clamps both to the same limit for a while) */
            CHECK(differ > (style == PM_EXCITE_SLAP ? 15000u : 20000u), "another seed must give another burst");
        }
    }
    /* successive notes on one voice differ, yet the sequence is reproducible */
    {
        static pm_string_voice_t a, b;
        pm_string_init(&a, 17u);
        pm_string_init(&b, 17u);
        for (i = 0; i < 3u; ++i) {
            uint32_t k;
            CHECK(pm_string_note_on(&a, 330.0f, 0.99f, 0.7f) == 0, "retrigger should start");
            CHECK(pm_string_note_on(&b, 330.0f, 0.99f, 0.7f) == 0, "retrigger should start");
            clear(buffer, 4096u);
            clear(other, 4096u);
            pm_string_render(&a, buffer, 4096u);
            pm_string_render(&b, other, 4096u);
            for (k = 0; k < 4096u; ++k)
                CHECK(buffer[k] == other[k], "retriggers must be deterministic");
        }
    }
    /* a hard pluck is brighter than a soft one (the 19th harmonic: the default
     * pluck at a quarter of the string has a node at every 4th) */
    pm_string_default_params(&params);
    params.style = PM_EXCITE_FINGER;
    params.decay_t60 = 3.0f;
    params.brightness = 1.0f;
    params.hardness = 0.1f;
    CHECK(play(&params, 3u, 110.0f, 0.8f, buffer, 8192u) == 0, "note should start");
    params.hardness = 1.0f;
    CHECK(play(&params, 3u, 110.0f, 0.8f, other, 8192u) == 0, "note should start");
    CHECK(t_tone(other, 8192u, 110.0 * 19.0) / t_tone(other, 8192u, 110.0) >
          4.0 * t_tone(buffer, 8192u, 110.0 * 19.0) / t_tone(buffer, 8192u, 110.0),
          "hardness should brighten the excitation");
    printf("  excitation: ok (%u styles deterministic, hardness brightens)\n", (unsigned)PM_EXCITE_COUNT);
    return 0;
}

/* DSP-007: stiffness makes the partials sharp. */
static int test_stiffness(void)
{
    pm_string_params_t params;
    double ideal, stiff, f0;
    pm_string_default_params(&params);
    params.decay_t60 = 8.0f;
    params.brightness = 1.0f;
    params.stiffness = 0.0f;
    CHECK(play(&params, 8u, 110.0f, 0.8f, buffer, SECOND) == 0, "note should start");
    ideal = t_peak_hz(buffer + 2205u, 32768u, 8.0 * 110.0 * 0.98, 8.0 * 110.0 * 1.06);
    CHECK(fabs(t_cents(ideal, 880.0)) < 4.0, "an ideal string's 8th partial is 8 times the fundamental");
    params.stiffness = 1.0f;
    CHECK(play(&params, 8u, 110.0f, 0.8f, buffer, SECOND) == 0, "note should start");
    f0 = t_peak_hz(buffer + 2205u, 32768u, 105.0, 115.0);
    CHECK(fabs(t_cents(f0, 110.0)) < 6.0, "a stiff string's fundamental stays in tune");
    stiff = t_peak_hz(buffer + 2205u, 32768u, 8.0 * 110.0 * 0.98, 8.0 * 110.0 * 1.06);
    CHECK(t_cents(stiff, 8.0 * f0) > 8.0, "a stiff string's 8th partial should be sharp");
    printf("  stiffness: ok (8th partial %+.1f cents sharp of 8 f0, fundamental %+.2f cents)\n",
           t_cents(stiff, 8.0 * f0), t_cents(f0, 110.0));
    return 0;
}

/* DSP-008 and DSP-010: the pluck position and the pickup position are combs. */
static int test_positions(void)
{
    pm_string_params_t params;
    const double hz = 110.25;           /* a 400-sample period: the middle is exactly 200 */
    double even_edge, even_middle, pickup_even, plain_even;

    pm_string_default_params(&params);
    params.decay_t60 = 6.0f;
    params.brightness = 1.0f;
    params.pluck_position = 0.13f;
    CHECK(play(&params, 21u, (float)hz, 0.8f, buffer, 16384u) == 0, "note should start");
    even_edge = t_tone(buffer + 2000u, 8192u, 2.0 * hz) / t_tone(buffer + 2000u, 8192u, hz);
    params.pluck_position = 0.5f;
    CHECK(play(&params, 21u, (float)hz, 0.8f, buffer, 16384u) == 0, "note should start");
    even_middle = t_tone(buffer + 2000u, 8192u, 2.0 * hz) / t_tone(buffer + 2000u, 8192u, hz);
    CHECK(even_middle < 0.1 * even_edge, "plucking the middle should remove the even harmonics");

    params.pluck_position = 0.0f;
    CHECK(play(&params, 21u, (float)hz, 0.8f, buffer, 16384u) == 0, "note should start");
    plain_even = t_tone(buffer + 2000u, 8192u, 2.0 * hz) / t_tone(buffer + 2000u, 8192u, hz);
    params.pickup_position = 0.5f;
    params.pickup_blend = 1.0f;
    CHECK(play(&params, 21u, (float)hz, 0.8f, buffer, 16384u) == 0, "note should start");
    pickup_even = t_tone(buffer + 2000u, 8192u, 2.0 * hz) / t_tone(buffer + 2000u, 8192u, hz);
    CHECK(pickup_even < 0.1 * plain_even, "a pickup at the middle should not hear the even harmonics");
    params.pickup_blend = 0.0f;
    CHECK(play(&params, 21u, (float)hz, 0.8f, other, 16384u) == 0, "note should start");
    params.pickup_position = 0.0f;
    CHECK(play(&params, 21u, (float)hz, 0.8f, buffer, 16384u) == 0, "note should start");
    {
        uint32_t i;
        for (i = 0; i < 16384u; ++i)
            CHECK(buffer[i] == other[i], "a pickup blended out must leave the string output as it is");
    }
    printf("  positions: ok (pluck at the middle: even harmonics %.0f dB; pickup at the middle: %.0f dB)\n",
           t_db(even_middle / even_edge), t_db(pickup_even / plain_even));
    return 0;
}

/* Release, bend and buzz: what changes while a note sounds. */
static int test_live_changes(void)
{
    static pm_string_voice_t voice, twin;
    pm_string_params_t params;
    double before, after, bent, step_plain, step_bend;
    uint32_t i;

    /* no release time: note-off does nothing (the first prototype's behaviour) */
    pm_string_init(&voice, 61u);
    pm_string_init(&twin, 61u);
    CHECK(pm_string_note_on(&voice, 220.0f, 0.997f, 0.8f) == 0, "note should start");
    CHECK(pm_string_note_on(&twin, 220.0f, 0.997f, 0.8f) == 0, "note should start");
    pm_string_note_off(&voice);
    clear(buffer, 8192u);
    clear(other, 8192u);
    pm_string_render(&voice, buffer, 8192u);
    pm_string_render(&twin, other, 8192u);
    for (i = 0; i < 8192u; ++i)
        CHECK(buffer[i] == other[i], "note-off without a release time must change nothing");

    /* with one: the string is damped */
    pm_string_default_params(&params);
    params.decay_t60 = 8.0f;
    params.release_t60 = 0.1f;
    params.brightness = 0.7f;
    pm_string_init(&voice, 61u);
    pm_string_configure(&voice, &params);
    CHECK(pm_string_note_on(&voice, 220.0f, 0.997f, 0.8f) == 0, "note should start");
    clear(buffer, SECOND);
    pm_string_render(&voice, buffer, 8820u);
    before = t_rms(buffer + 4410u, 4410u);
    pm_string_note_off(&voice);
    pm_string_render(&voice, buffer + 8820u, 17640u);
    after = t_rms(buffer + 8820u + 13230u, 4410u);
    CHECK(t_db(after / before) < -50.0, "a 0.1 s release should silence the string within 0.3 s");

    /* bend: two semitones up, no reset, no click */
    pm_string_default_params(&params);
    params.decay_t60 = 8.0f;
    params.brightness = 0.5f;
    pm_string_init(&voice, 62u);
    pm_string_configure(&voice, &params);
    CHECK(pm_string_note_on(&voice, 220.0f, 0.997f, 0.8f) == 0, "note should start");
    clear(buffer, SECOND * 2u);
    pm_string_render(&voice, buffer, 22050u);
    step_plain = t_max_step(buffer + 11025u, 11025u);
    pm_string_set_pitch_ratio(&voice, 1.122462f);
    pm_string_render(&voice, buffer + 22050u, 44100u);
    step_bend = t_max_step(buffer + 22049u, 4410u);
    bent = t_pitch(buffer + 22050u + 8820u, 30000u, 220.0 * 1.122462);
    CHECK(fabs(t_cents(bent, 220.0 * 1.122462)) < 4.0, "a two-semitone bend should land two semitones up");
    CHECK(step_bend < 1.5 * step_plain + 0.01, "a bend must not click");
    CHECK(t_rms(buffer + 22050u, 2205u) > 0.5 * t_rms(buffer + 19845u, 2205u), "a bend must not reset the string");
    pm_string_set_pitch_ratio(&voice, (float)NAN);
    pm_string_set_pitch_ratio(&voice, -1.0f);
    pm_string_set_pitch_ratio(&voice, 1000.0f);
    clear(buffer, 4096u);
    pm_string_render(&voice, buffer, 4096u);
    CHECK(t_finite(buffer, 4096u), "a wild bend must remain finite");

    /* buzz: the loop's limit holds, and takes energy out rather than adding it */
    pm_string_default_params(&params);
    params.decay_t60 = 8.0f;
    params.buzz = 1.0f;
    params.style = PM_EXCITE_SLAP;
    CHECK(play(&params, 63u, 55.0f, 1.0f, buffer, SECOND) == 0, "note should start");
    CHECK(t_finite(buffer, SECOND), "a buzzing string must remain finite");
    {
        /* the limit is on the string itself (what is heard is its force at
         * the bridge): once the excitation is over, nothing in the loop
         * swings further than it */
        pm_string_init(&twin, 63u);
        pm_string_configure(&twin, &params);
        CHECK(pm_string_note_on(&twin, 55.0f, 0.997f, 1.0f) == 0, "note should start");
        clear(other, SECOND);
        pm_string_render(&twin, other, 4410u);
        for (i = 0; i < 40u; ++i) {
            uint32_t k;
            pm_string_render(&twin, other, 1000u);
            for (k = 0; k < PM_STRING_DELAY_CAPACITY; ++k)
                CHECK(fabsf(twin.delay[k]) <= 0.151f, "the buzz limit must hold once the excitation is over");
        }
    }
    params.buzz = 0.0f;
    params.style = PM_EXCITE_PICK;
    CHECK(play(&params, 63u, 55.0f, 1.0f, other, SECOND) == 0, "note should start");
    CHECK(t_rms(buffer + 22050u, 22050u) < t_rms(other + 22050u, 22050u), "buzz must not add energy");

    /* a parameter update while sounding keeps the pitch and the note */
    pm_string_default_params(&params);
    params.decay_t60 = 8.0f;
    pm_string_init(&voice, 64u);
    pm_string_configure(&voice, &params);
    CHECK(pm_string_note_on(&voice, 330.0f, 0.997f, 0.8f) == 0, "note should start");
    clear(buffer, SECOND);
    pm_string_render(&voice, buffer, 8820u);
    params.brightness = 0.9f;
    params.pickup_position = 0.3f;
    params.pickup_blend = 0.5f;
    pm_string_update(&voice, &params);
    pm_string_render(&voice, buffer + 8820u, 35280u);
    CHECK(fabs(t_cents(t_pitch(buffer + 13230u, 22050u, 330.0), 330.0)) < 3.0,
          "an update while sounding must keep the pitch");
    printf("  live changes: ok (release %.0f dB in 0.3 s; bend %+.2f cents of target)\n",
           t_db(after / before), t_cents(bent, 220.0 * 1.122462));
    return 0;
}

/* Parameter extremes, including values that are not numbers. */
static int test_extremes(void)
{
    static pm_string_voice_t voice;
    static const float ODD[] = {0.0f, 1.0f, -1.0f, 0.5f, 1.0e9f, -1.0e9f, 1.0e-30f, 100.0f};
    uint32_t state = 0xBEEF1234u, trial, k;
    pm_string_init(&voice, 1u);
    for (trial = 0; trial < 400u; ++trial) {
        pm_string_params_t params;
        float *fields[10];
        float hz, velocity;
        fields[0] = &params.decay_t60;
        fields[1] = &params.release_t60;
        fields[2] = &params.brightness;
        fields[3] = &params.stiffness;
        fields[4] = &params.pluck_position;
        fields[5] = &params.hardness;
        fields[6] = &params.velocity_to_hardness;
        fields[7] = &params.pickup_position;
        fields[8] = &params.pickup_blend;
        fields[9] = &params.buzz;
        for (k = 0; k < 10u; ++k) {
            const uint32_t pick = t_random(&state) % 12u;
            *fields[k] = pick < 8u ? ODD[pick] : pick == 8u ? (float)NAN : pick == 9u ? (float)INFINITY :
                         pick == 10u ? -(float)INFINITY : t_uniform(&state);
        }
        params.style = (uint8_t)(t_random(&state) & 0xFFu);
        hz = 21.6f * (float)pow(2.0, (double)t_uniform(&state) * 9.9);
        velocity = t_uniform(&state);
        pm_string_configure(&voice, &params);
        if (pm_string_note_on(&voice, hz, t_uniform(&state), velocity) != 0)
            continue;
        clear(buffer, 8192u);
        pm_string_render(&voice, buffer, 4096u);
        pm_string_update(&voice, &params);
        if (trial & 1u)
            pm_string_note_off(&voice);
        pm_string_set_pitch_ratio(&voice, 0.25f + 3.75f * t_uniform(&state));
        pm_string_render(&voice, buffer + 4096u, 4096u);
        CHECK(t_finite(buffer, 8192u), "parameter extremes must not produce NaN or infinity");
        CHECK(t_peak(buffer, 8192u) < 8.0, "parameter extremes must not run away");
    }
    /* the edges of the frequency range */
    CHECK(play(0, 2u, 21.6f, 1.0f, buffer, 8192u) == 0, "the lowest note should start");
    CHECK(t_finite(buffer, 8192u) && t_rms(buffer, 8192u) > 0.01, "the lowest note should sound");
    CHECK(play(0, 2u, 22050.0f, 1.0f, buffer, 8192u) == 0, "the highest note should start");
    CHECK(t_finite(buffer, 8192u), "the highest note should remain finite");
    printf("  extremes: ok (400 random parameter sets, NaN and infinities included)\n");
    return 0;
}

int main(void)
{
    if (test_prototype() || test_tuning() || test_damping() || test_excitation() ||
        test_stiffness() || test_positions() || test_live_changes() || test_extremes())
        return 1;
    printf("string model: PASS\n");
    return 0;
}
