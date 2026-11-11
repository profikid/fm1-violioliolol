/* SPDX-License-Identifier: MIT
 * Host-side throughput harness. Device timing must use fm1_audio_stats_read()
 * and real FM-1 audio blocks; host numbers are not hardware performance claims.
 */
#include <stdio.h>
#include <time.h>
#include "string_model.h"

#define BLOCK_FRAMES 128u
#define VOICE_COUNTS 4u
#define ITERATIONS 3000u

int main(void)
{
    static const unsigned counts[VOICE_COUNTS] = {1u, 2u, 4u, 8u};
    static pm_string_voice_t voices[8];
    float block[BLOCK_FRAMES];
    unsigned c, v, i;
    printf("voices,frames,seconds,ns_per_frame,voice_state_bytes\n");

    for (c = 0; c < VOICE_COUNTS; ++c) {
        const unsigned count = counts[c];
        clock_t start, finish;
        for (v = 0; v < count; ++v) {
            pm_string_init(&voices[v], 0x1234u + v);
            (void)pm_string_note_on(&voices[v], 55.0f * (float)(v + 1u),
                                    0.997f, 0.2f);
        }
        start = clock();
        for (i = 0; i < ITERATIONS; ++i) {
            unsigned n;
            for (n = 0; n < BLOCK_FRAMES; ++n)
                block[n] = 0.0f;
            for (v = 0; v < count; ++v)
                pm_string_render(&voices[v], block, BLOCK_FRAMES);
        }
        finish = clock();
        {
            const double seconds = (double)(finish - start) / (double)CLOCKS_PER_SEC;
            const double frames = (double)ITERATIONS * (double)BLOCK_FRAMES;
            const double ns = seconds > 0.0 ? seconds * 1.0e9 / frames : 0.0;
            printf("%u,%.0f,%.6f,%.2f,%lu\n", count, frames, seconds, ns,
                   (unsigned long)(count * pm_string_voice_bytes()));
        }
    }
    return 0;
}
