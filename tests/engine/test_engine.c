/* SPDX-License-Identifier: MIT
 * Native tests of the engine: voices, allocation, MIDI, safety.
 * DSP-016, DSP-017, MIDI-001 .. MIDI-010, VOX-001 .. VOX-007 (host side), UI-006.
 */
#include <string.h>
#include "engine.h"
#include "test_util.h"

#define SECOND 44100u
#define BLOCK 256u
static pm_engine_t engine, twin;
static float buffer[SECOND * 8u];
static float other[SECOND * 8u];

static void run(pm_engine_t *e, float *out, uint32_t frames)
{
    uint32_t done = 0u;
    while (done < frames) {
        const uint32_t n = frames - done > BLOCK ? BLOCK : frames - done;
        pm_engine_render(e, out + done, n);
        done += n;
    }
}

static void fresh(pm_engine_t *e, uint32_t patch)
{
    pm_engine_init(e, 1u);
    pm_engine_set_patch(e, pm_factory_patch(patch));
}

static uint32_t find_patch(const char *name)
{
    uint32_t i;
    for (i = 0; i < pm_factory_count(); ++i)
        if (!strcmp(pm_factory_patch(i)->name, name))
            return i;
    return 0u;
}

static uint32_t count_state(const pm_engine_t *e, uint32_t state)
{
    uint32_t i, count = 0u;
    for (i = 0; i < PM_MAX_VOICES; ++i)
        count += e->voice[i].state == state;
    return count;
}

/* INS-001 .. INS-006: every instrument plays, stays in bounds, and gives its voices back. */
static int test_instruments(void)
{
    static const uint8_t CHORD[4] = {0, 4, 7, 12};
    uint32_t i, k;
    double loudest = 0.0, quietest = 10.0;
    for (i = 0; i < pm_factory_count(); ++i) {
        const pm_patch_t *patch = pm_factory_patch(i);
        const uint32_t root = (uint32_t)(60 + 12 * patch->key_octave);
        double level;
        fresh(&engine, i);
        for (k = 0; k < 4u; ++k)
            pm_engine_midi(&engine, 0x90u, root + CHORD[k], 100u);
        run(&engine, buffer, SECOND / 4u);
        CHECK(engine.active_voices == 4u * patch->unison, "a four-note chord uses four voices (eight for a course)");
        run(&engine, buffer + SECOND / 4u, SECOND * 2u - SECOND / 4u);
        CHECK(t_finite(buffer, SECOND * 2u), "every instrument must remain finite");
        CHECK(t_peak(buffer, SECOND * 2u) <= 1.0, "every instrument must stay inside full scale");
        level = t_rms(buffer, SECOND / 2u);
        CHECK(level > 0.01, "every instrument must sound");
        if (level > loudest)
            loudest = level;
        if (level < quietest)
            quietest = level;
        for (k = 0; k < 4u; ++k)
            pm_engine_midi(&engine, 0x80u, root + CHORD[k], 0u);
        run(&engine, buffer, SECOND * 3u);
        CHECK(engine.active_voices == 0u, "released voices must come back to the pool");
        CHECK(t_peak(buffer + SECOND * 2u, SECOND) < 2.0e-3, "a released instrument must fall silent");
        CHECK(engine.faults == 0u, "no voice may leave its bounds");
    }
    printf("  instruments: ok (%u patches, chord level %.3f .. %.3f rms)\n",
           (unsigned)pm_factory_count(), quietest, loudest);
    return 0;
}

