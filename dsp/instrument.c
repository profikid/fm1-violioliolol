/* SPDX-License-Identifier: MIT */
#include "instrument.h"
#include "pm_math.h"

#define V(pp, in, da, bo, de, st, re, to, pup, pub, dr, sty, bu, bp, bs, bpo, vi, vr, at, le) \
    {pp, in, da, bo, de, st, re, to, pup, pub, dr, sty, bu, bp, bs, bpo, vi, vr, at, le}

#define STYLE_FINGER 0
#define STYLE_PICK 40
#define STYLE_SLAP 80
#define STYLE_HAMMER 110

#define PAGE_BASS {{PM_P_STYLE, PM_P_PLUCK_POS, PM_P_DAMPING, PM_P_PICKUP_BLEND}, \
                   {PM_P_DECAY, PM_P_INTENSITY, PM_P_TONE, PM_P_RELEASE}}
#define PAGE_UPRIGHT {{PM_P_STYLE, PM_P_PLUCK_POS, PM_P_DAMPING, PM_P_BODY}, \
                      {PM_P_DECAY, PM_P_INTENSITY, PM_P_BUZZ, PM_P_RELEASE}}
#define PAGE_ACOUSTIC {{PM_P_PLUCK_POS, PM_P_INTENSITY, PM_P_DAMPING, PM_P_BODY}, \
                       {PM_P_DECAY, PM_P_STIFFNESS, PM_P_TONE, PM_P_RELEASE}}
#define PAGE_ELECTRIC {{PM_P_PICKUP_POS, PM_P_INTENSITY, PM_P_DAMPING, PM_P_DRIVE}, \
                       {PM_P_DECAY, PM_P_PLUCK_POS, PM_P_TONE, PM_P_PICKUP_BLEND}}
#define PAGE_BOWED {{PM_P_BOW_PRESSURE, PM_P_BOW_SPEED, PM_P_VIBRATO, PM_P_BODY}, \
                    {PM_P_BOW_POS, PM_P_VIB_RATE, PM_P_ATTACK, PM_P_RELEASE}}
#define PAGE_FOLK {{PM_P_PLUCK_POS, PM_P_INTENSITY, PM_P_DAMPING, PM_P_BODY}, \
                   {PM_P_DECAY, PM_P_STIFFNESS, PM_P_BUZZ, PM_P_RELEASE}}

/* name, family, engine, body, unison, pages, CC1, pressure, bend, key octave,
 * open string, string interval, course detune, 0, values */
