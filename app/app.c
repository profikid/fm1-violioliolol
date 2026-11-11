/* SPDX-License-Identifier: MIT
 * fm1-violioliolol first device-side DSP benchmark app.
 *
 * K1 encoder selects 1, 2, 4 or 8 active plucked-string voices.
 * The first N key LEDs indicate the current load. This is a measurement
 * instrument, not the finished playable UI.
 */
#include "app.h"
#include "../dsp/string_model.h"

/* fm1-base host builds use the app unity unit; device builds compile this DSP at -O2 via fm1.toml. */
#ifdef PLAT_HOST
#include "../dsp/string_model.c"
#endif

#define BENCH_MAX_VOICES 8u
#define BENCH_MAX_BLOCK 256u

static pm_string_voice_t voices[BENCH_MAX_VOICES] PLAT_POOL;
static float mix[ BENCH_MAX_BLOCK ];
static uint8_t active_voices = 1u;
static uint8_t initialized;

static const float bench_frequency[BENCH_MAX_VOICES] = {
    55.0f, 82.4069f, 110.0f, 146.832f,
    196.0f, 220.0f, 293.665f, 392.0f
};

static void append_text(char *destination, const char *source)
{
    while (*destination)
        ++destination;
    while (*source)
        *destination++ = *source++;
    *destination = 0;
}

static void uint_text(uint32_t value, char *destination)
{
    char digits[10];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < 10u);
    while (count)
        *destination++ = digits[--count];
    *destination = 0;
}

static void draw_status(void)
{
    char voices[24] = "VOICES ";
    char cpu[32] = "CPU ";
    char xruns[24] = "XRUNS ";
    char number[12];

    uint_text(active_voices, number);
    append_text(voices, number);
    uint_text(plat_cpu_pct(), number);
    append_text(cpu, number);
    append_text(cpu, "%  ");
#ifdef PLAT_HOST
    append_text(cpu, "HOST");
#else
    uint_text(plat_cpu_khz(), number);
    append_text(cpu, number);
    append_text(cpu, " kHz");
#endif
    uint_text(plat_xruns(), number);
    append_text(xruns, number);

    cv_begin(240, 80, C_BLACK);
    cv_text(8, 4, &FONT_B, APP_NAME, C_WHITE);
    cv_text(8, 26, &FONT_S, voices, C_HI);
    cv_text(8, 46, &FONT_XS, cpu, C_GRAY);
    cv_text(8, 62, &FONT_XS, xruns, C_GRAY);
    cv_blit(0, 0);
}

static void bench_set_voices(uint8_t count)
{
    uint32_t i;
    if (count != 1u && count != 2u && count != 4u && count != 8u)
        return;
    active_voices = count;
    for (i = 0; i < BENCH_MAX_VOICES; ++i) {
        if (i < active_voices)
            (void)pm_string_note_on(&voices[i], bench_frequency[i], 0.997f, 0.10f);
        else
            pm_string_note_off(&voices[i]);
    }
    plat_leds(0u, (1u << active_voices) - 1u);
}

void app_init(void)
{
    uint32_t i;
    for (i = 0; i < BENCH_MAX_VOICES; ++i)
        pm_string_init(&voices[i], 0x51A1u + i * 7919u);
    initialized = 1u;
    bench_set_voices(1u);
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, v;
    float gain;
    if (!initialized || !lr || n > BENCH_MAX_BLOCK)
        return;
    for (i = 0; i < n; ++i)
        mix[i] = 0.0f;
    for (v = 0; v < active_voices; ++v)
        pm_string_render(&voices[v], mix, n);
    gain = ((float)master_q12 * (1.0f / 4096.0f)) * 8388607.0f;
    for (i = 0; i < n; ++i) {
        float sample = mix[i] * gain;
        if (sample > 8388607.0f)
            sample = 8388607.0f;
        else if (sample < -8388608.0f)
            sample = -8388608.0f;
        lr[2u * i] = (int32_t)sample;
        lr[2u * i + 1u] = (int32_t)sample;
    }
}

void app_panic(void)
{
    uint32_t i;
    for (i = 0; i < BENCH_MAX_VOICES; ++i)
        pm_string_note_off(&voices[i]);
}

void app_ui_init(void)
{
    plat_glow(1);
    plat_leds(0u, 1u);
    draw_status();
}

void app_frame(void)
{
    static uint32_t last_stats_ms;
    const uint32_t now = plat_ms();
    const int32_t detents = plat_enc(EN_K1);
    if (detents) {
        int32_t index = active_voices == 1u ? 0 :
                        active_voices == 2u ? 1 :
                        active_voices == 4u ? 2 : 3;
        index = clamp(index + detents, 0, 3);
        bench_set_voices((uint8_t)(1u << index));
        draw_status();
    }
    if ((uint32_t)(now - last_stats_ms) >= 250u) {
        last_stats_ms = now;
        draw_status();
    }
}

void app_input_only(void)
{
}

void app_say(const char *a, const char *b)
{
    (void)a;
    (void)b;
}