/* MIDI-009, VOX-003: which voice plays which note is decided the same way every time. */
static int test_allocator(void)
{
    uint32_t i;
    fresh(&engine, find_patch("NYLON"));
    pm_engine_post(&engine, PM_EV_POLYPHONY, 0u, 4u);
    run(&engine, buffer, BLOCK);
    for (i = 0; i < 4u; ++i)
        pm_engine_note_on(&engine, 60u + i, 100u);
    run(&engine, buffer, BLOCK);
    for (i = 0; i < 4u; ++i)
        CHECK(engine.voice[i].state == PM_VOICE_HELD && engine.voice[i].note == 60u + i,
              "free voices are taken in order");
    CHECK(engine.active_voices == 4u, "four notes, four voices");

    /* a fifth note takes the oldest */
    pm_engine_note_on(&engine, 72u, 100u);
    CHECK(engine.voice[0].state == PM_VOICE_STEALING, "the oldest voice is the one taken");
    CHECK(engine.steals == 1u, "a steal is counted");
    run(&engine, buffer, BLOCK);
    CHECK(engine.voice[0].state == PM_VOICE_HELD && engine.voice[0].note == 72u, "the new note plays on the taken voice");
    CHECK(engine.active_voices == 4u, "polyphony holds at its limit");

    /* a released voice goes before a held one, however old the held one is */
    pm_engine_note_off(&engine, 63u);
    CHECK(engine.voice[3].state == PM_VOICE_RELEASED, "a lifted key releases its voice");
    pm_engine_note_on(&engine, 74u, 100u);
    CHECK(engine.voice[3].state == PM_VOICE_STEALING && engine.voice[1].state == PM_VOICE_HELD,
          "a released voice is taken before an older held one");
    run(&engine, buffer, BLOCK);
    CHECK(engine.voice[3].note == 74u, "and plays the new note");

    /* the same note again plays on its own voice */
    pm_engine_note_on(&engine, 61u, 80u);
    CHECK(engine.voice[1].state == PM_VOICE_STEALING && engine.voice[1].pending_note == 61u,
          "a repeated note plays on the voice it already has");
    run(&engine, buffer, BLOCK);
    CHECK(engine.active_voices == 4u, "a repeated note uses no second voice");

    /* a note let go while its voice is still fading in for it is not left hanging */
    pm_engine_note_on(&engine, 80u, 100u);
    pm_engine_note_off(&engine, 80u);
    run(&engine, buffer, BLOCK);
    CHECK(count_state(&engine, PM_VOICE_RELEASED) == 1u, "a note let go during the steal ends up released");

    /* velocity 0 is note-off */
    fresh(&engine, find_patch("NYLON"));
    pm_engine_midi(&engine, 0x90u, 60u, 100u);
    pm_engine_midi(&engine, 0x90u, 60u, 0u);
    CHECK(engine.voice[0].state == PM_VOICE_RELEASED, "note-on with velocity 0 is note-off");
    /* notes out of range do nothing */
    pm_engine_note_on(&engine, 200u, 100u);
    CHECK(count_state(&engine, PM_VOICE_HELD) == 0u, "a note number out of range is ignored");

    /* the same performance twice: the same samples */
    fresh(&engine, find_patch("STEEL"));
    fresh(&twin, find_patch("STEEL"));
    {
        uint32_t state = 5150u, block;
        for (block = 0; block < 600u; ++block) {
            if ((block % 7u) == 0u) {
                const uint32_t note = 40u + t_random(&state) % 40u, velocity = 1u + t_random(&state) % 127u;
                pm_engine_note_on(&engine, note, velocity);
                pm_engine_note_on(&twin, note, velocity);
            }
            if ((block % 11u) == 5u) {
                const uint32_t note = 40u + t_random(&state) % 40u;
                pm_engine_note_off(&engine, note);
                pm_engine_note_off(&twin, note);
            }
            pm_engine_render(&engine, buffer, BLOCK);
            pm_engine_render(&twin, other, BLOCK);
            for (i = 0; i < BLOCK; ++i)
                CHECK(buffer[i] == other[i], "the same performance must give the same samples");
        }
    }
    printf("  allocator: ok (order, oldest, released first, repeats, deterministic)\n");
    return 0;
}

