/* SPDX-License-Identifier: MIT */
#include "engine.h"
#include "pm_math.h"

#define PM_QUEUE_MASK (PM_QUEUE_SIZE - 1u)
#define PM_QUIET_PEAK 1.0e-4f           /* a voice below this (-80 dB) has ended */
#define PM_QUIET_FRAMES 512u            /* for this long */
#define PM_FAULT_PEAK 16.0f             /* no healthy voice comes near this */
#define PM_STRING_LOW_NOTE 18.0f        /* 23 Hz: the delay line's length */
#define PM_STRING_HIGH_NOTE 108.0f
#define PM_BOWED_LOW_NOTE 20.0f         /* 26 Hz */
#define PM_BOWED_HIGH_NOTE 105.0f       /* 3520 Hz: above it the bowed string no longer speaks */
#define PM_ENGINE_NONE 255u
/* Keep the compiler from moving plain stores across the hand-over's flag. The
 * FM-1 is one core and the render an interrupt: ordering against the compiler
 * is all that is needed. */
#define PM_BARRIER() __asm__ __volatile__("" ::: "memory")

static const uint8_t BENCH_NOTE[PM_MAX_VOICES] = {33, 40, 45, 50, 55, 57, 62, 67, 69, 74, 76, 81};

/* ------------------------------------------------------------- values --- */

static uint8_t raised(uint32_t base, uint32_t amount)
{
    return (uint8_t)(base + ((127u - base) * amount + 63u) / 127u);
}

/* The patch's values as the controllers have moved them. */
static void recompute_values(pm_engine_t *e)
{
    uint32_t i;
    for (i = 0; i < PM_PARAM_COUNT; ++i)
        e->value[i] = e->patch.value[i];
    if (e->patch.mod_dest < PM_PARAM_COUNT)
        e->value[e->patch.mod_dest] = raised(e->value[e->patch.mod_dest], e->mod);
    if (e->patch.pressure_dest < PM_PARAM_COUNT)
        e->value[e->patch.pressure_dest] = raised(e->value[e->patch.pressure_dest], e->pressure);
    e->dirty = 1u;
}

static float expression_gain(const pm_engine_t *e)
{
    return (float)e->expression * (1.0f / 127.0f);
}

static void bowed_params(const pm_engine_t *e, pm_bowed_params_t *params)
{
    pm_patch_bowed_params(&e->patch, e->value, params);
    /* expression is the bow arm: it slows the bow as well as the level */
    params->bow_speed *= 0.3f + 0.7f * expression_gain(e);
}

/* Push the values to the body, the output stage and the sounding voices. */
static void push_values(pm_engine_t *e)
{
    uint32_t i;
    const float volume = (float)e->volume * 0.01f;
    const float expression = e->patch.engine == PM_ENGINE_BOW ?
                             0.35f + 0.65f * expression_gain(e) : expression_gain(e);
    pm_body_set_wet(&e->body, (float)e->value[PM_P_BODY] * (1.0f / 127.0f));
    pm_output_set_tone(&e->output, (float)e->value[PM_P_TONE] * (1.0f / 127.0f));
    pm_output_set_drive(&e->output, (float)e->value[PM_P_DRIVE] * (1.0f / 127.0f));
    pm_output_set_level(&e->output, pm_patch_level(e->value) * volume * volume * expression);
    for (i = 0; i < PM_MAX_VOICES; ++i) {
        pm_engine_voice_t *vc = &e->voice[i];
        if (vc->state == PM_VOICE_IDLE || vc->state == PM_VOICE_STEALING)
            continue;
        if (vc->engine == PM_ENGINE_BOW) {
            pm_bowed_params_t params;
            bowed_params(e, &params);
            pm_bowed_configure(&vc->model.bowed, &params);
        } else if (vc->engine == PM_ENGINE_PLUCK) {
            pm_string_params_t params;
            pm_patch_string_params(&e->patch, e->value, (int32_t)vc->note, &params);
            pm_string_update(&vc->model.string, &params);
        }
    }
    e->dirty = 0u;
}

/* ------------------------------------------------------------- voices --- */

