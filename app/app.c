/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: the instrument.
 *
 * The keys play the selected string instrument; ALGORITHM picks the family,
 * PRESET the instrument, K1..K4 its four parameters. EDIT shows four more,
 * GLOBAL the performance page (polyphony, bend range, key curve, and the
 * benchmark voices with CPU load and xruns). SAVE keeps a preset.
 *
 * Two contexts, kept apart (PRD 10, UI-006):
 *   app_render   the audio interrupt. It reads MIDI and owns the engine: every
 *                voice, the body, the output stage.
 *   app_frame    the main loop. It reads the panel, draws, and saves to flash.
 *                To the engine it only posts messages (pm_engine_post), which
 *                the render takes at the start of its next block.
 *
 * Part of fm1-base's unity build (device/unity.c, host/plat_host.c), after the
 * platform: gfx.c, announce.c and lib/touch.c are in scope. The DSP (dsp/) is
 * built apart, at -O2, and knows nothing of the platform.
 */
#include "app.h"
#include "engine.h"

/* ------------------------------------------------------------- engine --- */

static pm_engine_t engine PLAT_POOL;
static float mix[APP_BLOCK];
static float gain_now;                       /* glides to MASTER: no step at a block's edge */
static volatile uint8_t muted;               /* app_panic: silence until the main loop runs again */
static volatile uint32_t midi_count;         /* channel messages in, for the screen's light */
static uint32_t blocks_since_shed;

static void midi_in(void)                    /* audio interrupt: the packets that came in */
{
    uint32_t pkt, guard;
    int port;
    for (guard = 0; guard < 64u && plat_midi_in(&pkt, &port); ++guard) {
        const uint32_t status = (pkt >> 8) & 0xFFu;
        if (status < 0x80u || status >= 0xF0u)
            continue;                        /* clock, active sensing, SysEx pieces: not ours */
        pm_engine_midi(&engine, status, (pkt >> 16) & 0x7Fu, (pkt >> 24) & 0x7Fu);
        ++midi_count;
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float gain;
    if (!lr || n > APP_BLOCK)
        return;
    midi_in();
    pm_engine_render(&engine, mix, n);
    if (blocks_since_shed < 0xFFFFu)
        ++blocks_since_shed;
    /* MASTER, read once a block. The engine's limiter keeps mix inside
     * +-0.85, so full MASTER is still inside 24 bits; the clamp below is the
     * last line, not the plan. */
    gain = muted ? 0.0f : (float)master_q12 * (1.0f / 4096.0f) * 8388607.0f;
    for (i = 0; i < n; ++i) {
        float sample;
        gain_now += (gain - gain_now) * 0.004f;
        sample = mix[i] * gain_now;
        if (sample > 8388607.0f)
            sample = 8388607.0f;
        else if (sample < -8388608.0f)
            sample = -8388608.0f;
        lr[2u * i] = (int32_t)sample;
        lr[2u * i + 1u] = (int32_t)sample;
    }
}

void app_overload(void)                      /* audio interrupt: the last render took over 90 % of its block */
{
    if (blocks_since_shed < 40u)             /* let the last one take effect first: a quarter second */
        return;
    blocks_since_shed = 0u;
    pm_engine_shed(&engine);
}

void app_panic(void)
{
    muted = 1u;
    (void)pm_engine_post(&engine, PM_EV_ALL_OFF, 0u, 0u);
}

/* ----------------------------------------------------- saved settings --- */

#define SET_MAGIC 0x31534C56u                /* "VLS1"; a record with another magic loads the defaults */
#define USER_FLAG 0x80u                      /* an instrument number with it: a user slot */
#define DEFAULT_INSTRUMENT 5u                /* NYLON */
typedef struct {
    uint32_t magic;
    uint8_t instrument;                      /* a factory index, or USER_FLAG | slot */
    int8_t octave;                           /* -3 .. +3, on top of the instrument's own */
    uint8_t polyphony;                       /* 1 .. PM_MAX_VOICES */
    int8_t curve;                            /* the keys' velocity curve, -5 .. +5 */
    uint8_t reserved[4];
} settings_t;
static settings_t set;
static pm_bank_t bank;                       /* the player's presets, as kept in flash */

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.instrument = DEFAULT_INSTRUMENT;
    set.octave = 0;
    set.polyphony = PM_DEFAULT_POLYPHONY;
    set.curve = 0;
    set.reserved[0] = set.reserved[1] = set.reserved[2] = set.reserved[3] = 0u;
}