/* VOX-004: taking a sounding voice does not click. */
static int test_click_safe(void)
{
    uint32_t i;
    double playing, stealing = 0.0;
    fresh(&engine, find_patch("FINGER BASS"));
    pm_engine_post(&engine, PM_EV_POLYPHONY, 0u, 1u);
    pm_engine_note_on(&engine, 33u, 127u);
    run(&engine, buffer, SECOND / 4u);
    playing = t_max_step(buffer + 4410u, SECOND / 4u - 4410u);
    for (i = 0; i < 40u; ++i) {
        /* a low, loud note cut by a very high, quiet one, at every phase */
        pm_engine_note_on(&engine, (i & 1u) ? 33u : 96u, (i & 1u) ? 127u : 1u);
        run(&engine, buffer, 1024u + i * 13u);
        if (!(i & 1u)) {
            const double step = t_max_step(buffer, 512u);
            if (step > stealing)
                stealing = step;
        }
    }
    CHECK(stealing < 2.0 * playing + 0.02, "taking a voice must not step more than the music does");
    /* all sound off fades too */
    fresh(&engine, find_patch("CELLO"));
    pm_engine_note_on(&engine, 48u, 127u);
    run(&engine, buffer, SECOND);
    playing = t_max_step(buffer + SECOND / 2u, SECOND / 2u);
    pm_engine_control(&engine, 120u, 0u);
    run(&engine, buffer, 4096u);
    CHECK(t_max_step(buffer, 4096u) < 2.0 * playing + 0.02, "all sound off must fade, not cut");
    CHECK(engine.active_voices == 0u, "all sound off stops every voice");
    run(&engine, buffer, SECOND);       /* (the body rings on for a moment, as a body does) */
    CHECK(t_peak(buffer + SECOND / 2u, SECOND / 2u) < 1.0e-4, "and silence follows");
    /* changing the instrument under held notes fades them */
    fresh(&engine, find_patch("VIOLIN"));
    pm_engine_note_on(&engine, 67u, 127u);
    run(&engine, buffer, SECOND);
    pm_engine_post_patch(&engine, pm_factory_patch(find_patch("HARP")));
    run(&engine, buffer, 4096u);
    CHECK(t_max_step(buffer, 4096u) < 0.25, "changing the instrument must not click");
    CHECK(engine.active_voices == 0u && !strcmp(engine.patch.name, "HARP"), "the new instrument is in place");
    printf("  click safety: ok (largest step while stealing %.3f, while playing %.3f)\n", stealing, playing);
    return 0;
}

