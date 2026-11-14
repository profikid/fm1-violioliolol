/* SPDX-License-Identifier: MIT
 * Native tests of the bowed-string voice: DSP-011 .. DSP-015.
 */
#include "bowed_model.h"
#include "test_util.h"

#define SECOND 44100u
static float buffer[SECOND * 4u];
static float other[SECOND * 4u];
static pm_bowed_voice_t voice, twin;

static void clear(float *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i)
        x[i] = 0.0f;
}

static int bow(const pm_bowed_params_t *params, float hz, float velocity, float *out, uint32_t frames)
{
    pm_bowed_init(&voice);
    if (params)
        pm_bowed_configure(&voice, params);
    if (pm_bowed_note_on(&voice, hz, velocity) != 0)
        return -1;
    clear(out, frames);
    pm_bowed_render(&voice, out, frames);
    return 0;
}

/* The share of the level that is above the 6th harmonic: a brightness figure. */
static double highs(const float *x, uint32_t n, double hz)
{
    double low = 0.0, high = 0.0;
    int k;
    for (k = 1; k <= 16; ++k) {
        const double mag = t_tone(x, n, hz * (double)k);
        if (k <= 6)
            low += mag * mag;
        else
            high += mag * mag;
    }
    return sqrt(high / (low + 1.0e-30));
}

/* DSP-011: the bow sustains a stable tone at the asked pitch, across the range. */
static int test_sustain_and_pitch(void)
{
    static const double NOTES[] = {41.2, 65.41, 98.0, 146.83, 196.0, 293.66, 440.0, 659.26, 880.0, 1318.5, 1760.0, 2637.0};
    static const float PRESSURES[] = {0.15f, 0.5f, 0.9f};
    double worst = 0.0, quietest = 10.0, loudest = 0.0;
    uint32_t i, k;
    for (i = 0; i < sizeof NOTES / sizeof NOTES[0]; ++i) {
        for (k = 0; k < 3u; ++k) {
            pm_bowed_params_t params;
            double first, second, cents;
            pm_bowed_default_params(&params);
            params.bow_pressure = PRESSURES[k];
            CHECK(bow(&params, (float)NOTES[i], 0.8f, buffer, SECOND * 3u) == 0, "note should start");
            CHECK(t_finite(buffer, SECOND * 3u), "a bowed note must remain finite");
            first = t_rms(buffer + SECOND, SECOND);
            second = t_rms(buffer + SECOND * 2u, SECOND);
            CHECK(second > 0.08, "the bow must sustain the string");
            CHECK(fabs(t_db(second / first)) < 1.5, "a held bow must give a steady level");
            CHECK(t_peak(buffer, SECOND * 3u) < 1.6, "a bowed voice must stay near full scale");
            cents = t_cents(t_pitch(buffer + SECOND, 30000u, NOTES[i]), NOTES[i]);
            if (fabs(cents) > worst)
                worst = fabs(cents);
            CHECK(fabs(cents) < 10.0, "a bowed note should be within 10 cents");
            if (second < quietest)
                quietest = second;
            if (second > loudest)
                loudest = second;
        }
    }
    printf("  sustain and pitch: ok (41 Hz .. 2637 Hz at three pressures, worst %.1f cents, level %.2f .. %.2f rms)\n",
           worst, quietest, loudest);
    return 0;
}

