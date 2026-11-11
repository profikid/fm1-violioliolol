/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: first plucked-string voice prototype.
 *
 * Platform-independent DSP. No allocation, I/O, locks or platform headers.
 */
#ifndef FM1_STRING_MODEL_H
#define FM1_STRING_MODEL_H

#include <stdint.h>

#define PM_STRING_SAMPLE_RATE 44100u
#define PM_STRING_DELAY_CAPACITY 2048u

typedef struct {
    float delay[PM_STRING_DELAY_CAPACITY];
    float delay_samples;
    float feedback;
    float damping_state;
    float output_level;
    uint32_t rng;
    uint16_t write_index;
    uint8_t active;
} pm_string_voice_t;

/* Initialise a voice. Call from control context, not the audio callback. */
void pm_string_init(pm_string_voice_t *voice, uint32_t seed);

/* Start/retrigger a plucked string.
 * frequency_hz: supported range is approximately 21.6 Hz .. 22.05 kHz.
 * decay: feedback coefficient, clamped internally to 0.80 .. 0.9998.
 * velocity: linear output level, clamped to 0 .. 1.
 * Returns 0 on success, -1 for invalid input.
 */
int pm_string_note_on(pm_string_voice_t *voice, float frequency_hz,
                      float decay, float velocity);

/* Release is intentionally a no-op for this naturally decaying model. */
void pm_string_note_off(pm_string_voice_t *voice);

/* Add frames to a mono accumulator. Safe for a preallocated audio buffer. */
void pm_string_render(pm_string_voice_t *voice, float *mono, uint32_t frames);

/* Number of bytes required by one voice; useful in memory budget reports. */
uint32_t pm_string_voice_bytes(void);

#endif
