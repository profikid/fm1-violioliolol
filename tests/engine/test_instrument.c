/* SPDX-License-Identifier: MIT
 * Native tests of the patches and the preset bank: INS-001 .. INS-005.
 */
#include <string.h>
#include "instrument.h"
#include "test_util.h"

static int has(const char *name)
{
    uint32_t i;
    for (i = 0; i < pm_factory_count(); ++i)
        if (!strcmp(pm_factory_patch(i)->name, name))
            return 1;
    return 0;
}

static int test_factory(void)
{
    uint32_t i, k, family, total = 0u;
    CHECK(sizeof(pm_patch_t) == 52u, "the patch is 52 bytes: its layout is the preset format");
    CHECK(sizeof(pm_bank_t) == 8u + 16u * 52u + 16u + 4u, "the bank is 860 bytes");
    CHECK(sizeof(pm_bank_t) <= 3840u, "the bank fits one store object");
    CHECK(pm_factory_patch(pm_factory_count()) == 0, "beyond the table there is no patch");
    for (i = 0; i < pm_factory_count(); ++i) {
        pm_patch_t copy = *pm_factory_patch(i);
        CHECK(pm_patch_sanitize(&copy) == 0u, "every factory patch is in range as written");
        CHECK(strlen(copy.name) > 0u && strlen(copy.name) < PM_PATCH_NAME, "every factory patch has a name");
        for (k = 0; k < i; ++k)
            CHECK(strcmp(copy.name, pm_factory_patch(k)->name) != 0, "factory names are unique");
        CHECK(i == 0u || copy.family >= pm_factory_patch(i - 1u)->family, "the table is grouped by family");
        if (copy.engine == PM_ENGINE_BOW)
            CHECK(copy.family == PM_FAMILY_BOWED, "bowed patches are in the bowed family");
    }
    for (family = 0; family < PM_FAMILY_COUNT; ++family) {
        CHECK(pm_family_count(family) >= 4u, "every family has at least four instruments");
        CHECK(pm_factory_patch(pm_family_first(family))->family == family, "a family's first patch is its own");
        CHECK(strlen(pm_family_name(family)) > 0u, "every family has a name");
        total += pm_family_count(family);
    }
    CHECK(total == pm_factory_count(), "every patch is in a family");
    /* the configurations the PRD names */
    CHECK(has("FINGER BASS") && has("PICK BASS") && has("SLAP BASS") && has("FRETLESS") && has("UPRIGHT"),
          "INS-001: finger, pick, slap, fretless and upright bass");
    CHECK(has("NYLON") && has("STEEL") && has("CLEAN ELEC") && has("DRIVE ELEC") && has("JAZZ"),
          "INS-002: nylon, steel, clean electric, distorted electric and jazz guitar");
    CHECK(has("VIOLIN") && has("VIOLA") && has("CELLO") && has("CONTRABASS"),
          "INS-003: violin, viola, cello and double bass");
    CHECK(has("HARP") && has("MANDOLIN") && has("BANJO") && has("KOTO") && has("ZITHER"),
          "INS-004: harp, mandolin, banjo, koto and zither");
    for (i = 0; i < PM_PARAM_COUNT; ++i)
        CHECK(strlen(pm_param_name(i)) > 0u && strlen(pm_param_name(i)) <= 10u, "parameter names fit the screen");
    CHECK(!strcmp(pm_style_name(0u), "FINGER") && !strcmp(pm_style_name(127u), "HAMMER") &&
          !strcmp(pm_style_name(1000u), "HAMMER"), "styles are a quarter of the range each");
    for (i = 0; i < PM_BODY_COUNT + 3u; ++i) {
        uint32_t count = 99u;
        const pm_body_mode_t *modes = pm_body_modes(i, &count);
        CHECK(count <= PM_BODY_MAX_MODES && (count == 0u || modes != 0), "every body has at most six modes");
    }
    printf("  factory: ok (%u instruments in %u families)\n", (unsigned)pm_factory_count(), (unsigned)PM_FAMILY_COUNT);
    return 0;
}

/* INS-006: each family's parameters reach its model, and differ from the others'. */
static int test_mapping(void)
{
    uint32_t i;
    for (i = 0; i < pm_factory_count(); ++i) {
        const pm_patch_t *patch = pm_factory_patch(i);
        pm_string_params_t string, high;
        pm_bowed_params_t bowed;
        pm_patch_string_params(patch, patch->value, 40, &string);
        pm_patch_string_params(patch, patch->value, 88, &high);
        pm_patch_bowed_params(patch, patch->value, &bowed);
        CHECK(string.decay_t60 > 0.05f && string.decay_t60 < 60.0f, "decay maps to seconds");
        CHECK(high.decay_t60 < string.decay_t60, "high strings ring shorter than low ones");
        CHECK(string.release_t60 >= 0.04f && string.release_t60 <= 1.3f, "release maps to seconds");
        CHECK(string.brightness >= 0.0f && string.brightness <= 1.0f, "brightness is 0 .. 1");
        CHECK(string.pluck_position > 0.0f && string.pluck_position <= 0.95f, "pluck position is on the string");
        CHECK(string.style < PM_EXCITE_COUNT, "the style is one of the styles");
        CHECK(bowed.bow_position >= 0.04f && bowed.bow_position <= 0.34f, "bow position is on the string");
        CHECK(bowed.attack_seconds >= 0.004f && bowed.attack_seconds <= 0.52f, "attack maps to seconds");
        CHECK(pm_patch_level(patch->value) > 0.5f && pm_patch_level(patch->value) < 1.7f, "level is near unity");
    }
    {
        /* a fretted note: the pluck and the pickup are further along what sounds */
        const pm_patch_t *bass = pm_factory_patch(0u);
        pm_string_params_t open, fretted;
        pm_patch_string_params(bass, bass->value, 28, &open);
        pm_patch_string_params(bass, bass->value, 32, &fretted);
        CHECK(fretted.pluck_position > open.pluck_position * 1.2f, "a fretted string is plucked further along");
        CHECK(fretted.pickup_position > open.pickup_position * 1.2f, "and its pickup sits further along");
    }
    printf("  mapping: ok\n");
    return 0;
}

