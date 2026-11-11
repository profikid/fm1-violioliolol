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
    float output[4096] = {0.0f};
    float peak = 0.0f;
    float energy = 0.0f;
    uint32_t i;

    pm_string_init(&voice, 12345u);
    CHECK(pm_string_note_on(&voice, 110.0f, 0.997f, 0.8f) == 0,
          "valid note should start");
    CHECK(pm_string_note_on(&voice, 0.0f, 0.997f, 0.8f) == -1,
          "zero frequency should be rejected");

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
    CHECK(pm_string_voice_bytes() >= sizeof(voice.delay),
          "voice memory report must include delay line");

    printf("string model: PASS (voice state %lu bytes, peak %.4f)\n",
           (unsigned long)pm_string_voice_bytes(), (double)peak);
    return 0;
}