/* --------------------------------------------------------------------- UI --- */

enum { PAGE_MAIN, PAGE_MORE, PAGE_PERF, PAGE_COUNT };
#define FAMILY_USER PM_FAMILY_COUNT          /* ALGORITHM's fifth position: the player's presets */
static const uint8_t BENCH_STEPS[6] = {0u, 1u, 2u, 4u, 8u, 12u};

static struct {
    pm_patch_t patch;                        /* the instrument as the player has it: what SAVE keeps */
    uint8_t user;                            /* it came from a user slot */
    uint8_t index;                           /* its factory index, or its slot */
    uint8_t edited;                          /* changed since it was loaded or saved */
    uint8_t page;
    uint8_t saving;                          /* SAVE pressed once: PRESET picks the slot, SAVE again keeps it */
    uint8_t save_slot;
    uint8_t save_what;                       /* 0 nothing waits; 1 the settings; 2 the bank and the settings */
    uint8_t sustain;                         /* PLAY: the pedal */
    uint8_t bench;                           /* index into BENCH_STEPS */
    uint8_t pressure;
    uint8_t redraw;
    uint8_t status_voices;
    uint8_t key_note[NKEYS];                 /* the note each held key started: its note-off's */
    uint32_t keys, btn;
    uint32_t save_at;                        /* when the waiting save is written at the latest */
    uint32_t msg_until;
    uint32_t midi_seen, midi_lit_until;
    uint32_t status_at;
    char msg[2][24];
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void slot_text(uint32_t slot, char *b)        /* U01 .. U16 */
{
    b[0] = 'U';
    b[1] = (char)('0' + (slot + 1u) / 10u);
    b[2] = (char)('0' + (slot + 1u) % 10u);
    b[3] = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

/* ---- the four rows: the page's parameters, or the performance settings ---- */

static const char *const PERF_NAME[4] = {"POLYPHONY", "BEND RANGE", "KEY CURVE", "BENCH"};

static const char *row_name(uint32_t row)
{
    if (ui.page == PAGE_PERF)
        return PERF_NAME[row];
    return pm_param_name(ui.patch.page[ui.page][row]);
}

static void row_value(uint32_t row, char *text, uint32_t *bar)   /* its text, and 0..127 for the bar */
{
    text[0] = 0;
    if (ui.page == PAGE_PERF) {
        if (row == 0u) {
            int_text(set.polyphony, text);
            *bar = set.polyphony * 127u / PM_MAX_VOICES;
        } else if (row == 1u) {
            int_text(ui.patch.bend_range, text);
            *bar = ui.patch.bend_range * 127u / 24u;
        } else if (row == 2u) {
            if (set.curve > 0)
                text_cat(text, "+");
            int_text(set.curve, text + (set.curve > 0));
            *bar = (uint32_t)(set.curve + 5) * 127u / 10u;
        } else {
            if (BENCH_STEPS[ui.bench])
                int_text(BENCH_STEPS[ui.bench], text);
            else
                text_cat(text, "OFF");
            *bar = BENCH_STEPS[ui.bench] * 127u / PM_MAX_VOICES;
        }
        return;
    }
    {
        const uint32_t param = ui.patch.page[ui.page][row], value = ui.patch.value[param];
        if (param == PM_P_STYLE)
            text_cat(text, pm_style_name(value));
        else
            int_text((int32_t)value, text);
        *bar = value;
    }
}

static void say_row(uint32_t row)
{
    char text[12];
    uint32_t bar;
    row_value(row, text, &bar);
    app_say(row_name(row), text);
}

/* ---- instruments ---- */

static uint32_t user_count(void)
{
    uint32_t i, n = 0u;
    for (i = 0; i < PM_BANK_SLOTS; ++i)
        n += bank.used[i] != 0u;
    return n;
}

static void instrument_title(char *t)        /* "VIOLIN", or "U03 VIOLIN" */
{
    t[0] = 0;
    if (ui.user) {
        slot_text(ui.index, t);
        text_cat(t, " ");
    }
    text_cat(t, ui.patch.name);
}

/* Make an instrument the playing one. The render takes the patch at its next block. */
static void instrument_load(uint32_t user, uint32_t index, int announce)
{
    char title[24];
    if (user) {
        if (index >= PM_BANK_SLOTS || !bank.used[index])
            return;
        ui.patch = bank.slot[index];
    } else {
        if (index >= pm_factory_count())
            return;
        ui.patch = *pm_factory_patch(index);
    }
    (void)pm_patch_sanitize(&ui.patch);
    ui.user = (uint8_t)user;
    ui.index = (uint8_t)index;
    ui.edited = 0u;
    ui.saving = 0u;
    if (ui.page == PAGE_MORE)
        ui.page = PAGE_MAIN;
    set.instrument = (uint8_t)(user ? USER_FLAG | index : index);
    (void)pm_engine_post_patch(&engine, &ui.patch);
    ui.redraw = 1;
    if (announce) {
        instrument_title(title);
        app_say(ui.user ? "USER" : pm_family_name(ui.patch.family), title);
    }
}

static uint32_t family_now(void)
{
    return ui.user ? FAMILY_USER : ui.patch.family;
}

static int32_t user_next(int32_t from, int32_t direction)   /* the next used slot that way, or -1 */
{
    int32_t slot;
    for (slot = from + direction; slot >= 0 && slot < (int32_t)PM_BANK_SLOTS; slot += direction)
        if (bank.used[slot])
            return slot;
    return -1;
}

static void family_step(int32_t detents)     /* ALGORITHM */
{
    int32_t family = clamp((int32_t)family_now() + detents, 0, (int32_t)FAMILY_USER);
    if (family == (int32_t)FAMILY_USER && user_next(-1, 1) < 0) {
        family = (int32_t)FAMILY_USER - 1;   /* nothing saved yet: the user family is not there */
        if (family == (int32_t)family_now()) {
            app_say("USER", "NO PRESETS YET");
            return;
        }
    }
    if (family == (int32_t)family_now())
        return;
    if (family == (int32_t)FAMILY_USER) {
        instrument_load(1u, (uint32_t)user_next(-1, 1), 1);
    } else {
        instrument_load(0u, pm_family_first((uint32_t)family), 1);
    }
}

static void preset_step(int32_t detents)     /* PRESET: within the family */
{
    int32_t step = detents > 0 ? 1 : -1, left = detents > 0 ? detents : -detents;
    if (ui.user) {
        int32_t slot = (int32_t)ui.index, next;
        while (left-- > 0 && (next = user_next(slot, step)) >= 0)
            slot = next;
        if (slot != (int32_t)ui.index)
            instrument_load(1u, (uint32_t)slot, 1);
    } else {
        const int32_t first = (int32_t)pm_family_first(ui.patch.family);
        const int32_t index = clamp((int32_t)ui.index + detents, first,
                                    first + (int32_t)pm_family_count(ui.patch.family) - 1);
        if (index != (int32_t)ui.index)
            instrument_load(0u, (uint32_t)index, 1);
    }
}

static void browse_step(int32_t detents)     /* SELECT: every instrument, factory then user, end to end */
{
    int32_t step = detents > 0 ? 1 : -1, left = detents > 0 ? detents : -detents;
    int32_t user = ui.user, index = (int32_t)ui.index;
    while (left-- > 0) {
        if (!user) {
            if (index + step >= 0 && index + step < (int32_t)pm_factory_count()) {
                index += step;
            } else if (step > 0 && user_next(-1, 1) >= 0) {
                user = 1;
                index = user_next(-1, 1);
            } else {
                break;
            }
        } else {
            const int32_t next = user_next(index, step);
            if (next >= 0) {
                index = next;
            } else if (step < 0) {
                user = 0;
                index = (int32_t)pm_factory_count() - 1;
            } else {
                break;
            }
        }
    }
    if (user != (int32_t)ui.user || index != (int32_t)ui.index)
        instrument_load((uint32_t)user, (uint32_t)index, 1);
}

/* ---- the knobs ---- */

static void param_turn(uint32_t row, int32_t detents)
{
    const uint32_t param = ui.patch.page[ui.page][row];
    int32_t value = ui.patch.value[param];
    if (param == PM_P_STYLE) {               /* four styles: a detent each */
        value = clamp((value >> 5) + detents, 0, 3) * 32 + 8;
    } else {
        /* a slow turn is fine, a fast one covers the range */
        const int32_t speed = detents > 2 || detents < -2 ? 4 : 2;
        value = clamp(value + detents * speed, 0, 127);
    }
    if (value == ui.patch.value[param])
        return;
    ui.patch.value[param] = (uint8_t)value;
    ui.edited = 1u;
    (void)pm_engine_post(&engine, PM_EV_PARAM, param, (uint32_t)value);
    say_row(row);
}

static void perf_turn(uint32_t row, int32_t detents)
{
    if (row == 0u) {
        const int32_t v = clamp(set.polyphony + detents, 1, (int32_t)PM_MAX_VOICES);
        if (v == set.polyphony)
            return;
        set.polyphony = (uint8_t)v;
        (void)pm_engine_post(&engine, PM_EV_POLYPHONY, 0u, (uint32_t)v);
    } else if (row == 1u) {
        const int32_t v = clamp(ui.patch.bend_range + detents, 1, 24);
        if (v == ui.patch.bend_range)
            return;
        ui.patch.bend_range = (uint8_t)v;
        ui.edited = 1u;
        (void)pm_engine_post(&engine, PM_EV_BEND_RANGE, 0u, (uint32_t)v);
    } else if (row == 2u) {
        const int32_t v = clamp(set.curve + detents, -5, 5);
        if (v == set.curve)
            return;
        set.curve = (int8_t)v;
        touch_curve(v);
    } else {
        const int32_t v = clamp(ui.bench + detents, 0, 5);
        if (v == ui.bench)
            return;
        ui.bench = (uint8_t)v;
        (void)pm_engine_post(&engine, PM_EV_BENCH, 0u, BENCH_STEPS[v]);
    }
    say_row(row);
}

/* ---- saving ---- */

static void save_begin(void)
{
    int32_t slot;
    if (ui.page == PAGE_PERF) {              /* the settings: polyphony, key curve, octave, this instrument */
        ui.save_what = 1u;
        ui.save_at = plat_ms() + 60u;
        (void)pm_engine_post(&engine, PM_EV_ALL_OFF, 0u, 0u);
        return;
    }
    ui.saving = 1u;
    if (ui.user) {
        slot = (int32_t)ui.index;
    } else {                                 /* the first free slot, or the first */
        for (slot = 0; slot < (int32_t)PM_BANK_SLOTS && bank.used[slot]; ++slot)
            ;
        if (slot == (int32_t)PM_BANK_SLOTS)
            slot = 0;
    }
    ui.save_slot = (uint8_t)slot;
    ui.redraw = 1;
    {
        char t[8];
        slot_text(ui.save_slot, t);
        app_say("SAVE TO", t);
    }
}

static void save_commit(void)
{
    bank.slot[ui.save_slot] = ui.patch;
    bank.used[ui.save_slot] = 1u;
    pm_bank_seal(&bank);
    ui.user = 1u;
    ui.index = ui.save_slot;
    ui.edited = 0u;
    ui.saving = 0u;
    set.instrument = (uint8_t)(USER_FLAG | ui.save_slot);
    /* A flash write stops the sound for a moment: fade every voice out
     * first, and write once they are silent (or 60 ms on, whichever is first). */
    ui.save_what = 2u;
    ui.save_at = plat_ms() + 60u;
    (void)pm_engine_post(&engine, PM_EV_ALL_OFF, 0u, 0u);
    ui.sustain = 0u;
    ui.redraw = 1;
}

/* The main loop only: the one place the flash is written. */
static void save_write(void)
{
    int failed = 0;
    if (ui.save_what == 2u)
        failed |= plat_store_save(OBJ_BANK, &bank, sizeof bank) != 0;
    failed |= plat_store_save(OBJ_SETTINGS, &set, sizeof set) != 0;
    if (BENCH_STEPS[ui.bench])               /* the benchmark's voices went quiet for the write */
        (void)pm_engine_post(&engine, PM_EV_BENCH, 0u, BENCH_STEPS[ui.bench]);
    if (failed) {
        app_say("SAVE", "FAILED");
    } else if (ui.save_what == 2u) {
        char t[8];
        slot_text(ui.index, t);
        app_say("SAVED", t);
    } else {
        app_say("SETTINGS", "SAVED");
    }
    ui.save_what = 0u;
}

/* ---- input ---- */

static void octave_step(int32_t d)
{
    const int32_t o = clamp(set.octave + d, -3, 3);
    char t[4];
    if (o == set.octave)
        return;
    set.octave = (int8_t)o;
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
}

static void keys_poll(void)
{
    uint32_t keys, k, pressure = 0u;
    touch_poll();                            /* the sensed keys' presses, velocities and pressure */
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        const uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            const int32_t note = clamp(53 + (int32_t)k + 12 * (ui.patch.key_octave + set.octave), 0, 127);
            uint32_t velocity = touch_vel(k);
            if (!velocity)
                velocity = 100u;
            if (velocity > 127u)
                velocity = 127u;
            ui.key_note[k] = (uint8_t)note;
            (void)pm_engine_post(&engine, PM_EV_NOTE_ON, (uint32_t)note, velocity);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | velocity << 24, TO_USB);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            (void)pm_engine_post(&engine, PM_EV_NOTE_OFF, ui.key_note[k], 0u);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)ui.key_note[k] << 16, TO_USB);
        }
        if ((keys & bit) && touch_pressure(k) > pressure)
            pressure = touch_pressure(k);
    }
    if (pressure > 127u)
        pressure = 127u;
    if (pressure != ui.pressure) {           /* the hardest-pressed key: bow pressure, vibrato, as the instrument maps it */
        ui.pressure = (uint8_t)pressure;
        (void)pm_engine_post(&engine, PM_EV_PRESSURE, 0u, pressure);
        plat_midi_send(0x0Du | 0xD0u << 8 | pressure << 16, TO_USB);
    }
    if (keys != ui.keys)
        ui.redraw = 1;
    ui.keys = keys;
}