/* DSP-012, DSP-013, DSP-014: pressure, speed and position each change the sound. */
static int test_bow_parameters(void)
{
    pm_bowed_params_t params;
    double slow, fast, light, heavy, near_bridge, near_board;

    pm_bowed_default_params(&params);
    params.bow_speed = 0.1f;
    CHECK(bow(&params, 196.0f, 0.8f, buffer, SECOND * 2u) == 0, "note should start");
    slow = t_rms(buffer + SECOND, SECOND);
    params.bow_speed = 1.0f;
    CHECK(bow(&params, 196.0f, 0.8f, buffer, SECOND * 2u) == 0, "note should start");
    fast = t_rms(buffer + SECOND, SECOND);
    CHECK(fast > 1.5 * slow, "a faster bow should put more energy in");

    /* velocity is bow speed too */
    pm_bowed_default_params(&params);
    CHECK(bow(&params, 196.0f, 0.2f, buffer, SECOND * 2u) == 0, "note should start");
    slow = t_rms(buffer + SECOND, SECOND);
    CHECK(bow(&params, 196.0f, 1.0f, buffer, SECOND * 2u) == 0, "note should start");
    CHECK(t_rms(buffer + SECOND, SECOND) > 1.3 * slow, "a harder note should be louder");

    pm_bowed_default_params(&params);
    params.bow_pressure = 0.1f;
    CHECK(bow(&params, 196.0f, 0.8f, buffer, SECOND * 2u) == 0, "note should start");
    light = highs(buffer + SECOND, 16384u, t_pitch(buffer + SECOND, 30000u, 196.0));
    params.bow_pressure = 0.95f;
    CHECK(bow(&params, 196.0f, 0.8f, buffer, SECOND * 2u) == 0, "note should start");
    heavy = highs(buffer + SECOND, 16384u, t_pitch(buffer + SECOND, 30000u, 196.0));
    CHECK(fabs(t_db(heavy / light)) > 1.5, "bow pressure should change the harmonic balance");

    /* near the bridge a string needs more bow force to keep its Helmholtz
     * motion (Schelleng's minimum force), as a player gives it */
    pm_bowed_default_params(&params);
    params.bow_position = 0.04f;
    params.bow_pressure = 0.75f;
    CHECK(bow(&params, 196.0f, 0.8f, buffer, SECOND * 2u) == 0, "note should start");
    near_bridge = highs(buffer + SECOND, 16384u, t_pitch(buffer + SECOND, 30000u, 196.0));
    pm_bowed_default_params(&params);
    params.bow_position = 0.3f;
    CHECK(bow(&params, 196.0f, 0.8f, buffer, SECOND * 2u) == 0, "note should start");
    near_board = highs(buffer + SECOND, 16384u, t_pitch(buffer + SECOND, 30000u, 196.0));
    CHECK(near_bridge > 1.15 * near_board, "bowing near the bridge should be brighter than over the fingerboard");

    printf("  bow parameters: ok (speed x%.1f level; pressure %+.1f dB highs; bridge/fingerboard x%.1f highs)\n",
           fast / slow, t_db(heavy / light), near_bridge / near_board);
    return 0;
}

/* DSP-015: vibrato modulates the pitch by the asked depth, and only when asked. */
static int test_vibrato(void)
{
    pm_bowed_params_t params;
    double lo = 1.0e9, hi = 0.0, spread_off = 0.0, spread_on = 0.0;
    uint32_t w;
    int pass;
    for (pass = 0; pass < 2; ++pass) {
        pm_bowed_default_params(&params);
        params.vibrato_depth = pass ? 0.5f : 0.0f;
        params.vibrato_rate_hz = 5.0f;
        CHECK(bow(&params, 440.0f, 0.8f, buffer, SECOND * 3u) == 0, "note should start");
        lo = 1.0e9;
        hi = 0.0;
        for (w = 0; w < 40u; ++w) {        /* 25 ms windows over a second */
            const double hz = t_pitch(buffer + SECOND * 2u + w * 1102u, 1102u, 440.0);
            if (hz < lo)
                lo = hz;
            if (hz > hi)
                hi = hz;
        }
        if (pass)
            spread_on = t_cents(hi, lo);
        else
            spread_off = t_cents(hi, lo);
    }
    CHECK(spread_off < 12.0, "without vibrato the pitch should hold still");
    CHECK(spread_on > 50.0 && spread_on < 130.0, "half a semitone of vibrato should swing about a semitone in all");
    printf("  vibrato: ok (pitch spread %.0f cents off, %.0f cents at 0.5 semitone depth)\n", spread_off, spread_on);
    return 0;
}