static const pm_patch_t FACTORY[] = {
    /* ---- bass: the open E string is E1 (28), strings in fourths ---- */
    {"FINGER BASS", PM_FAMILY_BASS, PM_ENGINE_PLUCK, PM_BODY_NONE, 1, PAGE_BASS,
     PM_P_DAMPING, PM_P_VIBRATO, 2, -2, 28, 5, 0, 0,
     V(50, 55, 45, 0, 80, 25, 35, 70, 40, 90, 0, STYLE_FINGER, 0, 64, 64, 40, 0, 60, 20, 100)},
    {"PICK BASS", PM_FAMILY_BASS, PM_ENGINE_PLUCK, PM_BODY_NONE, 1, PAGE_BASS,
     PM_P_DAMPING, PM_P_VIBRATO, 2, -2, 28, 5, 0, 0,
     V(30, 90, 35, 0, 78, 30, 35, 92, 30, 95, 8, STYLE_PICK, 0, 64, 64, 40, 0, 60, 20, 100)},
    {"SLAP BASS", PM_FAMILY_BASS, PM_ENGINE_PLUCK, PM_BODY_NONE, 1, PAGE_BASS,
     PM_P_DAMPING, PM_P_VIBRATO, 2, -2, 28, 5, 0, 0,
     V(20, 110, 25, 0, 75, 30, 30, 112, 25, 100, 0, STYLE_SLAP, 30, 64, 64, 40, 0, 60, 20, 96)},
    {"FRETLESS", PM_FAMILY_BASS, PM_ENGINE_PLUCK, PM_BODY_NONE, 1, PAGE_BASS,
     PM_P_VIBRATO, PM_P_VIBRATO, 2, -2, 28, 5, 0, 0,
     V(60, 45, 50, 0, 90, 20, 45, 62, 55, 85, 0, STYLE_FINGER, 45, 64, 64, 40, 0, 50, 20, 100)},
    {"UPRIGHT", PM_FAMILY_BASS, PM_ENGINE_PLUCK, PM_BODY_UPRIGHT, 1, PAGE_UPRIGHT,
     PM_P_DAMPING, PM_P_VIBRATO, 2, -2, 28, 5, 0, 0,
     V(70, 50, 60, 90, 60, 35, 30, 127, 0, 0, 0, STYLE_FINGER, 20, 64, 64, 40, 0, 50, 20, 104)},
    /* ---- guitar: the open low E is E2 (40) ---- */
    {"NYLON", PM_FAMILY_GUITAR, PM_ENGINE_PLUCK, PM_BODY_NYLON, 1, PAGE_ACOUSTIC,
     PM_P_PLUCK_POS, PM_P_VIBRATO, 2, -1, 40, 5, 0, 0,
     V(45, 45, 40, 95, 70, 10, 40, 127, 0, 0, 0, STYLE_FINGER, 0, 64, 64, 40, 0, 60, 20, 104)},
    {"STEEL", PM_FAMILY_GUITAR, PM_ENGINE_PLUCK, PM_BODY_STEEL, 1, PAGE_ACOUSTIC,
     PM_P_PLUCK_POS, PM_P_VIBRATO, 2, -1, 40, 5, 0, 0,
     V(30, 85, 25, 90, 85, 25, 45, 127, 0, 0, 0, STYLE_PICK, 0, 64, 64, 40, 0, 60, 20, 100)},
    {"CLEAN ELEC", PM_FAMILY_GUITAR, PM_ENGINE_PLUCK, PM_BODY_NONE, 1, PAGE_ELECTRIC,
     PM_P_PICKUP_BLEND, PM_P_VIBRATO, 2, -1, 40, 5, 0, 0,
     V(35, 80, 30, 0, 88, 20, 40, 95, 45, 100, 0, STYLE_PICK, 0, 64, 64, 40, 0, 60, 20, 100)},
    {"DRIVE ELEC", PM_FAMILY_GUITAR, PM_ENGINE_PLUCK, PM_BODY_NONE, 1, PAGE_ELECTRIC,
     PM_P_PICKUP_BLEND, PM_P_VIBRATO, 2, -1, 40, 5, 0, 0,
     V(25, 100, 25, 0, 95, 20, 35, 80, 25, 100, 95, STYLE_PICK, 0, 64, 64, 40, 0, 60, 20, 92)},
    {"JAZZ", PM_FAMILY_GUITAR, PM_ENGINE_PLUCK, PM_BODY_ARCHTOP, 1, PAGE_ELECTRIC,
     PM_P_PICKUP_BLEND, PM_P_VIBRATO, 2, -1, 40, 5, 0, 0,
     V(55, 48, 55, 45, 75, 15, 40, 55, 100, 90, 0, STYLE_PICK, 0, 64, 64, 40, 0, 60, 20, 104)},
    /* ---- bowed: strings in fifths ---- */
    {"VIOLIN", PM_FAMILY_BOWED, PM_ENGINE_BOW, PM_BODY_VIOLIN, 1, PAGE_BOWED,
     PM_P_VIBRATO, PM_P_BOW_PRESSURE, 2, 0, 55, 7, 0, 0,
     V(40, 64, 35, 100, 64, 0, 35, 127, 0, 0, 0, STYLE_FINGER, 0, 60, 80, 35, 45, 70, 45, 100)},
    {"VIOLA", PM_FAMILY_BOWED, PM_ENGINE_BOW, PM_BODY_VIOLA, 1, PAGE_BOWED,
     PM_P_VIBRATO, PM_P_BOW_PRESSURE, 2, 0, 48, 7, 0, 0,
     V(40, 64, 42, 100, 64, 0, 38, 127, 0, 0, 0, STYLE_FINGER, 0, 62, 78, 36, 40, 60, 50, 100)},
    {"CELLO", PM_FAMILY_BOWED, PM_ENGINE_BOW, PM_BODY_CELLO, 1, PAGE_BOWED,
     PM_P_VIBRATO, PM_P_BOW_PRESSURE, 2, -1, 36, 7, 0, 0,
     V(40, 64, 50, 100, 64, 0, 42, 127, 0, 0, 0, STYLE_FINGER, 0, 66, 76, 38, 38, 52, 55, 100)},
    {"CONTRABASS", PM_FAMILY_BOWED, PM_ENGINE_BOW, PM_BODY_CONTRABASS, 1, PAGE_BOWED,
     PM_P_VIBRATO, PM_P_BOW_PRESSURE, 2, -2, 28, 5, 0, 0,
     V(40, 64, 60, 100, 64, 0, 48, 127, 0, 0, 0, STYLE_FINGER, 0, 72, 74, 42, 20, 45, 62, 100)},
    /* ---- other plucked and folk ---- */
    {"HARP", PM_FAMILY_FOLK, PM_ENGINE_PLUCK, PM_BODY_HARP, 1, PAGE_FOLK,
     PM_P_DAMPING, PM_P_VIBRATO, 2, 0, 0, 0, 0, 0,
     V(64, 40, 20, 80, 105, 5, 90, 127, 0, 0, 0, STYLE_FINGER, 0, 64, 64, 40, 0, 60, 20, 104)},
    {"MANDOLIN", PM_FAMILY_FOLK, PM_ENGINE_PLUCK, PM_BODY_MANDOLIN, 2, PAGE_FOLK,
     PM_P_DAMPING, PM_P_VIBRATO, 2, 0, 55, 7, 7, 0,
     V(30, 90, 30, 95, 55, 30, 40, 127, 0, 0, 0, STYLE_PICK, 0, 64, 64, 40, 0, 60, 20, 96)},
    {"BANJO", PM_FAMILY_FOLK, PM_ENGINE_PLUCK, PM_BODY_BANJO, 1, PAGE_FOLK,
     PM_P_DAMPING, PM_P_VIBRATO, 2, 0, 50, 5, 0, 0,
     V(15, 105, 35, 115, 40, 35, 30, 127, 0, 0, 0, STYLE_PICK, 10, 64, 64, 40, 0, 60, 20, 100)},
    {"KOTO", PM_FAMILY_FOLK, PM_ENGINE_PLUCK, PM_BODY_KOTO, 1, PAGE_FOLK,
     PM_P_DAMPING, PM_P_VIBRATO, 2, 0, 0, 0, 0, 0,
     V(12, 95, 30, 85, 70, 45, 50, 127, 0, 0, 0, STYLE_PICK, 5, 64, 64, 40, 0, 55, 20, 100)},
    {"ZITHER", PM_FAMILY_FOLK, PM_ENGINE_PLUCK, PM_BODY_ZITHER, 1, PAGE_FOLK,
     PM_P_DAMPING, PM_P_VIBRATO, 2, 0, 0, 0, 0, 0,
     V(25, 70, 15, 75, 110, 40, 100, 127, 0, 0, 0, STYLE_HAMMER, 0, 64, 64, 40, 0, 60, 20, 100)},
};
#define FACTORY_COUNT ((uint32_t)(sizeof FACTORY / sizeof FACTORY[0]))