/* MIDI-001 .. MIDI-008. */
static int test_midi(void)
{
    double plain, level, bent;
    uint32_t dest;

    /* MIDI-002: velocity */
    fresh(&engine, find_patch("NYLON"));
    pm_engine_midi(&engine, 0x90u, 57u, 30u);
    run(&engine, buffer, SECOND / 2u);
    plain = t_rms(buffer, SECOND / 2u);
    fresh(&engine, find_patch("NYLON"));
    pm_engine_midi(&engine, 0x93u, 57u, 127u);          /* any channel */
    run(&engine, buffer, SECOND / 2u);
    CHECK(t_rms(buffer, SECOND / 2u) > 3.0 * plain, "velocity should set how loud a note is");

    /* MIDI-003: pitch bend, both engines, no voice reset */
    {
        static const char *const NAMES[2] = {"NYLON", "CELLO"};
        uint32_t k;
        for (k = 0; k < 2u; ++k) {
            const double hz = 220.0;
            uint32_t age;
            fresh(&engine, find_patch(NAMES[k]));
            pm_engine_midi(&engine, 0x90u, 57u, 110u);
            run(&engine, buffer, SECOND);
            age = engine.voice[0].age;
            CHECK(fabs(t_cents(t_pitch(buffer + SECOND / 2u, 20000u, hz), hz)) < 10.0, "an unbent note is in tune");
            pm_engine_midi(&engine, 0xE0u, 0x7Fu, 0x7Fu);       /* wheel fully up: +2 semitones */
            run(&engine, buffer, SECOND);
            bent = t_cents(t_pitch(buffer + SECOND / 2u, 20000u, hz * 1.122462), hz);
            CHECK(fabs(bent - 200.0) < 12.0, "the wheel fully up is two semitones");
            CHECK(engine.voice[0].age == age && engine.voice[0].state == PM_VOICE_HELD, "a bend does not restart the voice");
            pm_engine_midi(&engine, 0xE0u, 0u, 0u);             /* fully down */
            run(&engine, buffer, SECOND);
            bent = t_cents(t_pitch(buffer + SECOND / 2u, 20000u, hz / 1.122462), hz);
            CHECK(fabs(bent + 200.0) < 12.0, "the wheel fully down is two semitones down");
            /* a note started with the wheel off centre starts bent */
            pm_engine_midi(&engine, 0x80u, 57u, 0u);
            pm_engine_midi(&engine, 0x90u, 69u, 110u);
            run(&engine, buffer, SECOND);
            bent = t_cents(t_pitch(buffer + SECOND / 2u, 20000u, 440.0 / 1.122462), 440.0);
            CHECK(fabs(bent + 200.0) < 12.0, "a note started with the wheel down starts two semitones down");
            pm_engine_midi(&engine, 0xE0u, 0u, 0x40u);
            run(&engine, buffer, SECOND);
            CHECK(fabs(t_cents(t_pitch(buffer + SECOND / 2u, 20000u, 440.0), 440.0)) < 10.0,
                  "the wheel centred is in tune again");
        }
    }

    /* MIDI-004: the modulation wheel raises what the patch maps it to */
    fresh(&engine, find_patch("VIOLIN"));
    dest = engine.patch.mod_dest;
    CHECK(dest == PM_P_VIBRATO, "the violin's wheel is vibrato");
    pm_engine_midi(&engine, 0xB0u, 1u, 127u);
    CHECK(engine.value[dest] == 127u && engine.patch.value[dest] < 127u, "CC1 raises the value, not the patch");
    pm_engine_midi(&engine, 0xB0u, 1u, 0u);
    CHECK(engine.value[dest] == engine.patch.value[dest], "CC1 at rest leaves the patch's value");
    fresh(&engine, find_patch("CLEAN ELEC"));
    CHECK(engine.patch.mod_dest == PM_P_PICKUP_BLEND, "an electric guitar's wheel is its pickup blend");

    /* MIDI-005 and MIDI-006: volume and expression */
    fresh(&engine, find_patch("VIOLIN"));
    pm_engine_midi(&engine, 0x90u, 67u, 100u);
    run(&engine, buffer, SECOND);
    plain = t_rms(buffer + SECOND / 2u, SECOND / 2u);
    pm_engine_midi(&engine, 0xB0u, 7u, 50u);
    run(&engine, buffer, SECOND);
    level = t_rms(buffer + SECOND / 2u, SECOND / 2u);
    CHECK(level < 0.4 * plain && level > 0.1 * plain, "CC7 at 50 is about a quarter of the level at 100");
    CHECK(t_max_step(buffer, 512u) < 0.2, "a volume change must not click");
    pm_engine_midi(&engine, 0xB0u, 7u, 100u);
    pm_engine_midi(&engine, 0xB0u, 11u, 20u);
    run(&engine, buffer, SECOND);
    level = t_rms(buffer + SECOND / 2u, SECOND / 2u);
    CHECK(level < 0.6 * plain, "CC11 down is quieter");
    pm_engine_midi(&engine, 0xB0u, 11u, 0u);
    run(&engine, buffer, SECOND);
    CHECK(t_rms(buffer + SECOND / 2u, SECOND / 2u) > 0.001, "a bowed string at no expression still whispers");
    fresh(&engine, find_patch("NYLON"));
    pm_engine_midi(&engine, 0xB0u, 11u, 0u);
    pm_engine_midi(&engine, 0x90u, 60u, 100u);
    run(&engine, buffer, SECOND / 4u);
    CHECK(t_peak(buffer + 2048u, 4096u) < 1.0e-4, "a plucked string at no expression is silent");

    /* MIDI-007: the sustain pedal */
    fresh(&engine, find_patch("NYLON"));
    pm_engine_midi(&engine, 0xB0u, 64u, 127u);
    pm_engine_midi(&engine, 0x90u, 60u, 100u);
    pm_engine_midi(&engine, 0x90u, 64u, 100u);
    pm_engine_midi(&engine, 0x80u, 60u, 0u);
    pm_engine_midi(&engine, 0x80u, 64u, 64u);
    CHECK(count_state(&engine, PM_VOICE_SUSTAINED) == 2u, "the pedal holds notes that were let go");
    run(&engine, buffer, SECOND / 2u);
    CHECK(engine.active_voices == 2u && t_rms(buffer + SECOND / 4u, SECOND / 4u) > 0.005, "and they go on sounding");
    pm_engine_midi(&engine, 0x90u, 67u, 100u);
    pm_engine_midi(&engine, 0xB0u, 64u, 0u);
    CHECK(count_state(&engine, PM_VOICE_RELEASED) == 2u && count_state(&engine, PM_VOICE_HELD) == 1u,
          "lifting the pedal releases them, and not the key still down");
    run(&engine, buffer, SECOND / 2u);
    CHECK(engine.active_voices == 1u, "the released notes end; the held one stays");

    /* MIDI-008: channel pressure */
    fresh(&engine, find_patch("CELLO"));
    dest = engine.patch.pressure_dest;
    CHECK(dest == PM_P_BOW_PRESSURE, "the cello's pressure is bow pressure");
    pm_engine_midi(&engine, 0xD0u, 100u, 0u);
    CHECK(engine.value[dest] > engine.patch.value[dest], "channel pressure raises what the patch maps it to");
    pm_engine_midi(&engine, 0xD0u, 0u, 0u);
    pm_engine_midi(&engine, 0xA0u, 60u, 127u);
    CHECK(engine.value[dest] == 127u, "a key's own pressure is taken as the channel's");

    /* the knobs over MIDI, a program change, the housekeeping controllers */
    fresh(&engine, find_patch("VIOLIN"));
    pm_engine_midi(&engine, 0xB0u, 16u, 5u);
    CHECK(engine.patch.value[engine.patch.page[0][0]] == 5u, "CC16 is the first knob");
    CHECK(engine.midi_params == 1u << engine.patch.page[0][0], "and the UI is told which value changed");
    pm_engine_midi(&engine, 0xB0u, 23u, 99u);
    CHECK(engine.patch.value[engine.patch.page[1][3]] == 99u, "CC23 is the fourth knob of the second page");
    pm_engine_midi(&engine, 0xC0u, 12u, 0u);
    CHECK(engine.program_request == 13u, "a program change is handed to the UI");
    pm_engine_midi(&engine, 0xB0u, 1u, 90u);
    pm_engine_midi(&engine, 0xE0u, 0u, 0u);
    pm_engine_midi(&engine, 0xB0u, 64u, 127u);
    pm_engine_midi(&engine, 0xB0u, 121u, 0u);
    CHECK(engine.mod == 0u && engine.bend == 8192u && !engine.sustain && engine.expression == 127u,
          "reset all controllers puts them back");
    pm_engine_midi(&engine, 0x90u, 60u, 100u);
    pm_engine_midi(&engine, 0x90u, 64u, 100u);
    pm_engine_midi(&engine, 0xB0u, 123u, 0u);
    CHECK(count_state(&engine, PM_VOICE_RELEASED) == 2u, "all notes off lets every key go");
    pm_engine_midi(&engine, 0xF8u, 0u, 0u);
    pm_engine_midi(&engine, 0xB0u, 99u, 99u);
    printf("  midi: ok (notes, velocity, bend, CC1, CC7, CC11, CC64, pressure, knobs, program)\n");
    return 0;
}