static float fold(float note, float lo, float hi)
{
    uint32_t guard;
    for (guard = 0; guard < 11u && note < lo; ++guard)
        note += 12.0f;
    for (guard = 0; guard < 11u && note > hi; ++guard)
        note -= 12.0f;
    return note;
}

static float string_ratio(const pm_engine_t *e)
{
    return e->last_ratio;
}

static void stop_model(pm_engine_voice_t *vc)
{
    if (vc->engine == PM_ENGINE_BOW)
        vc->model.bowed.active = 0u;
    else if (vc->engine == PM_ENGINE_PLUCK)
        vc->model.string.active = 0u;
}

static void release_voice(pm_engine_voice_t *vc)
{
    if (vc->engine == PM_ENGINE_BOW)
        pm_bowed_note_off(&vc->model.bowed);
    else if (vc->engine == PM_ENGINE_PLUCK)
        pm_string_note_off(&vc->model.string);
    vc->state = PM_VOICE_RELEASED;
}

/* Start a note on a voice that is silent. The audio task only. */
static void start_voice(pm_engine_t *e, pm_engine_voice_t *vc, uint32_t note, uint32_t velocity,
                        uint32_t string)
{
    float v = (float)(velocity > 127u ? 127u : velocity) * (1.0f / 127.0f);
    float pitch = (float)note;
    int ok;
    v *= 0.25f + 0.75f * v;             /* soft playing is quieter than a straight line gives */
    if (e->patch.unison > 1u)           /* a course: the two strings either side of the note */
        pitch += (string ? 0.005f : -0.005f) * (float)e->patch.detune_cents;
    vc->note = (uint8_t)note;
    vc->string = (uint8_t)string;
    vc->pending = 0u;
    vc->pending_off = 0u;
    vc->quiet_blocks = 0u;
    vc->fade_left = 0u;
    vc->age = ++e->age_counter;
    if (e->patch.engine == PM_ENGINE_BOW) {
        pm_bowed_params_t params;
        if (vc->engine != PM_ENGINE_BOW)
            pm_bowed_init(&vc->model.bowed);
        else
            pm_bowed_reset(&vc->model.bowed);
        vc->engine = PM_ENGINE_BOW;
        vc->base_note = fold(pitch, PM_BOWED_LOW_NOTE, PM_BOWED_HIGH_NOTE);
        bowed_params(e, &params);
        pm_bowed_configure(&vc->model.bowed, &params);
        pm_bowed_set_pitch_ratio(&vc->model.bowed, e->bend_ratio);
        ok = pm_bowed_note_on(&vc->model.bowed, pm_note_hz(vc->base_note), v);
    } else {
        pm_string_params_t params;
        if (vc->engine != PM_ENGINE_PLUCK)
            pm_string_init(&vc->model.string, vc->seed);
        vc->engine = PM_ENGINE_PLUCK;
        vc->base_note = fold(pitch, PM_STRING_LOW_NOTE, PM_STRING_HIGH_NOTE);
        pm_patch_string_params(&e->patch, e->value, (int32_t)note, &params);
        pm_string_configure(&vc->model.string, &params);
        ok = pm_string_note_on(&vc->model.string, pm_note_hz(vc->base_note), 0.995f, v);
        if (ok == 0) {
            /* a note that starts while the wheel is off centre starts there */
            pm_string_set_pitch_ratio(&vc->model.string, string_ratio(e));
            vc->model.string.delay_samples = vc->model.string.delay_target;
        }
    }
    vc->state = ok == 0 ? PM_VOICE_HELD : PM_VOICE_IDLE;
}

/* Take a sounding voice for another note: it fades out first, then starts. */
static void steal_voice(pm_engine_t *e, pm_engine_voice_t *vc, uint32_t note, uint32_t velocity,
                        uint32_t string)
{
    if (vc->state == PM_VOICE_IDLE) {
        start_voice(e, vc, note, velocity, string);
        return;
    }
    if (vc->state != PM_VOICE_STEALING) {
        vc->state = PM_VOICE_STEALING;
        vc->fade_left = PM_FADE_FRAMES;
        ++e->steals;
    }
    vc->pending = 1u;
    vc->pending_note = (uint8_t)note;
    vc->pending_velocity = (uint8_t)velocity;
    vc->pending_string = (uint8_t)string;
    vc->pending_off = 0u;
}