/* Release, legato, bend, parameter transitions. */
static int test_transitions(void)
{
    pm_bowed_params_t params;
    uint32_t state = 0x1234ABCDu, block, i;
    double step, pitch;

    /* the bow lifted: the string rings out and falls silent */
    pm_bowed_default_params(&params);
    CHECK(bow(&params, 220.0f, 0.8f, buffer, SECOND) == 0, "note should start");
    pm_bowed_note_off(&voice);
    clear(buffer, SECOND * 2u);
    pm_bowed_render(&voice, buffer, SECOND * 2u);
    CHECK(t_peak(buffer + SECOND, SECOND) < 1.0e-3, "a released bowed string should fall silent within a second");
    CHECK(t_max_step(buffer, 4410u) < 0.5, "lifting the bow must not click");

    /* legato: a new pitch on a sounding voice, no reset */
    CHECK(bow(&params, 220.0f, 0.8f, buffer, SECOND) == 0, "note should start");
    CHECK(pm_bowed_note_on(&voice, 293.66f, 0.8f) == 0, "a legato note should start");
    clear(buffer, SECOND * 2u);
    pm_bowed_render(&voice, buffer, SECOND * 2u);
    CHECK(t_rms(buffer, 2205u) > 0.05, "a legato change must not reset the string");
    pitch = t_pitch(buffer + SECOND, 30000u, 293.66);
    CHECK(fabs(t_cents(pitch, 293.66)) < 10.0, "a legato change should reach the new pitch");

    /* bend: a whole tone up */
    CHECK(bow(&params, 220.0f, 0.8f, buffer, SECOND) == 0, "note should start");
    pm_bowed_set_pitch_ratio(&voice, 1.122462f);
    clear(buffer, SECOND * 2u);
    pm_bowed_render(&voice, buffer, SECOND * 2u);
    pitch = t_pitch(buffer + SECOND, 30000u, 220.0 * 1.122462);
    CHECK(fabs(t_cents(pitch, 220.0 * 1.122462)) < 10.0, "a two-semitone bend should land two semitones up");
    CHECK(t_rms(buffer, 2205u) > 0.05, "a bend must not reset the string");

    /* every parameter thrown around every block for ten seconds, and a new
     * pitch now and then: nothing leaves the bounds, nothing steps */
    pm_bowed_init(&voice);
    CHECK(pm_bowed_note_on(&voice, 196.0f, 0.9f) == 0, "note should start");
    step = 0.0;
    for (block = 0; block < 1720u; ++block) {
        float last = block ? buffer[255] : 0.0f;
        params.bow_pressure = t_uniform(&state);
        params.bow_speed = t_uniform(&state);
        params.bow_position = 0.02f + 0.43f * t_uniform(&state);
        params.brightness = t_uniform(&state);
        params.attack_seconds = 0.002f + t_uniform(&state);
        params.release_seconds = 0.005f + t_uniform(&state);
        params.vibrato_depth = t_uniform(&state);
        params.vibrato_rate_hz = 0.5f + 11.5f * t_uniform(&state);
        pm_bowed_configure(&voice, &params);
        if ((block & 63u) == 0u)
            CHECK(pm_bowed_note_on(&voice, 30.0f + 3000.0f * t_uniform(&state) * t_uniform(&state), t_uniform(&state)) == 0,
                  "a legato note should start");
        if ((block & 127u) == 100u)
            pm_bowed_note_off(&voice);
        pm_bowed_set_pitch_ratio(&voice, 0.5f + 1.5f * t_uniform(&state));
        clear(buffer, 256u);
        pm_bowed_render(&voice, buffer, 256u);
        CHECK(t_finite(buffer, 256u), "parameter transitions must remain finite");
        CHECK(t_peak(buffer, 256u) < 14.0, "parameter transitions must stay bounded");
        if (fabs((double)buffer[0] - (double)last) > step)
            step = fabs((double)buffer[0] - (double)last);
    }
    /* values that are not numbers are replaced, not used */
    params.bow_pressure = (float)NAN;
    params.bow_speed = (float)INFINITY;
    params.bow_position = -(float)INFINITY;
    params.brightness = (float)NAN;
    params.attack_seconds = 0.0f;
    params.release_seconds = (float)NAN;
    params.vibrato_depth = 1.0e9f;
    params.vibrato_rate_hz = (float)NAN;
    pm_bowed_configure(&voice, &params);
    pm_bowed_set_pitch_ratio(&voice, (float)NAN);
    CHECK(pm_bowed_note_on(&voice, 440.0f, 1.0f) == 0, "note should start");
    clear(buffer, SECOND);
    pm_bowed_render(&voice, buffer, SECOND);
    CHECK(t_finite(buffer, SECOND), "parameters that are not numbers must not reach the string");
    CHECK(t_peak(buffer, SECOND) < 14.0, "parameters that are not numbers must not run away");
    for (i = 0; i < PM_BOWED_NECK_CAPACITY; ++i)
        CHECK(fabsf(voice.neck[i]) <= 4.0f, "the string's state has a hard bound");

    printf("  transitions: ok (release, legato, bend; 10 s of random parameters bounded)\n");
    return 0;
}