static const char *const FAMILY_NAME[PM_FAMILY_COUNT] = {"BASS", "GUITAR", "BOWED", "FOLK"};
static const char *const PARAM_NAME[PM_PARAM_COUNT] = {
    "PLUCK POS", "INTENSITY", "DAMPING", "BODY", "DECAY", "STIFFNESS", "RELEASE", "TONE",
    "PICKUP POS", "PU BLEND", "DRIVE", "STYLE", "BUZZ", "BOW PRESS", "BOW SPEED", "BOW POS",
    "VIBRATO", "VIB RATE", "ATTACK", "LEVEL"
};
static const char *const STYLE_NAME[4] = {"FINGER", "PICK", "SLAP", "HAMMER"};

/* The bodies' modes: frequency, Q, gain. From the usual signature modes of
 * each instrument (air, top plate, back, the bridge hill); starting points
 * to be tuned by ear on the device. */
static const pm_body_mode_t BODY_NYLON[] = {
    {98.0f, 22.0f, 1.0f}, {196.0f, 18.0f, -0.7f}, {285.0f, 14.0f, 0.5f}, {420.0f, 12.0f, 0.35f}, {620.0f, 10.0f, 0.2f}};
static const pm_body_mode_t BODY_STEEL[] = {
    {102.0f, 25.0f, 1.0f}, {204.0f, 20.0f, -0.8f}, {380.0f, 15.0f, 0.5f}, {520.0f, 12.0f, 0.4f},
    {1100.0f, 8.0f, 0.25f}, {2400.0f, 5.0f, 0.15f}};
static const pm_body_mode_t BODY_ARCHTOP[] = {
    {120.0f, 15.0f, 0.8f}, {230.0f, 12.0f, -0.5f}, {450.0f, 8.0f, 0.3f}};