static void fade_voice(pm_engine_voice_t *vc)
{
    if (vc->state == PM_VOICE_IDLE)
        return;
    if (vc->state != PM_VOICE_STEALING) {
        vc->state = PM_VOICE_STEALING;
        vc->fade_left = PM_FADE_FRAMES;
    }
    vc->pending = 0u;
}

/* The voice to take when none is free: one already let go before one the
 * pedal holds before one a key holds, the oldest of its kind; equal ages
 * cannot happen (the count only rises), so the choice is always the same. */
static pm_engine_voice_t *find_steal(pm_engine_t *e)
{
    static const uint8_t ORDER[3] = {PM_VOICE_RELEASED, PM_VOICE_SUSTAINED, PM_VOICE_HELD};
    uint32_t pass, i;
    for (pass = 0; pass < 3u; ++pass) {
        pm_engine_voice_t *best = 0;
        for (i = 0; i < e->polyphony; ++i) {
            pm_engine_voice_t *vc = &e->voice[i];
            if (vc->bench || vc->state != ORDER[pass])
                continue;
            if (!best || vc->age < best->age)
                best = vc;
        }
        if (best)
            return best;
    }
    /* every voice is already being taken: the one that started fading first */
    {
        pm_engine_voice_t *best = 0;
        for (i = 0; i < e->polyphony; ++i) {
            pm_engine_voice_t *vc = &e->voice[i];
            if (vc->bench || vc->state != PM_VOICE_STEALING)
                continue;
            if (!best || vc->age < best->age)
                best = vc;
        }
        return best;
    }
}

void pm_engine_note_on(pm_engine_t *e, uint32_t note, uint32_t velocity)
{
    uint32_t string, i;
    if (!e || note > 127u)
        return;
    if (!velocity) {
        pm_engine_note_off(e, note);
        return;
    }
    for (string = 0; string < e->patch.unison; ++string) {
        pm_engine_voice_t *target = 0;
        /* the same note again plays on the string it is already on */
        for (i = 0; i < PM_MAX_VOICES && !target; ++i) {
            pm_engine_voice_t *vc = &e->voice[i];
            if (vc->bench || vc->state == PM_VOICE_IDLE)
                continue;
            if (vc->state == PM_VOICE_STEALING ?
                (vc->pending && vc->pending_note == note && vc->pending_string == string) :
                (vc->note == note && vc->string == string))
                target = vc;
        }
        for (i = 0; i < e->polyphony && !target; ++i)
            if (e->voice[i].state == PM_VOICE_IDLE && !e->voice[i].bench)
                target = &e->voice[i];
        if (!target)
            target = find_steal(e);
        if (target)
            steal_voice(e, target, note, velocity, string);
    }
}

void pm_engine_note_off(pm_engine_t *e, uint32_t note)
{
    uint32_t i;
    if (!e)
        return;
    for (i = 0; i < PM_MAX_VOICES; ++i) {
        pm_engine_voice_t *vc = &e->voice[i];
        if (vc->bench)
            continue;
        if (vc->state == PM_VOICE_STEALING) {
            if (vc->pending && vc->pending_note == note)
                vc->pending_off = 1u;
        } else if (vc->state == PM_VOICE_HELD && vc->note == note) {
            if (e->sustain)
                vc->state = PM_VOICE_SUSTAINED;
            else
                release_voice(vc);
        }
    }
}

void pm_engine_all_off(pm_engine_t *e)
{
    uint32_t i;
    if (!e)
        return;
    for (i = 0; i < PM_MAX_VOICES; ++i) {
        e->voice[i].bench = 0u;
        fade_voice(&e->voice[i]);
    }
}

static void bench_start(pm_engine_t *e, uint32_t count)
{
    uint32_t i;
    if (count > PM_MAX_VOICES)
        count = PM_MAX_VOICES;
    pm_engine_all_off(e);
    e->bench = (uint8_t)count;
    for (i = 0; i < count; ++i) {
        steal_voice(e, &e->voice[i], BENCH_NOTE[i], 100u, 0u);
        e->voice[i].bench = 1u;
    }
}