static int test_contract(void)
{
    uint32_t i;
    pm_bowed_init(&voice);
    CHECK(pm_bowed_note_on(&voice, 10.0f, 0.5f) == -1, "a frequency below the string's range should be rejected");
    CHECK(pm_bowed_note_on(&voice, 20000.0f, 0.5f) == -1, "a frequency above the string's range should be rejected");
    CHECK(pm_bowed_note_on(&voice, (float)NAN, 0.5f) == -1, "NaN frequency should be rejected");
    CHECK(pm_bowed_note_on(&voice, 440.0f, (float)NAN) == -1, "NaN velocity should be rejected");
    CHECK(pm_bowed_note_on(&voice, 440.0f, 1.5f) == -1, "velocity above 1 should be rejected");
    CHECK(pm_bowed_note_on(0, 440.0f, 0.5f) == -1, "a null voice should be rejected");
    clear(buffer, 4096u);
    pm_bowed_render(&voice, buffer, 4096u);
    CHECK(t_peak(buffer, 4096u) == 0.0, "a voice that never started must be silent");

    /* deterministic: no noise in the model at all */
    pm_bowed_init(&voice);
    pm_bowed_init(&twin);
    CHECK(pm_bowed_note_on(&voice, 330.0f, 0.7f) == 0 && pm_bowed_note_on(&twin, 330.0f, 0.7f) == 0,
          "notes should start");
    clear(buffer, SECOND);
    clear(other, SECOND);
    pm_bowed_render(&voice, buffer, SECOND);
    pm_bowed_render(&twin, other, SECOND);
    for (i = 0; i < SECOND; ++i)
        CHECK(buffer[i] == other[i], "two bowed voices given the same must sound the same");
    /* the attack time is the bow reaching its speed */
    {
        pm_bowed_params_t params;
        double quick, slow;
        pm_bowed_default_params(&params);
        params.attack_seconds = 0.005f;
        CHECK(bow(&params, 220.0f, 0.8f, buffer, SECOND) == 0, "note should start");
        quick = t_rms(buffer + 4410u, 4410u);
        params.attack_seconds = 0.5f;
        CHECK(bow(&params, 220.0f, 0.8f, buffer, SECOND) == 0, "note should start");
        slow = t_rms(buffer + 4410u, 4410u);
        CHECK(quick > 2.0 * slow, "a slow attack should come in later than a quick one");
    }
    printf("  contract: ok (validation, determinism, attack; voice state %lu bytes)\n",
           (unsigned long)pm_bowed_voice_bytes());
    return 0;
}

int main(void)
{
    if (test_contract() || test_sustain_and_pitch() || test_bow_parameters() || test_vibrato() ||
        test_transitions())
        return 1;
    printf("bowed model: PASS\n");
    return 0;
}