static void buttons_poll(void)
{
    const uint32_t btn = plat_buttons();
    uint32_t k;
    for (k = 0; k < NB; k++) {
        const uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (ui.saving) {                     /* SAVE again keeps it; anything else leaves it */
            if (k == B_SAVE) {
                save_commit();
            } else {
                ui.saving = 0u;
                app_say("SAVE", "CANCELLED");
            }
            continue;
        }
        switch (k) {
        case B_OCTDN:
            octave_step(-1);
            break;
        case B_OCTUP:
            octave_step(1);
            break;
        case B_EDIT:
            ui.page = ui.page == PAGE_MORE ? PAGE_MAIN : PAGE_MORE;
            app_say("PAGE", ui.page == PAGE_MORE ? "MORE" : "MAIN");
            break;
        case B_GLO:
            ui.page = ui.page == PAGE_PERF ? PAGE_MAIN : PAGE_PERF;
            app_say("PAGE", ui.page == PAGE_PERF ? "PERF" : "MAIN");
            break;
        case B_HOME:                         /* everything quiet, back to the first page */
            (void)pm_engine_post(&engine, PM_EV_ALL_OFF, 0u, 0u);
            if (BENCH_STEPS[ui.bench]) {
                ui.bench = 0u;
                (void)pm_engine_post(&engine, PM_EV_BENCH, 0u, 0u);
            }
            ui.sustain = 0u;
            ui.page = PAGE_MAIN;
            app_say("ALL NOTES", "OFF");
            break;
        case B_PLAY:                         /* the sustain pedal, latched */
            ui.sustain = (uint8_t)!ui.sustain;
            (void)pm_engine_post(&engine, PM_EV_CONTROL, 64u, ui.sustain ? 127u : 0u);
            app_say("SUSTAIN", ui.sustain ? "ON" : "OFF");
            break;
        case B_SAVE:
            save_begin();
            break;
        default:
            break;
        }
        ui.redraw = 1;
    }
    if (btn != ui.btn)
        ui.redraw = 1;
    ui.btn = btn;
}