static const pm_body_mode_t BODY_UPRIGHT[] = {
    {62.0f, 14.0f, 1.0f}, {104.0f, 16.0f, -0.8f}, {160.0f, 12.0f, 0.6f}, {245.0f, 10.0f, 0.4f}, {420.0f, 8.0f, 0.2f}};
static const pm_body_mode_t BODY_VIOLIN[] = {
    {275.0f, 18.0f, 1.0f}, {460.0f, 22.0f, -0.9f}, {550.0f, 20.0f, 0.8f}, {700.0f, 10.0f, 0.4f},
    {1050.0f, 8.0f, 0.4f}, {2600.0f, 3.0f, 0.6f}};
static const pm_body_mode_t BODY_VIOLA[] = {
    {220.0f, 18.0f, 1.0f}, {370.0f, 20.0f, -0.9f}, {450.0f, 18.0f, 0.7f}, {600.0f, 10.0f, 0.4f},
    {900.0f, 8.0f, 0.4f}, {2200.0f, 3.0f, 0.5f}};
static const pm_body_mode_t BODY_CELLO[] = {
    {98.0f, 16.0f, 1.0f}, {175.0f, 20.0f, -0.9f}, {222.0f, 18.0f, 0.7f}, {320.0f, 10.0f, 0.4f},
    {500.0f, 8.0f, 0.4f}, {1500.0f, 3.0f, 0.5f}};
static const pm_body_mode_t BODY_CONTRABASS[] = {
    {60.0f, 14.0f, 1.0f}, {100.0f, 18.0f, -0.9f}, {150.0f, 14.0f, 0.6f}, {220.0f, 10.0f, 0.4f},
    {330.0f, 8.0f, 0.3f}, {900.0f, 3.0f, 0.4f}};
static const pm_body_mode_t BODY_HARP[] = {
    {130.0f, 20.0f, 0.8f}, {260.0f, 16.0f, -0.5f}, {410.0f, 12.0f, 0.3f}};
static const pm_body_mode_t BODY_MANDOLIN[] = {
    {290.0f, 16.0f, 1.0f}, {480.0f, 14.0f, -0.7f}, {760.0f, 10.0f, 0.5f}, {1250.0f, 8.0f, 0.35f}, {2600.0f, 5.0f, 0.2f}};
static const pm_body_mode_t BODY_BANJO[] = {
    {340.0f, 7.0f, 1.0f}, {620.0f, 6.0f, -0.8f}, {980.0f, 6.0f, 0.7f}, {1500.0f, 5.0f, 0.5f},
    {2300.0f, 4.0f, 0.4f}, {3400.0f, 4.0f, 0.25f}};
static const pm_body_mode_t BODY_KOTO[] = {
    {190.0f, 14.0f, 0.9f}, {390.0f, 12.0f, -0.6f}, {720.0f, 10.0f, 0.45f}, {1300.0f, 7.0f, 0.3f}};
static const pm_body_mode_t BODY_ZITHER[] = {
    {170.0f, 25.0f, 0.8f}, {340.0f, 20.0f, -0.5f}, {680.0f, 14.0f, 0.35f}, {1400.0f, 8.0f, 0.2f}};

#define BODY(table) {table, (uint32_t)(sizeof table / sizeof table[0])}
static const struct { const pm_body_mode_t *modes; uint32_t count; } BODIES[PM_BODY_COUNT] = {
    {0, 0u}, BODY(BODY_NYLON), BODY(BODY_STEEL), BODY(BODY_ARCHTOP), BODY(BODY_UPRIGHT),
    BODY(BODY_VIOLIN), BODY(BODY_VIOLA), BODY(BODY_CELLO), BODY(BODY_CONTRABASS),
    BODY(BODY_HARP), BODY(BODY_MANDOLIN), BODY(BODY_BANJO), BODY(BODY_KOTO), BODY(BODY_ZITHER)
};

uint32_t pm_factory_count(void)
{
    return FACTORY_COUNT;
}

const pm_patch_t *pm_factory_patch(uint32_t index)
{
    return index < FACTORY_COUNT ? &FACTORY[index] : 0;
}

uint32_t pm_family_first(uint32_t family)
{
    uint32_t i;
    for (i = 0; i < FACTORY_COUNT; ++i)
        if (FACTORY[i].family == family)
            return i;
    return 0u;
}

uint32_t pm_family_count(uint32_t family)
{
    uint32_t i, count = 0u;
    for (i = 0; i < FACTORY_COUNT; ++i)
        if (FACTORY[i].family == family)
            ++count;
    return count;
}