static void set_sustain(pm_engine_t *e, uint32_t on)
{
    uint32_t i;
    e->sustain = (uint8_t)(on != 0u);
    if (on)
        return;
    for (i = 0; i < PM_MAX_VOICES; ++i)
        if (e->voice[i].state == PM_VOICE_SUSTAINED)
            release_voice(&e->voice[i]);
}

static void set_param(pm_engine_t *e, uint32_t param, uint32_t value)
{
    if (param >= PM_PARAM_COUNT)
        return;
    e->patch.value[param] = (uint8_t)(value > 127u ? 127u : value);
    recompute_values(e);
}

static void set_bend(pm_engine_t *e, uint32_t bend)
{
    if (bend > 16383u)
        bend = 16383u;
    e->bend = (uint16_t)bend;
    e->bend_ratio = pm_exp2f(((float)bend - 8192.0f) * (1.0f / 8192.0f) *
                             (float)e->patch.bend_range * (1.0f / 12.0f));
}

void pm_engine_control(pm_engine_t *e, uint32_t controller, uint32_t value)
{
    uint32_t i;
    if (!e)
        return;
    if (value > 127u)
        value = 127u;
    switch (controller) {
    case 1u:
        e->mod = (uint8_t)value;
        recompute_values(e);
        break;
    case 7u:
        e->volume = (uint8_t)value;
        e->dirty = 1u;
        break;
    case 11u:
        e->expression = (uint8_t)value;
        e->dirty = 1u;
        break;
    case 64u:
        set_sustain(e, value >= 64u);
        break;
    case 120u:                          /* all sound off */
        pm_engine_all_off(e);
        break;
    case 121u:                          /* reset all controllers */
        e->mod = 0u;
        e->pressure = 0u;
        e->expression = 127u;
        set_bend(e, 8192u);
        set_sustain(e, 0u);
        recompute_values(e);
        break;
    case 123u:                          /* all notes off: let go, as if every key were lifted */
        for (i = 0; i < PM_MAX_VOICES; ++i) {
            pm_engine_voice_t *vc = &e->voice[i];
            if (vc->bench)
                continue;
            if (vc->state == PM_VOICE_HELD || vc->state == PM_VOICE_SUSTAINED)
                release_voice(vc);
            else if (vc->state == PM_VOICE_STEALING)
                vc->pending_off = 1u;
        }
        break;
    default:
        /* the general purpose controllers are the four knobs: 16..19 the
         * primary page, 20..23 MORE */
        if (controller >= 16u && controller <= 23u) {
            const uint32_t param = e->patch.page[(controller - 16u) >> 2][(controller - 16u) & 3u];
            set_param(e, param, value);
            if (param < PM_PARAM_COUNT)
                e->midi_params |= 1u << param;
        }
        break;
    }
}

void pm_engine_midi(pm_engine_t *e, uint32_t status, uint32_t data1, uint32_t data2)
{
    if (!e)
        return;
    data1 &= 0x7Fu;
    data2 &= 0x7Fu;
    switch (status & 0xF0u) {
    case 0x80u:
        pm_engine_note_off(e, data1);
        break;
    case 0x90u:
        pm_engine_note_on(e, data1, data2);
        break;
    case 0xA0u:                         /* a key's own pressure: taken as the channel's */
        e->pressure = (uint8_t)data2;
        recompute_values(e);
        break;
    case 0xB0u:
        pm_engine_control(e, data1, data2);
        break;
    case 0xC0u:
        e->program_request = (uint8_t)(data1 + 1u);
        break;
    case 0xD0u:
        e->pressure = (uint8_t)data1;
        recompute_values(e);
        break;
    case 0xE0u:
        set_bend(e, data1 | data2 << 7);
        break;
    default:
        break;
    }
}