/* UI-006: the control task only posts; the render takes it at a block's start. */
static int test_queue(void)
{
    uint32_t i;
    pm_patch_t patch;
    fresh(&engine, find_patch("NYLON"));
    CHECK(pm_engine_post(&engine, PM_EV_NOTE_ON, 60u, 100u) == 0, "a message is queued");
    CHECK(engine.voice[0].state == PM_VOICE_IDLE, "posting touches no voice");
    run(&engine, buffer, BLOCK);
    CHECK(engine.voice[0].state == PM_VOICE_HELD && engine.voice[0].note == 60u, "the render plays it");
    CHECK(pm_engine_post(&engine, PM_EV_NOTE_OFF, 60u, 0u) == 0, "a message is queued");
    run(&engine, buffer, BLOCK);
    CHECK(engine.voice[0].state == PM_VOICE_RELEASED, "the render releases it");

    /* a full queue refuses, counts, and loses nothing it took */
    for (i = 0; i < PM_QUEUE_SIZE; ++i)
        CHECK(pm_engine_post(&engine, PM_EV_PARAM, PM_P_BODY, i & 127u) == 0, "the queue takes its size");
    CHECK(pm_engine_post(&engine, PM_EV_PARAM, PM_P_BODY, 1u) == -1, "a full queue refuses");
    CHECK(engine.dropped == 1u, "and counts what it refused");
    run(&engine, buffer, BLOCK);
    CHECK(engine.patch.value[PM_P_BODY] == ((PM_QUEUE_SIZE - 1u) & 127u), "every queued message was applied, in order");
    CHECK(pm_engine_post(&engine, PM_EV_PARAM, PM_P_BODY, 64u) == 0, "a drained queue takes messages again");

    /* a patch handed over */
    patch = *pm_factory_patch(find_patch("VIOLIN"));
    patch.value[PM_P_BODY] = 33u;
    CHECK(pm_engine_post_patch(&engine, &patch) == 0, "a patch is queued");
    CHECK(!strcmp(engine.patch.name, "NYLON"), "posting a patch does not change the playing one");
    run(&engine, buffer, BLOCK);
    CHECK(!strcmp(engine.patch.name, "VIOLIN") && engine.patch.value[PM_P_BODY] == 33u, "the render takes the patch");
    /* a hand-over caught half-written is skipped; the next one brings a whole patch */
    patch = *pm_factory_patch(find_patch("HARP"));
    engine.patch_lock = 1u;
    engine.patch_pending = patch;
    CHECK(pm_engine_post(&engine, PM_EV_PATCH, 0u, 0u) == 0, "a message is queued");
    run(&engine, buffer, BLOCK);
    CHECK(!strcmp(engine.patch.name, "VIOLIN"), "a locked hand-over is not taken");
    engine.patch_lock = 0u;
    CHECK(pm_engine_post_patch(&engine, &patch) == 0, "a patch is queued");
    run(&engine, buffer, BLOCK);
    CHECK(!strcmp(engine.patch.name, "HARP"), "the next hand-over is");
    /* a patch out of range is brought into range, not played as it is */
    memset(&patch, 0xEE, sizeof patch);
    CHECK(pm_engine_post_patch(&engine, &patch) == 0, "a patch is queued");
    pm_engine_post(&engine, PM_EV_NOTE_ON, 60u, 100u);
    run(&engine, buffer, SECOND);
    CHECK(t_finite(buffer, SECOND) && t_peak(buffer, SECOND) <= 1.0, "a patch of garbage still plays in bounds");
    /* messages that mean nothing do nothing */
    pm_engine_post(&engine, 200u, 1u, 1u);
    pm_engine_post(&engine, PM_EV_PARAM, 999u, 1u);
    pm_engine_post(&engine, PM_EV_BEND, 60000u, 0u);
    pm_engine_post(&engine, PM_EV_POLYPHONY, 0u, 0u);
    pm_engine_post(&engine, PM_EV_BEND_RANGE, 0u, 200u);
    run(&engine, buffer, BLOCK);
    CHECK(engine.polyphony == 1u && engine.patch.bend_range == 24u, "out-of-range settings are clamped");
    CHECK(pm_engine_post(0, PM_EV_NOTE_ON, 60u, 100u) == -1 && pm_engine_post_patch(&engine, 0) == -1,
          "nothing to post to, nothing posted");
    printf("  queue: ok (hand-over, order, full queue, half-written patch)\n");
    return 0;
}

