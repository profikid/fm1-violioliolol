/* SPDX-License-Identifier: MIT */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "string_model.h"

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        return 1; \
    } \
} while (0)

int main(void)
{
    pm_string_voice_t voice;
    pm_string_voice_t quiet_voice;
    pm_string_voice_t loud_voice;
    float output[4096] = {0.0f};
    float quiet_output[512] = {0.0f};
    float loud_output[512] = {0.0f};
    float peak = 0.0f;
    float energy = 0.0f;
    float pitch_corr = 0.0f;
    float pitch_energy = 0.0f;
    uint32_t i;

    pm_string_init(&voice, 12345u);
    CHECK(pm_string_note_on(&voice, 110.0f, 0.997f, 0.8f) == 0,
          "valid note should start");
    CHECK(pm_string_note_on(&voice, 0.0f, 0.997f, 0.8f) == -1,
          "zero frequency should be rejected");
    CHECK(pm_string_note_on(&voice, 110.0f, NAN, 0.8f) == -1,
          "NaN decay should be rejected");
    CHECK(pm_string_note_on(&voice, 110.0f, 0.997f, INFINITY) == -1,
          "infinite velocity should be rejected");

    /* Identical excitation must scale linearly with velocity, not velocity^2. */
    pm_string_init(&quiet_voice, 777u);
    pm_string_init(&loud_voice, 777u);
    CHECK(pm_string_note_on(&quiet_voice, 220.0f, 0.997f, 0.25f) == 0,
          "quarter-velocity note should start");
    CHECK(pm_string_note_on(&loud_voice, 220.0f, 0.997f, 0.50f) == 0,
          "half-velocity note should start");
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
    CHECK(pm_string_voice_bytes() >= sizeof(voice.delay),
          "voice memory report must include delay line");

    printf("string model: PASS (voice state %lu bytes, peak %.4f)\n",
           (unsigned long)pm_string_voice_bytes(), (double)peak);
    return 0;
}