const char *pm_family_name(uint32_t family)
{
    return family < PM_FAMILY_COUNT ? FAMILY_NAME[family] : "";
}

const char *pm_param_name(uint32_t param)
{
    return param < PM_PARAM_COUNT ? PARAM_NAME[param] : "";
}

uint32_t pm_style_of(uint32_t value)
{
    static const uint8_t STYLE[4] = {PM_EXCITE_FINGER, PM_EXCITE_PICK, PM_EXCITE_SLAP, PM_EXCITE_HAMMER};
    return STYLE[(value > 127u ? 127u : value) >> 5];
}

const char *pm_style_name(uint32_t value)
{
    return STYLE_NAME[(value > 127u ? 127u : value) >> 5];
}

static uint32_t fix(uint8_t *field, uint32_t lo, uint32_t hi, uint32_t fallback)
{
    if (*field >= lo && *field <= hi)
        return 0u;
    *field = (uint8_t)fallback;
    return 1u;
}

uint32_t pm_patch_sanitize(pm_patch_t *patch)
{
    uint32_t changed = 0u, i, page, ended = 0u;
    if (!patch)
        return 0u;
    for (i = 0; i < PM_PATCH_NAME; ++i) {
        char c = patch->name[i];
        if (ended || i == PM_PATCH_NAME - 1u) {
            if (c) {
                patch->name[i] = 0;
                ++changed;
            }
        } else if (!c) {
            ended = 1u;
        } else if (c < 32 || c > 126) {
            patch->name[i] = '?';
            ++changed;
        }
    }
    changed += fix(&patch->family, 0u, PM_FAMILY_COUNT - 1u, PM_FAMILY_GUITAR);
    changed += fix(&patch->engine, 0u, PM_ENGINE_COUNT - 1u, PM_ENGINE_PLUCK);
    changed += fix(&patch->body, 0u, PM_BODY_COUNT - 1u, PM_BODY_NONE);
    changed += fix(&patch->unison, 1u, 2u, 1u);
    for (page = 0; page < PM_PATCH_PAGES; ++page)
        for (i = 0; i < 4u; ++i)
            changed += fix(&patch->page[page][i], 0u, PM_PARAM_COUNT - 1u, PM_P_LEVEL);
    if (patch->mod_dest != PM_DEST_NONE)
        changed += fix(&patch->mod_dest, 0u, PM_PARAM_COUNT - 1u, PM_DEST_NONE);
    if (patch->pressure_dest != PM_DEST_NONE)
        changed += fix(&patch->pressure_dest, 0u, PM_PARAM_COUNT - 1u, PM_DEST_NONE);
    changed += fix(&patch->bend_range, 1u, 24u, 2u);
    if (patch->key_octave < -3 || patch->key_octave > 3) {
        patch->key_octave = 0;
        ++changed;
    }
    changed += fix(&patch->open_note, 0u, 127u, 0u);
    changed += fix(&patch->string_interval, 0u, 12u, 0u);
    changed += fix(&patch->detune_cents, 0u, 50u, 0u);
    changed += fix(&patch->reserved, 0u, 0u, 0u);
    for (i = 0; i < PM_PARAM_COUNT; ++i)
        changed += fix(&patch->value[i], 0u, 127u, 64u);
    return changed;
}

static float unit(const uint8_t *value, uint32_t param)
{
    return (float)(value[param] > 127u ? 127u : value[param]) * (1.0f / 127.0f);
}

/* A player plucks, and a pickup sits, at a fixed place on the instrument; a
 * fretted note shortens the string under it, so the same place is a larger
 * fraction of what sounds. */
static float fretted(const pm_patch_t *patch, int32_t note)
{
    int32_t frets;
    if (!patch->open_note || !patch->string_interval || note <= (int32_t)patch->open_note)
        return 1.0f;
    frets = (note - (int32_t)patch->open_note) % (int32_t)patch->string_interval;
    return pm_exp2f((float)frets * (1.0f / 12.0f));
}