/* MIDI-010, VOX-005 .. VOX-007, DSP-017: 4, 8 and 12 voices, mixed inside full scale. */
static int test_polyphony(void)
{
    static const uint32_t LIMITS[3] = {4u, 8u, 12u};
    uint32_t i, k, patch;
    for (patch = 0; patch < pm_factory_count(); ++patch) {
        for (k = 0; k < 3u; ++k) {
            const pm_patch_t *p = pm_factory_patch(patch);
            fresh(&engine, patch);
            pm_engine_post(&engine, PM_EV_POLYPHONY, 0u, LIMITS[k]);
            run(&engine, buffer, BLOCK);
            for (i = 0; i < 12u; ++i)
                pm_engine_note_on(&engine, (uint32_t)(48 + 12 * p->key_octave) + i * 3u, 127u);
            run(&engine, buffer, SECOND / 8u);
            CHECK(engine.active_voices == LIMITS[k], "the polyphony limit is the number of voices sounding");
            run(&engine, buffer + SECOND / 8u, SECOND - SECOND / 8u);
            CHECK(t_finite(buffer, SECOND), "many voices must remain finite");
            CHECK(t_peak(buffer, SECOND) <= 1.0, "many voices at full velocity must stay inside full scale");
            CHECK(engine.faults == 0u, "no voice may leave its bounds");
        }
    }
    /* the limiter is the exception, not the rule: four voices at a moderate velocity leave it alone */
    fresh(&engine, find_patch("NYLON"));
    for (i = 0; i < 4u; ++i)
        pm_engine_note_on(&engine, 52u + i * 5u, 80u);
    run(&engine, buffer, SECOND);
    CHECK(engine.limiter_q8 == 255u, "four voices at a moderate velocity fit the headroom");
    printf("  polyphony: ok (4, 8 and 12 voices of every instrument inside full scale)\n");
    return 0;
}