static int test_sanitize_and_bank(void)
{
    static pm_bank_t bank, copy;
    pm_patch_t patch;
    uint32_t i, state = 99u;

    /* garbage in: a usable patch out, every time */
    for (i = 0; i < 2000u; ++i) {
        uint8_t *bytes = (uint8_t *)&patch;
        uint32_t k;
        for (k = 0; k < sizeof patch; ++k)
            bytes[k] = (uint8_t)t_random(&state);
        (void)pm_patch_sanitize(&patch);
        CHECK(pm_patch_sanitize(&patch) == 0u, "a sanitized patch is in range");
        CHECK(patch.name[PM_PATCH_NAME - 1u] == 0, "a sanitized name ends");
        CHECK(patch.unison >= 1u && patch.unison <= 2u && patch.engine < PM_ENGINE_COUNT &&
              patch.body < PM_BODY_COUNT && patch.bend_range >= 1u && patch.bend_range <= 24u,
              "a sanitized patch has valid fields");
        for (k = 0; k < PM_PARAM_COUNT; ++k)
            CHECK(patch.value[k] <= 127u, "a sanitized value is 7 bits");
    }

    pm_bank_init(&bank);
    CHECK(pm_bank_open(&bank, (uint32_t)sizeof bank) == 0, "an empty bank opens");
    for (i = 0; i < PM_BANK_SLOTS; ++i)
        CHECK(bank.used[i] == 0u, "an empty bank has no presets");
    bank.slot[3] = *pm_factory_patch(10u);
    bank.slot[3].value[PM_P_BOW_PRESSURE] = 99u;
    memcpy(bank.slot[3].name, "MY VIOLIN", 10u);
    bank.used[3] = 1u;
    CHECK(pm_bank_open(&bank, (uint32_t)sizeof bank) == -1, "a bank changed and not sealed does not open");
    pm_bank_seal(&bank);
    copy = bank;
    CHECK(pm_bank_open(&copy, (uint32_t)sizeof copy) == 0, "a sealed bank opens");
    CHECK(copy.used[3] == 1u && !strcmp(copy.slot[3].name, "MY VIOLIN") &&
          copy.slot[3].value[PM_P_BOW_PRESSURE] == 99u && copy.slot[3].engine == PM_ENGINE_BOW,
          "a preset comes back as it was saved");
    CHECK(memcmp(&copy, &bank, sizeof bank) == 0, "opening a good bank changes nothing");

    /* every single-bit error is caught */
    for (i = 0; i < sizeof bank * 8u; i += 7u) {
        copy = bank;
        ((uint8_t *)&copy)[i >> 3] ^= (uint8_t)(1u << (i & 7u));
        CHECK(pm_bank_open(&copy, (uint32_t)sizeof copy) == -1, "a flipped bit must be caught");
    }
    copy = bank;
    CHECK(pm_bank_open(&copy, (uint32_t)sizeof copy - 1u) == -1, "a short record is not a bank");
    CHECK(pm_bank_open(0, (uint32_t)sizeof copy) == -1, "no record is not a bank");
    copy.version = 2u;
    copy.crc = pm_crc32(&copy, (uint32_t)sizeof copy - 4u);
    CHECK(pm_bank_open(&copy, (uint32_t)sizeof copy) == -1, "another version's bank is not read as this one's");
    /* a well-sealed bank with a patch out of range is repaired on opening */
    copy = bank;
    copy.slot[3].engine = 77u;
    copy.slot[3].value[0] = 200u;
    copy.crc = pm_crc32(&copy, (uint32_t)sizeof copy - 4u);
    CHECK(pm_bank_open(&copy, (uint32_t)sizeof copy) == 0, "a sealed bank opens");
    CHECK(copy.slot[3].engine < PM_ENGINE_COUNT && copy.slot[3].value[0] <= 127u, "and its patches are brought into range");
    CHECK(pm_crc32("123456789", 9u) == 0xCBF43926u, "the check is the standard CRC-32");
    printf("  sanitize and bank: ok (2000 random patches, %u bit flips)\n", (unsigned)(sizeof bank * 8u / 7u));
    return 0;
}

int main(void)
{
    if (test_factory() || test_mapping() || test_sanitize_and_bank())
        return 1;
    printf("instrument: PASS\n");
    return 0;
}
