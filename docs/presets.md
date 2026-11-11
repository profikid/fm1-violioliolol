# Preset format

A preset is a physical-model parameter set, never audio (INS-005). The format
is defined in `dsp/instrument.h`; this page is the reference.

## The patch: 52 bytes

| Offset | Size | Field | Range |
|---|---|---|---|
| 0 | 12 | name | upper-case ASCII, zero-terminated |
| 12 | 1 | family | 0 bass, 1 guitar, 2 bowed, 3 folk |
| 13 | 1 | engine | 0 plucked, 1 bowed |
| 14 | 1 | body | 0 none … 13 zither (`PM_BODY_*`) |
| 15 | 1 | unison | 1, or 2 (a course of two strings) |
| 16 | 8 | page[2][4] | parameter numbers on K1–K4, MAIN then MORE |
| 24 | 1 | mod_dest | parameter CC1 raises, 255 none |
| 25 | 1 | pressure_dest | parameter pressure raises, 255 none |
| 26 | 1 | bend_range | 1 … 24 semitones |
| 27 | 1 | key_octave | −3 … 3 (signed) |
| 28 | 1 | open_note | lowest open string (MIDI note), 0: none |
| 29 | 1 | string_interval | semitones between open strings, 0: none |
| 30 | 1 | detune_cents | between the strings of a course, 0 … 50 |
| 31 | 1 | reserved | 0 |
| 32 | 20 | value[20] | each parameter, 0 … 127 |

`open_note` and `string_interval` place the pluck and the pickup on a
fretted string: a note five frets up a bass string is plucked at the same
spot on the instrument, which is a larger fraction of what sounds.

## Parameter numbers

These are part of the format: new ones are added at the end, never
renumbered.

| # | Name | # | Name |
|---|---|---|---|
| 0 | PLUCK POS | 10 | DRIVE |
| 1 | INTENSITY | 11 | STYLE (0–31 finger, 32–63 pick, 64–95 slap, 96–127 hammer) |
| 2 | DAMPING | 12 | BUZZ |
| 3 | BODY | 13 | BOW PRESS |
| 4 | DECAY | 14 | BOW SPEED |
| 5 | STIFFNESS | 15 | BOW POS |
| 6 | RELEASE | 16 | VIBRATO |
| 7 | TONE | 17 | VIB RATE |
| 8 | PICKUP POS | 18 | ATTACK |
| 9 | PU BLEND | 19 | LEVEL (100 is unity) |

## The bank: 860 bytes, one store object

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `0x31564D50` ("PMV1") |
| 4 | 2 | version, 1 |
| 6 | 2 | slots, 16 |
| 8 | 832 | 16 patches |
| 840 | 16 | used[16]: 1 when the slot holds a preset |
| 856 | 4 | CRC-32 (zlib) of the 856 bytes before it |

It is kept in fm1-base's store (object `OBJ_BANK`, A/B sectors at 0xDE000),
so a write cut short by power loss leaves the previous copy. On loading, a
record with another magic, version or size, or a bad CRC, is not read: the
bank starts empty. Every patch of a good record is still range-checked
(`pm_patch_sanitize`) before it is played.

The settings (`OBJ_SETTINGS`, 0xFC000) are a separate 12-byte record: the
instrument you were on, octave, polyphony and key curve.