/* VOX-002, RT-007: the benchmark voices, and shedding load. */
static int test_bench_and_shed(void)
{
    static const uint32_t COUNTS[5] = {1u, 2u, 4u, 8u, 12u};
    uint32_t k;
    fresh(&engine, find_patch("NYLON"));
    for (k = 0; k < 5u; ++k) {
        pm_engine_post(&engine, PM_EV_BENCH, 0u, COUNTS[k]);
        run(&engine, buffer, SECOND * 8u);
        CHECK(engine.active_voices == COUNTS[k], "benchmark voices keep running after they have rung out");
    }
    /* a note played meanwhile takes a voice that is not a benchmark's */
    pm_engine_post(&engine, PM_EV_BENCH, 0u, 4u);
    run(&engine, buffer, BLOCK * 2u);
    pm_engine_note_on(&engine, 90u, 100u);
    run(&engine, buffer, BLOCK);
    CHECK(engine.active_voices == 5u && engine.voice[4].note == 90u, "playing does not disturb the benchmark's voices");
    /* a new instrument keeps the benchmark going, on the new model */
    pm_engine_post_patch(&engine, pm_factory_patch(find_patch("CELLO")));
    run(&engine, buffer, SECOND);
    CHECK(engine.active_voices == 4u && engine.voice[0].engine == PM_ENGINE_BOW, "the benchmark follows the instrument");
    pm_engine_post(&engine, PM_EV_BENCH, 0u, 0u);
    run(&engine, buffer, SECOND);
    CHECK(engine.active_voices == 0u, "benchmark off: no voices");

    /* overload: one voice and one of polyphony less each time, never below two */
    fresh(&engine, find_patch("VIOLIN"));
    for (k = 0; k < 8u; ++k)
        pm_engine_note_on(&engine, 55u + k * 2u, 100u);
    run(&engine, buffer, SECOND / 2u);
    CHECK(engine.active_voices == 8u, "eight notes, eight voices");
    pm_engine_post(&engine, PM_EV_SHED, 0u, 0u);
    run(&engine, buffer, BLOCK);
    CHECK(engine.active_voices == 7u && engine.polyphony == 7u && engine.sheds == 1u, "an overload sheds one voice");
    CHECK(t_max_step(buffer, BLOCK) < 0.3, "and fades it");
    for (k = 0; k < 20u; ++k)
        pm_engine_post(&engine, PM_EV_SHED, 0u, 0u);
    run(&engine, buffer, BLOCK * 4u);
    CHECK(engine.polyphony == 2u, "polyphony never sheds below two");
    printf("  benchmark and shedding: ok\n");
    return 0;
}

/* DSP-016: a voice that leaves its bounds is stopped; the rest plays on. */
static int test_faults(void)
{
    uint32_t i;
    fresh(&engine, find_patch("NYLON"));
    pm_engine_note_on(&engine, 60u, 100u);
    pm_engine_note_on(&engine, 64u, 100u);
    run(&engine, buffer, BLOCK * 4u);
    for (i = 0; i < PM_STRING_DELAY_CAPACITY; ++i)
        engine.voice[0].model.string.delay[i] = (float)NAN;
    run(&engine, buffer, SECOND / 4u);
    CHECK(engine.faults == 1u, "a voice that is not a number is caught");
    CHECK(t_finite(buffer, SECOND / 4u), "and nothing of it reaches the output");
    CHECK(engine.voice[0].state == PM_VOICE_IDLE && engine.voice[1].state == PM_VOICE_HELD, "the other voice plays on");
    CHECK(t_rms(buffer, SECOND / 4u) > 0.005, "and is heard");
    for (i = 0; i < PM_STRING_DELAY_CAPACITY; ++i)
        engine.voice[1].model.string.delay[i] = 1.0e6f;
    engine.voice[1].model.string.feedback = 1.0f;
    run(&engine, buffer, SECOND / 4u);
    CHECK(engine.faults == 2u && t_finite(buffer, SECOND / 4u) && t_peak(buffer, SECOND / 4u) <= 1.0,
          "a voice that runs away is caught");
    pm_engine_note_on(&engine, 67u, 100u);
    run(&engine, buffer, SECOND / 4u);
    CHECK(engine.voice[0].state == PM_VOICE_HELD && t_rms(buffer, SECOND / 4u) > 0.005, "a stopped voice is rebuilt for the next note");
    printf("  faults: ok\n");
    return 0;
}