static void encoders_poll(void)
{
    int32_t e;
    uint32_t row;
    if (ui.saving) {                         /* any of the three browsing knobs picks the slot */
        e = plat_enc(EN_PRESET) + plat_enc(EN_SELECT) + plat_enc(EN_ALGO);
        if (e) {
            char t[8];
            ui.save_slot = (uint8_t)clamp((int32_t)ui.save_slot + e, 0, (int32_t)PM_BANK_SLOTS - 1);
            slot_text(ui.save_slot, t);
            app_say("SAVE TO", t);
        }
    } else {
        if ((e = plat_enc(EN_ALGO)) != 0)
            family_step(e);
        if ((e = plat_enc(EN_PRESET)) != 0)
            preset_step(e);
        if ((e = plat_enc(EN_SELECT)) != 0)
            browse_step(e);
    }
    for (row = 0; row < 4u; ++row) {
        if ((e = plat_enc((int)(EN_K1 + row))) == 0)
            continue;
        if (ui.page == PAGE_PERF)
            perf_turn(row, e);
        else
            param_turn(row, e);
    }
}

/* What MIDI did that the screen shows: the knobs' controllers, a program change. */
static void engine_poll(void)
{
    uint32_t changed, lock, param;
    lock = plat_lock();                      /* (the render sets bits in it) */
    changed = engine.midi_params;
    engine.midi_params = 0u;
    plat_unlock(lock);
    for (param = 0; changed && param < PM_PARAM_COUNT; ++param) {
        if (!(changed >> param & 1u))
            continue;
        ui.patch.value[param] = engine.patch.value[param] & 0x7Fu;   /* one byte: read whole */
        ui.edited = 1u;
        ui.redraw = 1;
    }
    if (engine.program_request) {            /* program N: the Nth instrument, factory then user slots */
        const uint32_t program = engine.program_request - 1u;
        engine.program_request = 0u;
        if (program < pm_factory_count())
            instrument_load(0u, program, 1);
        else if (program - pm_factory_count() < PM_BANK_SLOTS)
            instrument_load(1u, program - pm_factory_count(), 1);
    }
}