void pm_engine_set_patch(pm_engine_t *e, const pm_patch_t *patch)
{
    const pm_body_mode_t *modes;
    uint32_t count;
    if (!e || !patch)
        return;
    pm_engine_all_off(e);
    e->patch = *patch;
    (void)pm_patch_sanitize(&e->patch);
    modes = pm_body_modes(e->patch.body, &count);
    pm_body_configure(&e->body, modes, count);
    set_bend(e, e->bend);
    recompute_values(e);
}

/* -------------------------------------------------------------- queue --- */

int pm_engine_post(pm_engine_t *e, uint32_t type, uint32_t a, uint32_t b)
{
    uint32_t head;
    if (!e)
        return -1;
    head = e->queue_head;
    if (head - e->queue_tail >= PM_QUEUE_SIZE) {
        ++e->dropped;
        return -1;
    }
    e->queue[head & PM_QUEUE_MASK] = (type & 0xFFu) << 24 | (a & 0xFFFFu) << 8 | (b & 0xFFu);
    e->queue_head = head + 1u;          /* the message is whole before the render can see it */
    return 0;
}

int pm_engine_post_patch(pm_engine_t *e, const pm_patch_t *patch)
{
    if (!e || !patch)
        return -1;
    /* The render skips a hand-over it finds locked; the message that follows
     * the unlock brings it back to a whole patch. */
    e->patch_lock = 1u;
    PM_BARRIER();
    e->patch_pending = *patch;
    PM_BARRIER();
    e->patch_lock = 0u;
    return pm_engine_post(e, PM_EV_PATCH, 0u, 0u);
}

void pm_engine_shed(pm_engine_t *e)
{
    pm_engine_voice_t *vc;
    if (!e)
        return;
    vc = find_steal(e);
    if (vc)
        fade_voice(vc);
    if (e->polyphony > 2u)
        --e->polyphony;
    ++e->sheds;
}