/* A course of two strings. */
static int test_unison(void)
{
    fresh(&engine, find_patch("MANDOLIN"));
    pm_engine_note_on(&engine, 67u, 100u);
    run(&engine, buffer, BLOCK);
    CHECK(engine.active_voices == 2u, "a mandolin note is two strings");
    CHECK(engine.voice[0].base_note < 67.0f && engine.voice[1].base_note > 67.0f, "a little apart, either side of the note");
    pm_engine_note_on(&engine, 67u, 100u);
    run(&engine, buffer, BLOCK);
    CHECK(engine.active_voices == 2u, "the note again is the same two strings");
    pm_engine_note_off(&engine, 67u);
    CHECK(count_state(&engine, PM_VOICE_RELEASED) == 2u, "letting go releases both");
    printf("  unison: ok\n");
    return 0;
}

/* Parameter extremes and a storm of MIDI: finite, in bounds, no faults. */
static int test_storm(void)
{
    uint32_t state = 0xC0FFEEu, block, peak_blocks = 0u;
    pm_engine_init(&engine, 7u);
    for (block = 0; block < 40000u; ++block) {       /* almost four minutes of audio */
        const uint32_t events = t_random(&state) % 4u;
        uint32_t k;
        for (k = 0; k < events; ++k) {
            const uint32_t r = t_random(&state);
            const uint32_t kind = r % 100u, d1 = (r >> 8) & 0x7Fu, d2 = (r >> 16) & 0x7Fu;
            if (kind < 45u)
                pm_engine_midi(&engine, 0x90u | (r >> 24 & 15u), d1, d2);
            else if (kind < 65u)
                pm_engine_midi(&engine, 0x80u, d1, d2);
            else if (kind < 75u)
                pm_engine_midi(&engine, 0xE0u, d1, d2);
            else if (kind < 87u)
                pm_engine_midi(&engine, 0xB0u, d1, d2);
            else if (kind < 90u)
                pm_engine_midi(&engine, 0xD0u, d1, d2);
            else if (kind < 96u)
                pm_engine_post(&engine, PM_EV_PARAM, d1 % PM_PARAM_COUNT, d2);
            else if (kind < 98u)
                pm_engine_post(&engine, PM_EV_POLYPHONY, 0u, 1u + d1 % PM_MAX_VOICES);
            else if (kind == 98u)
                pm_engine_post_patch(&engine, pm_factory_patch(d1 % pm_factory_count()));
            else
                pm_engine_post(&engine, (r >> 24) % 16u, d1 << 4, d2);
        }
        pm_engine_render(&engine, buffer, BLOCK);
        CHECK(t_finite(buffer, BLOCK), "a storm of MIDI must remain finite");
        CHECK(t_peak(buffer, BLOCK) <= 1.0, "a storm of MIDI must stay inside full scale");
        peak_blocks += t_peak(buffer, BLOCK) > 0.01;
    }
    CHECK(engine.faults == 0u, "no voice may leave its bounds, whatever is played");
    CHECK(peak_blocks > 10000u, "and it was not silent");
    printf("  storm: ok (40000 blocks of random MIDI, patches and parameters; no faults)\n");
    return 0;
}

int main(void)
{
    pm_engine_init(&engine, 1u);
    printf("  memory: engine %lu bytes (%u voice slots of %lu; string %lu, bowed %lu)\n",
           (unsigned long)pm_engine_bytes(), (unsigned)PM_MAX_VOICES, (unsigned long)pm_engine_voice_slot_bytes(),
           (unsigned long)pm_string_voice_bytes(), (unsigned long)pm_bowed_voice_bytes());
    if (test_instruments() || test_allocator() || test_click_safe() || test_midi() || test_queue() ||
        test_polyphony() || test_bench_and_shed() || test_faults() || test_unison() || test_storm())
        return 1;
    printf("engine: PASS\n");
    return 0;
}