static void input(void)
{
    uint32_t lit_buttons;
    muted = 0u;                              /* the main loop runs: no update is rewriting the flash */
    keys_poll();
    buttons_poll();
    encoders_poll();
    engine_poll();
    lit_buttons = ui.btn;
    if (ui.sustain)
        lit_buttons |= 1u << B_PLAY;
    if (ui.edited || ui.saving)
        lit_buttons |= 1u << B_SAVE;
    if (ui.page == PAGE_MORE)
        lit_buttons |= 1u << B_EDIT;
    if (ui.page == PAGE_PERF)
        lit_buttons |= 1u << B_GLO;
    plat_glow(1);                            /* the unlit buttons glow, readable in the dark */
    plat_leds(lit_buttons, ui.keys);
}

void app_input_only(void) { input(); }

/* ---- drawing: three bands, each at most 72 rows (APP_CANVAS_PX) ---- */

#define HEADER_Y 0
#define HEADER_H 46
#define ROWS_Y 50
#define ROW_H 30
#define STATUS_Y 174
#define STATUS_H 66

static void draw_header(void)
{
    char t[24];
    cv_begin(240, HEADER_H, C_BLACK);
    if (plat_ms() < ui.msg_until) {
        cv_text(8, 4, &FONT_S, ui.msg[0], C_GRAY);
        cv_text(8, 22, &FONT_M, ui.msg[1], C_WHITE);
    } else if (ui.saving) {
        slot_text(ui.save_slot, t);
        cv_text(8, 4, &FONT_S, "SAVE TO  (PRESET PICKS, SAVE KEEPS)", C_GRAY);
        cv_text(8, 22, &FONT_M, t, C_WHITE);
        cv_text(64, 26, &FONT_B, bank.used[ui.save_slot] ? bank.slot[ui.save_slot].name : "EMPTY", C_HI);
    } else {
        static const char *const PAGE[PAGE_COUNT] = {"MAIN", "MORE", "PERF"};
        int32_t x = cv_text(8, 4, &FONT_S, ui.user ? "USER" : pm_family_name(ui.patch.family), C_GRAY);
        if (ui.user) {
            slot_text(ui.index, t);
            x = cv_text(x + 8, 4, &FONT_S, t, C_AMB);
        }
        if (ui.edited)
            cv_text(x + 8, 4, &FONT_S, "EDITED", C_AMB);
        cv_text(232 - text_w(&FONT_S, PAGE[ui.page]), 4, &FONT_S, PAGE[ui.page], ui.page ? C_HI : C_DIM);
        cv_text(8, 22, &FONT_M, ui.patch.name, C_WHITE);
    }
    cv_rect(0, HEADER_H - 1, 240, 1, C_LINE);
    cv_blit(0, HEADER_Y);
}

