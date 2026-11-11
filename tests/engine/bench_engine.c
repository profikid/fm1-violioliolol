/* SPDX-License-Identifier: MIT
 * Host-side throughput harness: the whole engine (voices, body, output
 * stage) at 1, 2, 4, 8 and 12 voices, per model. A repeatable regression
 * signal only: device timing must be read on a real FM-1 (the PERF page:
 * plat_cpu_pct, plat_xruns). Host numbers are not hardware performance claims.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "engine.h"

#define BLOCK_FRAMES 256u               /* the FM-1's block (APP_BLOCK) */
#define ITERATIONS 1500u

static pm_engine_t engine;

int main(void)
{
    static const unsigned counts[] = {1u, 2u, 4u, 8u, 12u};
    static const char *const names[] = {"NYLON", "STEEL", "DRIVE ELEC", "VIOLIN", "CELLO"};
    float block[BLOCK_FRAMES];
    unsigned c, p, i, k;
    printf("instrument,model,voices,frames,seconds,ns_per_frame,realtime_ratio\n");
    for (p = 0; p < sizeof names / sizeof names[0]; ++p) {
        const pm_patch_t *patch = 0;
        for (k = 0; k < pm_factory_count(); ++k)
            if (!strcmp(pm_factory_patch(k)->name, names[p]))
                patch = pm_factory_patch(k);
        if (!patch)
            return 1;
        for (c = 0; c < sizeof counts / sizeof counts[0]; ++c) {
            clock_t start, finish;
            pm_engine_init(&engine, 0x1234u);
            pm_engine_set_patch(&engine, patch);
            pm_engine_post(&engine, PM_EV_BENCH, 0u, counts[c]);
            for (i = 0; i < 8u; ++i)
                pm_engine_render(&engine, block, BLOCK_FRAMES);
            start = clock();
            for (i = 0; i < ITERATIONS; ++i)
                pm_engine_render(&engine, block, BLOCK_FRAMES);
            finish = clock();
            if (engine.active_voices != counts[c])
                return 1;
            {
                const double seconds = (double)(finish - start) / (double)CLOCKS_PER_SEC;
                const double frames = (double)ITERATIONS * (double)BLOCK_FRAMES;
                const double ns = seconds > 0.0 ? seconds * 1.0e9 / frames : 0.0;
                printf("%s,%s,%u,%.0f,%.6f,%.2f,%.0f\n", names[p],
                       patch->engine == PM_ENGINE_BOW ? "bowed" : "plucked", counts[c], frames, seconds, ns,
                       seconds > 0.0 ? frames / 44100.0 / seconds : 0.0);
            }
        }
    }
    printf("memory: engine %lu bytes; voice slot %lu; plucked voice %lu; bowed voice %lu\n",
           (unsigned long)pm_engine_bytes(), (unsigned long)pm_engine_voice_slot_bytes(),
           (unsigned long)pm_string_voice_bytes(), (unsigned long)pm_bowed_voice_bytes());
    return 0;
}
