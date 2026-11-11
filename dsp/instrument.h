/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: instruments as parameter sets.
 *
 * A patch is a physical-model preset: which engine (plucked or bowed), which
 * body, and twenty 7-bit parameters. It holds no audio. The factory table
 * covers the four families of the PRD; a bank holds the player's own.
 *
 * Platform-independent. No allocation, I/O, locks or platform headers.
 */
#ifndef FM1_INSTRUMENT_H
#define FM1_INSTRUMENT_H

#include <stdint.h>
#include "string_model.h"
#include "bowed_model.h"
#include "body_model.h"

enum { PM_FAMILY_BASS, PM_FAMILY_GUITAR, PM_FAMILY_BOWED, PM_FAMILY_FOLK, PM_FAMILY_COUNT };
enum { PM_ENGINE_PLUCK, PM_ENGINE_BOW, PM_ENGINE_COUNT };
enum {
    PM_BODY_NONE, PM_BODY_NYLON, PM_BODY_STEEL, PM_BODY_ARCHTOP, PM_BODY_UPRIGHT,
    PM_BODY_VIOLIN, PM_BODY_VIOLA, PM_BODY_CELLO, PM_BODY_CONTRABASS,
    PM_BODY_HARP, PM_BODY_MANDOLIN, PM_BODY_BANJO, PM_BODY_KOTO, PM_BODY_ZITHER,
    PM_BODY_COUNT
};

/* The parameters, each 0 .. 127. Their numbers are part of the preset format:
 * add new ones at the end, never renumber. */
enum {
    PM_P_PLUCK_POS,     /* where the string is plucked: bridge .. middle */
    PM_P_INTENSITY,     /* how hard: the excitation's brightness */
    PM_P_DAMPING,       /* high-frequency loss and shorter sustain (bowed: a darker bridge) */
    PM_P_BODY,          /* how much goes through the body's resonances */
    PM_P_DECAY,         /* sustain time while held */
    PM_P_STIFFNESS,     /* inharmonicity */
    PM_P_RELEASE,       /* how fast the string is damped (bowed: the bow lifted) on note-off */
    PM_P_TONE,          /* output lowpass: a pickup's tone control */
    PM_P_PICKUP_POS,    /* where the pickup sits: bridge .. neck */
    PM_P_PICKUP_BLEND,  /* string output .. pickup output */
    PM_P_DRIVE,         /* soft clipping */
    PM_P_STYLE,         /* excitation style: finger, pick, slap, hammer (a quarter of the range each) */
    PM_P_BUZZ,          /* the string rattling on the frets or the fingerboard */
    PM_P_BOW_PRESSURE,
    PM_P_BOW_SPEED,
    PM_P_BOW_POS,       /* the bow's contact point: bridge .. fingerboard */
    PM_P_VIBRATO,       /* depth */
    PM_P_VIB_RATE,
    PM_P_ATTACK,        /* the bow reaching its speed */
    PM_P_LEVEL,         /* 100 is unity */
    PM_PARAM_COUNT
};
#define PM_DEST_NONE 255u           /* a controller mapped to nothing */

#define PM_PATCH_NAME 12u
#define PM_PATCH_PAGES 2u

/* Bytes only: the same layout on every compiler, 52 bytes. */
typedef struct {
    char name[PM_PATCH_NAME];       /* upper case ASCII, zero-terminated */
    uint8_t family;                 /* PM_FAMILY_* */
    uint8_t engine;                 /* PM_ENGINE_* */
    uint8_t body;                   /* PM_BODY_* */
    uint8_t unison;                 /* strings per note: 1, or 2 (a course, as a mandolin's) */
    uint8_t page[PM_PATCH_PAGES][4];/* the parameters on K1..K4: the primary page, then MORE */
    uint8_t mod_dest;               /* what the modulation wheel (CC1) raises: PM_P_* or PM_DEST_NONE */
    uint8_t pressure_dest;          /* what channel pressure and the keys' pressure raise */
    uint8_t bend_range;             /* pitch bend, semitones each way: 1 .. 24 */
    int8_t key_octave;              /* the FM-1's own keys play this many octaves away: -3 .. 3 */
    uint8_t open_note;              /* the lowest open string (MIDI note); 0: positions follow the note */
    uint8_t string_interval;        /* semitones between open strings: 5 fourths, 7 fifths */
    uint8_t detune_cents;           /* between the two strings of a course */
    uint8_t reserved;               /* 0 */
    uint8_t value[PM_PARAM_COUNT];
} pm_patch_t;

uint32_t pm_factory_count(void);
const pm_patch_t *pm_factory_patch(uint32_t index);     /* NULL beyond the table */
uint32_t pm_family_first(uint32_t family);              /* the family's first factory index */
uint32_t pm_family_count(uint32_t family);
const char *pm_family_name(uint32_t family);
const char *pm_param_name(uint32_t param);              /* at most 10 characters */
const char *pm_style_name(uint32_t value);              /* a PM_P_STYLE value's style */
uint32_t pm_style_of(uint32_t value);                   /* PM_EXCITE_FINGER .. PM_EXCITE_HAMMER */

/* Bring every field of a patch into range: what is loaded from flash or
 * received is never trusted. Returns the number of fields changed. */
uint32_t pm_patch_sanitize(pm_patch_t *patch);

/* The models' parameters for a note, from a patch and its current values
 * (the patch's own, or those the controllers have moved). */
void pm_patch_string_params(const pm_patch_t *patch, const uint8_t *value, int32_t note,
                            pm_string_params_t *out);
void pm_patch_bowed_params(const pm_patch_t *patch, const uint8_t *value,
                           pm_bowed_params_t *out);
float pm_patch_level(const uint8_t *value);             /* PM_P_LEVEL as a gain */
float pm_patch_vibrato_semitones(const uint8_t *value);
float pm_patch_vibrato_hz(const uint8_t *value);

const pm_body_mode_t *pm_body_modes(uint32_t body, uint32_t *count);

/* The player's presets: a bank of slots, kept as one record. */
#define PM_BANK_MAGIC 0x31564D50u   /* "PMV1" */
#define PM_BANK_VERSION 1u
#define PM_BANK_SLOTS 16u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t slots;                 /* PM_BANK_SLOTS */
    pm_patch_t slot[PM_BANK_SLOTS];
    uint8_t used[PM_BANK_SLOTS];    /* 1: the slot holds a preset */
    uint32_t crc;                   /* CRC-32 of everything before it */
} pm_bank_t;

void pm_bank_init(pm_bank_t *bank);                     /* empty, sealed */
void pm_bank_seal(pm_bank_t *bank);                     /* set the check before saving */
/* 0 when the record is a bank this build reads (magic, version, size, check);
 * its patches are then sanitized in place. -1: not one; the caller starts empty. */
int pm_bank_open(pm_bank_t *bank, uint32_t bytes);
uint32_t pm_crc32(const void *data, uint32_t bytes);

#endif