void pm_patch_string_params(const pm_patch_t *patch, const uint8_t *value, int32_t note,
                            pm_string_params_t *out)
{
    float damping, track, shorter;
    if (!patch || !value || !out)
        return;
    damping = unit(value, PM_P_DAMPING);
    shorter = fretted(patch, note);
    /* high strings ring shorter than low ones, as real ones do */
    track = pm_clampf(pm_exp2f((52.0f - (float)note) * (1.0f / 36.0f)), 0.25f, 2.0f);
    out->decay_t60 = 0.4f * pm_exp2f(unit(value, PM_P_DECAY) * 5.0f) * track * (1.0f - 0.6f * damping);
    out->release_t60 = 0.04f * pm_exp2f(unit(value, PM_P_RELEASE) * 5.0f);
    out->brightness = pm_clampf(1.0f - damping, 0.0f, 0.97f);
    out->stiffness = unit(value, PM_P_STIFFNESS);
    out->pluck_position = pm_clampf((0.03f + 0.47f * unit(value, PM_P_PLUCK_POS)) * shorter, 0.0f, 0.95f);
    out->hardness = unit(value, PM_P_INTENSITY);
    out->velocity_to_hardness = 0.5f;
    out->pickup_position = pm_clampf((0.04f + 0.4f * unit(value, PM_P_PICKUP_POS)) * shorter, 0.0f, 0.95f);
    out->pickup_blend = unit(value, PM_P_PICKUP_BLEND);
    out->buzz = 0.8f * unit(value, PM_P_BUZZ);
    out->style = (uint8_t)pm_style_of(value[PM_P_STYLE]);
}

void pm_patch_bowed_params(const pm_patch_t *patch, const uint8_t *value,
                           pm_bowed_params_t *out)
{
    if (!patch || !value || !out)
        return;
    out->bow_pressure = unit(value, PM_P_BOW_PRESSURE);
    out->bow_speed = unit(value, PM_P_BOW_SPEED);
    out->bow_position = 0.04f + 0.3f * unit(value, PM_P_BOW_POS);
    out->brightness = 1.0f - unit(value, PM_P_DAMPING);
    out->attack_seconds = 0.004f * pm_exp2f(unit(value, PM_P_ATTACK) * 7.0f);
    out->release_seconds = 0.02f * pm_exp2f(unit(value, PM_P_RELEASE) * 5.0f);
    out->vibrato_depth = pm_patch_vibrato_semitones(value);
    out->vibrato_rate_hz = pm_patch_vibrato_hz(value);
}

float pm_patch_level(const uint8_t *value)
{
    const float level = (float)(value[PM_P_LEVEL] > 127u ? 127u : value[PM_P_LEVEL]) * 0.01f;
    return level * level;
}

float pm_patch_vibrato_semitones(const uint8_t *value)
{
    return 0.5f * unit(value, PM_P_VIBRATO);
}

float pm_patch_vibrato_hz(const uint8_t *value)
{
    return 3.5f + 4.5f * unit(value, PM_P_VIB_RATE);
}

const pm_body_mode_t *pm_body_modes(uint32_t body, uint32_t *count)
{
    if (body >= PM_BODY_COUNT)
        body = PM_BODY_NONE;
    if (count)
        *count = BODIES[body].count;
    return BODIES[body].modes;
}

uint32_t pm_crc32(const void *data, uint32_t bytes)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu, i, bit;
    for (i = 0; i < bytes; ++i) {
        crc ^= p[i];
        for (bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

#define BANK_BODY_BYTES ((uint32_t)(sizeof(pm_bank_t) - sizeof(uint32_t)))

void pm_bank_seal(pm_bank_t *bank)
{
    if (!bank)
        return;
    bank->magic = PM_BANK_MAGIC;
    bank->version = PM_BANK_VERSION;
    bank->slots = PM_BANK_SLOTS;
    bank->crc = pm_crc32(bank, BANK_BODY_BYTES);
}

void pm_bank_init(pm_bank_t *bank)
{
    uint32_t i;
    uint8_t *bytes = (uint8_t *)bank;
    if (!bank)
        return;
    for (i = 0; i < (uint32_t)sizeof *bank; ++i)
        bytes[i] = 0u;
    pm_bank_seal(bank);
}

int pm_bank_open(pm_bank_t *bank, uint32_t bytes)
{
    uint32_t i;
    if (!bank || bytes != (uint32_t)sizeof *bank || bank->magic != PM_BANK_MAGIC ||
        bank->version != PM_BANK_VERSION || bank->slots != PM_BANK_SLOTS ||
        bank->crc != pm_crc32(bank, BANK_BODY_BYTES))
        return -1;
    for (i = 0; i < PM_BANK_SLOTS; ++i) {
        bank->used[i] = (uint8_t)(bank->used[i] != 0u);
        if (bank->used[i])
            (void)pm_patch_sanitize(&bank->slot[i]);
    }
    return 0;
}