static void handle(pm_engine_t *e, uint32_t message)
{
    const uint32_t type = message >> 24, a = (message >> 8) & 0xFFFFu, b = message & 0xFFu;
    switch (type) {
    case PM_EV_NOTE_ON:
        pm_engine_note_on(e, a, b);
        break;
    case PM_EV_NOTE_OFF:
        pm_engine_note_off(e, a);
        break;
    case PM_EV_PARAM:
        set_param(e, a, b);
        break;
    case PM_EV_CONTROL:
        pm_engine_control(e, a, b);
        break;
    case PM_EV_BEND:
        set_bend(e, a);
        break;
    case PM_EV_PRESSURE:
        e->pressure = (uint8_t)(b > 127u ? 127u : b);
        recompute_values(e);
        break;
    case PM_EV_PATCH:
        if (!e->patch_lock) {
            const uint32_t bench = e->bench;
            pm_engine_set_patch(e, &e->patch_pending);
            if (bench)
                bench_start(e, bench);
        }
        break;
    case PM_EV_ALL_OFF:
        pm_engine_all_off(e);
        e->bench = 0u;
        set_sustain(e, 0u);
        break;
    case PM_EV_POLYPHONY:
        e->polyphony = (uint8_t)(b < 1u ? 1u : b > PM_MAX_VOICES ? PM_MAX_VOICES : b);
        break;
    case PM_EV_BENCH:
        bench_start(e, b);
        break;
    case PM_EV_SHED:
        pm_engine_shed(e);
        break;
    case PM_EV_BEND_RANGE:
        e->patch.bend_range = (uint8_t)(b < 1u ? 1u : b > 24u ? 24u : b);
        set_bend(e, e->bend);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------- render --- */

/* Render frames of one voice into the scratch buffer; returns its peak. */
static float render_model(pm_engine_t *e, pm_engine_voice_t *vc, uint32_t frames)
{
    uint32_t i;
    float peak = 0.0f;
    for (i = 0; i < frames; ++i)
        e->scratch[i] = 0.0f;
    if (vc->engine == PM_ENGINE_BOW)
        pm_bowed_render(&vc->model.bowed, e->scratch, frames);
    else if (vc->engine == PM_ENGINE_PLUCK)
        pm_string_render(&vc->model.string, e->scratch, frames);
    for (i = 0; i < frames; ++i) {
        const float a = pm_absf(e->scratch[i]);
        if (!(a <= peak))               /* also true for a NaN, which then fails the bound below */
            peak = a;
    }
    return peak;
}

static void render_voice(pm_engine_t *e, pm_engine_voice_t *vc, float *mix, uint32_t frames)
{
    uint32_t offset = 0u, i, guard;
    float peak = 0.0f;
    for (guard = 0; guard < 3u && offset < frames && vc->state != PM_VOICE_IDLE; ++guard) {
        uint32_t segment = frames - offset;
        if (vc->state == PM_VOICE_STEALING && segment > vc->fade_left)
            segment = vc->fade_left;
        peak = segment ? render_model(e, vc, segment) : 0.0f;
        if (!(peak < PM_FAULT_PEAK)) {
            /* a voice outside its bounds (or not a number) is stopped and
             * rebuilt; nothing of it reaches the mix */
            stop_model(vc);
            vc->engine = PM_ENGINE_NONE;
            vc->state = PM_VOICE_IDLE;
            vc->bench = 0u;
            ++e->faults;
            return;
        }
        if (vc->state == PM_VOICE_STEALING) {
            float gain = (float)vc->fade_left * (1.0f / (float)PM_FADE_FRAMES);
            for (i = 0; i < segment; ++i) {
                mix[offset + i] += e->scratch[i] * gain;
                gain -= 1.0f / (float)PM_FADE_FRAMES;
            }
            vc->fade_left -= segment;
            if (vc->fade_left == 0u) {
                stop_model(vc);
                if (vc->pending) {
                    const uint32_t off = vc->pending_off;
                    start_voice(e, vc, vc->pending_note, vc->pending_velocity, vc->pending_string);
                    if (off && vc->state == PM_VOICE_HELD) {
                        if (e->sustain)
                            vc->state = PM_VOICE_SUSTAINED;
                        else
                            release_voice(vc);
                    }
                } else {
                    vc->state = PM_VOICE_IDLE;
                }
            }
        } else {
            for (i = 0; i < segment; ++i)
                mix[offset + i] += e->scratch[i];
        }
        offset += segment;
    }
    if (vc->state == PM_VOICE_IDLE || vc->state == PM_VOICE_STEALING || vc->bench)
        return;
    /* a voice that has rung out gives its place back */
    if (vc->engine == PM_ENGINE_BOW ?
        (!vc->model.bowed.bowing && vc->model.bowed.envelope <= 0.0f && peak < PM_QUIET_PEAK) :
        (!pm_string_exciting(&vc->model.string) && peak < PM_QUIET_PEAK)) {
        vc->quiet_blocks += frames;
        if (vc->quiet_blocks >= PM_QUIET_FRAMES) {
            stop_model(vc);
            vc->state = PM_VOICE_IDLE;
        }
    } else {
        vc->quiet_blocks = 0u;
    }
}

static void render_block(pm_engine_t *e, float *out, uint32_t frames)
{
    uint32_t i, drained, active = 0u;
    float ratio;

    for (drained = 0; drained < PM_QUEUE_SIZE && e->queue_tail != e->queue_head; ++drained) {
        const uint32_t message = e->queue[e->queue_tail & PM_QUEUE_MASK];
        e->queue_tail = e->queue_tail + 1u;
        handle(e, message);
    }
    if (e->dirty)
        push_values(e);

    /* pitch: the wheel for every voice, and for plucked strings a vibrato
     * (the bowed model has its own, at audio rate) */
    ratio = e->bend_ratio;
    if (e->patch.engine == PM_ENGINE_PLUCK && e->value[PM_P_VIBRATO]) {
        e->vibrato_phase += PM_TWO_PI * pm_patch_vibrato_hz(e->value) * (float)frames / PM_SAMPLE_RATE_F;
        if (e->vibrato_phase > PM_TWO_PI)
            e->vibrato_phase -= PM_TWO_PI;
        ratio *= pm_exp2f(pm_patch_vibrato_semitones(e->value) * pm_sinf(e->vibrato_phase) * (1.0f / 12.0f));
    }
    if (ratio != e->last_ratio) {
        e->last_ratio = ratio;
        for (i = 0; i < PM_MAX_VOICES; ++i) {
            pm_engine_voice_t *vc = &e->voice[i];
            if (vc->state == PM_VOICE_IDLE)
                continue;
            if (vc->engine == PM_ENGINE_BOW)
                pm_bowed_set_pitch_ratio(&vc->model.bowed, e->bend_ratio);
            else if (vc->engine == PM_ENGINE_PLUCK)
                pm_string_set_pitch_ratio(&vc->model.string, ratio);
        }
    }

    for (i = 0; i < frames; ++i)
        out[i] = 0.0f;
    for (i = 0; i < PM_MAX_VOICES; ++i) {
        if (e->voice[i].state == PM_VOICE_IDLE)
            continue;
        render_voice(e, &e->voice[i], out, frames);
        if (e->voice[i].state != PM_VOICE_IDLE)
            ++active;
    }
    for (i = 0; i < frames; ++i)
        out[i] *= PM_MIX_HEADROOM;
    pm_body_process(&e->body, out, frames);
    pm_output_process(&e->output, out, frames);

    e->active_voices = (uint8_t)active;
    e->polyphony_now = e->polyphony;
    e->limiter_q8 = (uint8_t)(e->output.reduction * 255.0f);
}

void pm_engine_render(pm_engine_t *e, float *out, uint32_t frames)
{
    if (!e || !out)
        return;
    while (frames) {
        const uint32_t n = frames > PM_ENGINE_BLOCK ? PM_ENGINE_BLOCK : frames;
        render_block(e, out, n);
        out += n;
        frames -= n;
    }
}

void pm_engine_init(pm_engine_t *e, uint32_t seed)
{
    uint32_t i;
    if (!e)
        return;
    for (i = 0; i < PM_MAX_VOICES; ++i) {
        pm_engine_voice_t *vc = &e->voice[i];
        vc->seed = (seed ? seed : 0x51A1u) + i * 7919u;
        vc->engine = PM_ENGINE_NONE;
        vc->state = PM_VOICE_IDLE;
        vc->age = 0u;
        vc->fade_left = 0u;
        vc->quiet_blocks = 0u;
        vc->base_note = 0.0f;
        vc->note = 0u;
        vc->string = 0u;
        vc->pending = 0u;
        vc->pending_note = 0u;
        vc->pending_velocity = 0u;
        vc->pending_string = 0u;
        vc->pending_off = 0u;
        vc->bench = 0u;
    }
    pm_body_init(&e->body);
    pm_output_init(&e->output);
    for (i = 0; i < PM_ENGINE_BLOCK; ++i)
        e->scratch[i] = 0.0f;
    for (i = 0; i < PM_QUEUE_SIZE; ++i)
        e->queue[i] = 0u;
    e->queue_head = 0u;
    e->queue_tail = 0u;
    e->patch_lock = 0u;
    e->age_counter = 0u;
    e->bend = 8192u;
    e->bend_ratio = 1.0f;
    e->vibrato_phase = 0.0f;
    e->last_ratio = 1.0f;
    e->mod = 0u;
    e->pressure = 0u;
    e->volume = 100u;
    e->expression = 127u;
    e->sustain = 0u;
    e->polyphony = PM_DEFAULT_POLYPHONY;
    e->bench = 0u;
    e->active_voices = 0u;
    e->polyphony_now = PM_DEFAULT_POLYPHONY;
    e->limiter_q8 = 255u;
    e->program_request = 0u;
    e->steals = 0u;
    e->sheds = 0u;
    e->faults = 0u;
    e->dropped = 0u;
    e->midi_params = 0u;
    e->patch_pending = *pm_factory_patch(0u);
    pm_engine_set_patch(e, pm_factory_patch(0u));
    push_values(e);
    e->output.level = e->output.level_target;
    e->body.wet = e->body.wet_target;
}

uint32_t pm_engine_bytes(void)
{
    return (uint32_t)sizeof(pm_engine_t);
}

uint32_t pm_engine_voice_slot_bytes(void)
{
    return (uint32_t)sizeof(pm_engine_voice_t);
}