static void draw_rows(uint32_t first)        /* rows first and first + 1: one band */
{
    uint32_t row;
    cv_begin(240, 2 * ROW_H, C_BLACK);
    for (row = first; row < first + 2u; ++row) {
        static const char *const KNOB[4] = {"K1", "K2", "K3", "K4"};
        const int32_t y = (int32_t)(row - first) * ROW_H;
        char text[12];
        uint32_t bar;
        row_value(row, text, &bar);
        cv_text(8, y + 2, &FONT_XS, KNOB[row], C_DIM);
        cv_text(32, y + 1, &FONT_S, row_name(row), C_GRAY);
        cv_text(232 - text_w(&FONT_B, text), y + 1, &FONT_B, text, C_HI);
        cv_rect(32, y + 21, 200, 3, C_LINE);
        cv_rect(32, y + 21, (int32_t)(bar > 127u ? 127u : bar) * 200 / 127, 3, C_HI);
    }
    cv_blit(0, (uint32_t)(ROWS_Y + (int32_t)first * ROW_H));
}

static void draw_status(void)
{
    char t[24], n[12];
    const uint32_t voices = engine.active_voices, polyphony = engine.polyphony_now;
    uint32_t i;
    int32_t x;
    cv_begin(240, STATUS_H, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    for (i = 0; i < PM_MAX_VOICES; ++i)      /* a box a voice: sounding, free, or beyond the polyphony */
        cv_rect(8 + (int32_t)i * 19, 8, 15, 8, i < voices ? C_HI : i < polyphony ? C_DIM : C_LINE);
    t[0] = 0;
    text_cat(t, "CPU ");
    int_text((int32_t)plat_cpu_pct(), n);
    text_cat(t, n);
    text_cat(t, "%");
    x = cv_text(8, 24, &FONT_XS, t, C_GRAY);
    t[0] = 0;
    text_cat(t, "XRUNS ");
    int_text((int32_t)plat_xruns(), n);
    text_cat(t, n);
    x = cv_text(x + 12, 24, &FONT_XS, t, plat_xruns() ? C_WHITE : C_GRAY);
    t[0] = 0;
    text_cat(t, "OCT ");
    if (set.octave >= 0)
        text_cat(t, "+");
    int_text(set.octave, n);
    text_cat(t, n);
    cv_text(232 - text_w(&FONT_XS, t), 24, &FONT_XS, t, C_GRAY);
    x = cv_text(8, 42, &FONT_XS, "SUSTAIN", ui.sustain ? C_HI : C_LINE);
    x = cv_text(x + 12, 42, &FONT_XS, "MIDI", plat_ms() < ui.midi_lit_until ? C_HI : C_LINE);
    x = cv_text(x + 12, 42, &FONT_XS, "LIMIT", engine.limiter_q8 < 250u ? C_WHITE : C_LINE);
    if (engine.sheds) {                      /* the render ran out of time and voices were shed */
        t[0] = 0;
        text_cat(t, "SHED ");
        int_text((int32_t)engine.sheds, n);
        text_cat(t, n);
        cv_text(x + 12, 42, &FONT_XS, t, C_WHITE);
    }
    cv_blit(0, STATUS_Y);
    ui.status_voices = (uint8_t)voices;
    ui.status_at = plat_ms();
}

static void draw(void)
{
    draw_header();
    draw_rows(0u);
    draw_rows(2u);
    draw_status();
}

/* a screen reader that has just started listening (or asks) hears the whole screen */
static void speak(void)
{
    if (ann_on() && ann_where()) {
        char t[160], text[12], title[24];
        uint32_t row, bar;
        instrument_title(title);
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, ui.user ? "USER" : pm_family_name(ui.patch.family));
        text_cat(t, ". ");
        text_cat(t, title);
        for (row = 0; row < 4u; ++row) {
            row_value(row, text, &bar);
            text_cat(t, ". ");
            text_cat(t, row_name(row));
            text_cat(t, " ");
            text_cat(t, text);
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

/* ------------------------------------------------------------- contract --- */

void app_init(void)
{
    uint32_t user, index;
    int loaded;
    pm_engine_init(&engine, 0x51A1u);
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC ||
        set.octave < -3 || set.octave > 3 || set.polyphony < 1u || set.polyphony > PM_MAX_VOICES ||
        set.curve < -5 || set.curve > 5)
        settings_defaults();
    /* the presets: whole and checked, or none (a torn or foreign record is never played) */
    loaded = plat_store_load(OBJ_BANK, &bank, sizeof bank);
    if (loaded < 0 || pm_bank_open(&bank, (uint32_t)loaded) != 0)
        pm_bank_init(&bank);
    user = (set.instrument & USER_FLAG) != 0u;
    index = set.instrument & (USER_FLAG - 1u);
    if (user ? (index >= PM_BANK_SLOTS || !bank.used[index]) : index >= pm_factory_count()) {
        user = 0u;
        index = DEFAULT_INSTRUMENT;
    }
    ui.patch = user ? bank.slot[index] : *pm_factory_patch(index);
    (void)pm_patch_sanitize(&ui.patch);
    ui.user = (uint8_t)user;
    ui.index = (uint8_t)index;
    set.instrument = (uint8_t)(user ? USER_FLAG | index : index);
    /* the audio has not started: the engine is ours to set */
    pm_engine_set_patch(&engine, &ui.patch);
    engine.polyphony = set.polyphony;
    engine.polyphony_now = set.polyphony;
}

void app_ui_init(void)
{
    touch_on(1);                             /* velocity and pressure: the keys' readings on */
    touch_curve(set.curve);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    const uint32_t now = plat_ms();
    input();
    speak();
    if (ui.save_what && (engine.active_voices == 0u || (int32_t)(now - ui.save_at) >= 0))
        save_write();
    if (midi_count != ui.midi_seen) {
        ui.midi_seen = midi_count;
        if (now >= ui.midi_lit_until)
            ui.status_at = 0u;               /* light it now */
        ui.midi_lit_until = now + 120u;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0u;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    } else if (engine.active_voices != ui.status_voices || now - ui.status_at >= 250u) {
        draw_status();                       /* the voices as they come and go; CPU and xruns four times a second */
    }
}

#ifdef PLAT_HOST
/* The host simulator's checks (tests/scenarios): expect WHAT VALUE. */
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "instrument"))         /* factory index, or 100 + user slot */
        *got = ui.user ? 100 + ui.index : ui.index;
    else if (!strcmp(what, "family"))        /* PM_FAMILY_*, 4 = user */
        *got = (int)family_now();
    else if (!strcmp(what, "engine"))        /* the playing patch's model: 0 plucked, 1 bowed */
        *got = engine.patch.engine;
    else if (!strcmp(what, "voices"))
        *got = engine.active_voices;
    else if (!strcmp(what, "polyphony"))
        *got = engine.polyphony_now;
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "curve"))
        *got = set.curve;
    else if (!strcmp(what, "edited"))
        *got = ui.edited;
    else if (!strcmp(what, "saving"))
        *got = ui.saving;
    else if (!strcmp(what, "sustain"))
        *got = engine.sustain;
    else if (!strcmp(what, "bench"))
        *got = BENCH_STEPS[ui.bench];
    else if (!strcmp(what, "users"))         /* user slots in use */
        *got = (int)user_count();
    else if (!strcmp(what, "bend"))          /* the wheel, 0..16383 */
        *got = engine.bend;
    else if (!strcmp(what, "bend_range"))
        *got = engine.patch.bend_range;
    else if (!strcmp(what, "mod"))
        *got = engine.mod;
    else if (!strcmp(what, "pressure"))
        *got = engine.pressure;
    else if (!strcmp(what, "volume"))
        *got = engine.volume;
    else if (!strcmp(what, "expression"))
        *got = engine.expression;
    else if (!strcmp(what, "steals"))
        *got = (int)engine.steals;
    else if (!strcmp(what, "sheds"))
        *got = (int)engine.sheds;
    else if (!strcmp(what, "faults"))
        *got = (int)engine.faults;
    else if (!strcmp(what, "dropped"))
        *got = (int)engine.dropped;
    else if (!strncmp(what, "row", 3) && what[3] >= '0' && what[3] <= '3' && !what[4])   /* rowN: the page's Nth value */
        *got = ui.page == PAGE_PERF ? -1 : ui.patch.value[ui.patch.page[ui.page][what[3] - '0']];
    else if (!strncmp(what, "live", 4) && what[4] >= '0' && what[4] <= '3' && !what[5])  /* liveN: as the engine plays it */
        *got = ui.page == PAGE_PERF ? -1 : engine.value[engine.patch.page[ui.page][what[4] - '0']];
    else
        return 0;
    return 1;
}
#endif
