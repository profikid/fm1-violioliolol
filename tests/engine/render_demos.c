/* SPDX-License-Identifier: MIT
 * Render a short phrase on every factory instrument through the whole engine
 * (voices, body, output stage) to 16-bit mono WAV files, for listening and
 * for the timbre checks in tests/engine/timbre.py.
 *
 *   render_demos OUTDIR [INSTRUMENT]
 *
 * Plucked: three single notes (low, middle, high) at a firm velocity, one soft
 * note, then a chord let ring. Bowed: a held note, a short legato line, a
 * held note with vibrato from the mod wheel. Every note is in the instrument's
 * own range (its key octave).
 */
#include <stdio.h>
#include <string.h>
#include "engine.h"

#define RATE 44100u
#define BLOCK 256u

static pm_engine_t engine;
static float block[BLOCK];
static short pcm[RATE * 20u];
static uint32_t frames;

static void put32(FILE *f, uint32_t v)
{
    fputc((int)(v & 255u), f);
    fputc((int)(v >> 8 & 255u), f);
    fputc((int)(v >> 16 & 255u), f);
    fputc((int)(v >> 24 & 255u), f);
}

static void put16(FILE *f, uint32_t v)
{
    fputc((int)(v & 255u), f);
    fputc((int)(v >> 8 & 255u), f);
}

static void play(double seconds)
{
    uint32_t n = (uint32_t)(seconds * RATE / BLOCK), b, i;
    for (b = 0; b < n && frames + BLOCK <= sizeof pcm / sizeof pcm[0]; ++b) {
        pm_engine_render(&engine, block, BLOCK);
        for (i = 0; i < BLOCK; ++i)
            pcm[frames++] = (short)(block[i] * 32000.0f);
    }
}

static void on(int note, int velocity) { pm_engine_midi(&engine, 0x90u, (uint32_t)note, (uint32_t)velocity); }
static void off(int note) { pm_engine_midi(&engine, 0x80u, (uint32_t)note, 0u); }

static int write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f)
        return -1;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36u + frames * 2u);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16u);
    put16(f, 1u);
    put16(f, 1u);
    put32(f, RATE);
    put32(f, RATE * 2u);
    put16(f, 2u);
    put16(f, 16u);
    fwrite("data", 1, 4, f);
    put32(f, frames * 2u);
    for (i = 0; i < frames; ++i)
        put16(f, (uint32_t)(uint16_t)pcm[i]);
    return fclose(f);
}

static void phrase(const pm_patch_t *patch)
{
    const int root = 60 + 12 * patch->key_octave;   /* C of the instrument's own octave */
    frames = 0u;
    pm_engine_init(&engine, 0x51A1u);
    pm_engine_set_patch(&engine, patch);
    play(0.05);
    if (patch->engine == PM_ENGINE_BOW) {
        on(root - 5, 100);
        play(2.0);
        off(root - 5);
        play(0.4);
        {
            static const int LINE[6] = {0, 2, 4, 5, 7, 9};
            int i;
            for (i = 0; i < 6; ++i) {           /* legato: the next note before the last is let go */
                on(root + LINE[i], 90);
                play(0.04);
                if (i)
                    off(root + LINE[i - 1]);
                play(0.36);
            }
            off(root + 9);
        }
        play(0.5);
        pm_engine_midi(&engine, 0xB0u, 1u, 100u);
        on(root + 7, 110);
        play(2.5);
        off(root + 7);
        pm_engine_midi(&engine, 0xB0u, 1u, 0u);
        play(1.5);
    } else {
        static const int NOTES[3] = {-12, 0, 12};
        int i;
        for (i = 0; i < 3; ++i) {
            on(root + NOTES[i], 105);
            play(1.4);
            off(root + NOTES[i]);
            play(0.25);
        }
        on(root, 40);
        play(1.2);
        off(root);
        play(0.25);
        on(root - 12, 95);
        play(0.03);
        on(root - 5, 95);
        play(0.03);
        on(root, 95);
        play(0.03);
        on(root + 4, 95);
        play(3.0);
        off(root - 12);
        off(root - 5);
        off(root);
        off(root + 4);
        play(1.0);
    }
}

int main(int argc, char **argv)
{
    uint32_t i;
    char path[512], name[PM_PATCH_NAME + 1];
    if (argc < 2) {
        fprintf(stderr, "usage: render_demos OUTDIR [INSTRUMENT]\n");
        return 2;
    }
    for (i = 0; i < pm_factory_count(); ++i) {
        const pm_patch_t *patch = pm_factory_patch(i);
        uint32_t k;
        if (argc > 2 && strcmp(argv[2], patch->name))
            continue;
        for (k = 0; k <= PM_PATCH_NAME && patch->name[k]; ++k)
            name[k] = patch->name[k] == ' ' ? '_' : (char)(patch->name[k] | 0x20);
        name[k] = 0;
        phrase(patch);
        snprintf(path, sizeof path, "%s/%02u_%s.wav", argv[1], (unsigned)i, name);
        if (write_wav(path))
            return 1;
        printf("%s %.1f s, faults %u, limiter %u\n", path, (double)frames / RATE,
               (unsigned)engine.faults, (unsigned)engine.limiter_q8);
    }
    return 0;
}
