# fm1-violioliolol — Product Requirements Document

**Document:** Product Requirements Document  
**Version:** 1.1  
**Status:** Living specification  
**Target hardware:** M-VAVE FM-1  
**Firmware foundation:** `fm1-base`, pinned to the same revision used by CHOPPAH  
**Primary implementation language:** C  
**DSP sample rate:** 44.1 kHz  
**Audio block:** 256 frames in the pinned platform baseline  
**Last status review:** 2026-11-12

> This is the canonical product and engineering specification. Requirement status is tracked per ID below. A requirement is not complete merely because it is described, planned, or compiles on a desktop. Use the evidence column to link tests, commits, benchmark records, or hardware validation.

---

## 1. Product vision

**fm1-violioliolol** is an experimental real-time physical-modelling instrument for the M-VAVE FM-1. It should turn the device into a compact, expressive string instrument workstation capable of synthesising plucked, struck and bowed string families.

The defining constraint is that the instrument generates its sound algorithmically. It must not depend on recorded instrument samples, sample playback, or large sample libraries.

The first engineering objective is not a large preset catalogue. It is a small number of convincing, controllable physical models that run reliably inside the FM-1 audio budget.

### Product principles

- **Physical modelling first:** sound comes from simulated strings, excitation, damping and resonances.
- **Expressive control:** performance controls should change physical behaviour, not merely select static effects.
- **Real-time safety:** audio rendering must be deterministic, bounded and free of blocking operations.
- **Hardware honesty:** desktop results are regression signals, never claims about FM-1 performance.
- **Recoverability:** application development must not compromise the bootloader or recovery path.
- **Incremental fidelity:** establish stable fundamentals and measurable polyphony before adding complexity.

## 2. Goals and non-goals

### Goals

- Synthesise plucked and bowed strings in real time on FM-1 hardware.
- Provide instrument families covering bass, guitar, bowed strings and selected folk/plucked instruments.
- Expose useful physical parameters through the FM-1 controls and MIDI.
- Support multi-voice playing with predictable voice allocation.
- Keep DSP platform-independent so the core models can be tested natively.
- Measure CPU load, xruns and memory on real hardware before setting polyphony guarantees.
- Reuse the CHOPPAH firmware foundation and build conventions.

### Non-goals for the initial milestones

- A sample player, sampler or sample-based hybrid instrument.
- A full workstation/sequencer or DAW replacement.
- A claim of acoustic realism for every instrument family from the first release.
- A guaranteed voice count before on-device profiling.
- Bootloader replacement, bootloader modification or recovery partition changes.
- Persistent user-data writes from audio, timer, MIDI or other interrupt-sensitive callbacks.

## 3. Hardware and platform baseline

| Item | Current baseline | Status |
|---|---|---|
| Target | M-VAVE FM-1 | `[verified]` |
| MCU/platform | JieLi AC791N / WL82, pi32v2 | `[verified-reference]` |
| CPU features | Single-precision FPU; second core APIs exist | `[verified-reference]` |
| Audio rate | 44,100 Hz | `[verified]` |
| Audio block | 256 frames in pinned `fm1-base` revision | `[verified]` |
| Block duration | Approximately 5.805 ms at 44.1 kHz | `[verified]` |
| Audio format | Stereo output, 24-bit render path | `[verified-reference]` |
| Display | 240 × 240 | `[verified-reference]` |
| Controls | 27 keys, 14 buttons, 7 encoders and master pot | `[verified-reference]` |
| Memory | Platform pool and static data/BSS are shared resources | `[verified-reference]` |
| App flash slot | Approximately 581 KB in inspected reference documentation | `[reference-only]` |

The public `fm1_open` SDK header inspected during the initial audit declares a 128-frame block. That is not the contract used by this project. The pinned `fm1-base` source and the app's explicit `APP_BLOCK 256u` setting define the current project baseline. Re-check this whenever the platform submodule is updated.

### Source pins

- CHOPPAH reference: `profikid/fm1-break-choppah@c31629f8d1e92bbd379e9d0fc74c2017e8ce06fc`
- `fm1-base`: `charlesvestal/fm1-base@317cb86cc60d6286fa80a02743e94e6a60ff842d`
- Public SDK comparison point: `m-vave/fm1_open@4c7496daac04a95ff10d1afdcc8f9ead2dbabc41`

These pins are documented in [engineering-baseline.md](engineering-baseline.md). The public SDK is a comparison reference, not a replacement for the pinned `fm1-base` platform.

## 4. Sound-generation architecture

### 4.1 Core signal path

The intended signal path is:

1. MIDI or local performance input creates a note/gesture event.
2. Voice allocator assigns a preallocated voice.
3. Instrument model converts note and expression data into physical parameters.
4. Excitation model starts or continuously drives the string.
5. String model runs its delay-line, waveguide or modal state.
6. Body and pickup/resonance stages colour the string output.
7. Voice output is mixed with headroom and converted to the platform output format.

The audio callback must only process already prepared state. Setup, preset loading and persistent storage operations belong in non-real-time control context.

### 4.2 Primary synthesis techniques

- **Digital waveguides:** primary framework for travelling-wave string simulation.
- **Karplus–Strong:** initial plucked-string implementation and baseline for plucked instruments.
- **Fractional delay:** tune delay-line period between integer samples.
- **Loss filtering:** model frequency-dependent energy loss and decay.
- **Dispersion/stiffness:** add inharmonicity and instrument-dependent string behaviour.
- **Nonlinear bow friction:** model stick-slip interaction for sustained bowed excitation.
- **Modal synthesis:** model body resonances and selected coupled resonances.
- **Pickup modelling:** approximate pickup position and response for electric instruments.

The implementation may use simpler approximations where required by the hardware budget, but each simplification must be documented and measured.

## 5. Instrument families and requirements

Status values are maintained per requirement. See [Status tracking](#12-status-tracking-and-update-process).

| ID | Requirement | Acceptance intent | Status | Evidence / notes |
|---|---|---|---|---|
| INS-001 | Electric and acoustic bass family | Finger, pick, slap, fretless and upright-style configurations | `[implemented]` | FINGER BASS, PICK BASS, SLAP BASS, FRETLESS, UPRIGHT in `dsp/instrument.c`; `test_instrument` checks they exist, `test_engine` plays each in bounds. Hardware listening pending |
| INS-002 | Guitar family | Classical/nylon, steel-string, clean electric, distorted electric and jazz-style configurations | `[implemented]` | NYLON, STEEL, CLEAN ELEC, DRIVE ELEC, JAZZ; same tests. Hardware listening pending |
| INS-003 | Bowed-string family | Violin, viola, cello and double bass configurations | `[implemented]` | VIOLIN, VIOLA, CELLO, CONTRABASS on the bowed engine; same tests. Hardware listening pending |
| INS-004 | Other plucked/folk family | Harp, mandolin, banjo, koto and zither configurations | `[implemented]` | HARP, MANDOLIN (a two-string course), BANJO, KOTO, ZITHER (hammered); same tests. Hardware listening pending |
| INS-005 | Parameter-driven presets | Presets store physical-model parameters, not recorded audio | `[implemented]` | 52-byte `pm_patch_t`; a 16-slot bank with CRC-32 in the store ([presets.md](presets.md)); `test_instrument` (2000 random patches, bit flips), `tests/scenarios/edit_save.txt` (save, reboot, reload) |
| INS-006 | Instrument-specific response | Each family should have a recognisable excitation, string and resonance configuration | `[implemented]` | Per-instrument excitation style, string parameters, one of 13 bodies and pickup; mapping tested in `test_instrument`. "Recognisable" needs listening tests on hardware |

## 6. Physical model requirements

| ID | Requirement | Acceptance intent | Status | Evidence / notes |
|---|---|---|---|---|
| DSP-001 | Plucked-string engine | Stable mono string voice with deterministic excitation and bounded state | `[implemented]` | `dsp/string_model.c`; `test_string_model` (prototype contract, 400 random parameter sets incl. NaN/Inf, 61 s stability) |
| DSP-002 | Fractional delay | Tune fundamental frequency between integer delay lengths | `[implemented]` | Third-order Lagrange fractional delay; loop filters compensated at the fundamental. Worst error 0.19 cents, 27.5 Hz to 1.76 kHz (`test_string_model` tuning) |
| DSP-003 | Damping/loss filter | Control decay without unstable feedback | `[implemented]` | One-zero loss filter with brightness; loop gain from a T60. A 1 s T60 loses 30.0 dB in 0.5 s; level never grows (`test_string_model` damping) |
| DSP-004 | Deterministic excitation | Reproducible noise-burst excitation from a seed | `[implemented]` | Seeded xorshift; five styles (noise, finger, pick, slap, hammer), each bit-identical per seed (`test_string_model` excitation) |
| DSP-005 | Velocity response | Velocity controls output amplitude linearly | `[implemented]` | Regression test compares 0.25 and 0.50 velocity sample by sample |
| DSP-006 | Input validation | Reject invalid frequency, NaN decay and non-finite/out-of-range velocity | `[implemented]` | Native regression tests; the bowed model validates the same way (`test_bowed_model` contract) |
| DSP-007 | String stiffness/dispersion | Add controllable inharmonicity for stiff strings | `[implemented]` | Three-stage allpass dispersion; 8th partial +43 cents at full stiffness, fundamental held at 0.00 cents (`test_string_model` stiffness) |
| DSP-008 | Pluck position | Excitation spectrum changes with pluck position | `[implemented]` | Pluck-position comb on the excitation; plucking the middle removes the even harmonics by 57 dB (`test_string_model` positions) |
| DSP-009 | Body resonator | Add instrument body modes and controllable resonances | `[implemented]` | `dsp/body_model.c`: up to six two-pole modes, one body shared by all voices; `test_body_output` (frequency, decay, bounds, invalid modes) |
| DSP-010 | Pickup model | Add pickup position/response for electric instruments | `[implemented]` | Pickup tap comb with blend, plus output tone and drive; a middle pickup drops the even harmonics 51 dB (`test_string_model` positions, `test_body_output`) |
| DSP-011 | Nonlinear bowed-string engine | Sustain sound through a stable bow/string stick-slip interaction | `[implemented]` | `dsp/bowed_model.c`: two-line waveguide, bounded friction curve. Steady level 41 Hz to 2.6 kHz at three pressures, worst 6.4 cents (`test_bowed_model`) |
| DSP-012 | Bow pressure | Bow force changes friction and excitation behaviour | `[implemented]` | Friction-curve slope; 5.2 dB change in the upper harmonics across the range (`test_bowed_model` bow parameters) |
| DSP-013 | Bow speed | Bow velocity changes energy input and tone | `[implemented]` | Bow velocity; 3.3x level from slowest to fastest; note velocity maps to speed (same test) |
| DSP-014 | Bow position | Contact point changes harmonic balance and stability | `[implemented]` | Split point of the two lines; near the bridge has 1.5x the upper harmonics of over the fingerboard (same test) |
| DSP-015 | Vibrato | Controlled pitch modulation suitable for bowed instruments | `[implemented]` | Audio-rate vibrato with delayed onset on bowed; block-rate vibrato on plucked. 0.5 semitone depth swings 95 cents (`test_bowed_model` vibrato) |
| DSP-016 | Voice-level output safety | Prevent runaway amplitude and clamp/limit at the output boundary | `[implemented]` | Hard state bounds in the bowed model; per-voice fault catch stops NaN or runaway voices (`test_engine` faults); output limiter keeps every sample within 0.85 (`test_body_output`) |
| DSP-017 | Polyphonic mixing | Mix multiple model voices without blocking or dynamic allocation | `[implemented]` | `dsp/engine.c` mixes 12 preallocated voices at fixed headroom; 4, 8 and 12 voices of every instrument at full velocity stay inside full scale (`test_engine` polyphony) |

### 6.1 Plucked-string model

`dsp/string_model.c`, an extended Karplus–Strong waveguide:

- 44.1 kHz, mono voice output, single precision only (no libm, no doubles: `dsp/pm_math.h`).
- Fixed 2048-sample circular delay line; third-order Lagrange fractional delay, so tuning and tone do not depend on where a note falls between integer lengths.
- One-zero loss filter with a brightness control; loop gain set from a T60 decay time, with a separate release T60.
- Three-stage allpass dispersion for stiffness; its delay at the fundamental comes off the delay line, so the note stays in tune.
- Excitation fed in over the first period: noise, finger, pick (with a release click), slap (with a short fret-rattle limit) and hammer (a smooth contact pulse), through a pluck-position comb.
- Pickup-position comb on the output, with a blend; an amplitude limit in the loop for fret and fingerboard buzz.
- Pitch bend and vibrato glide the delay length: no reset, no click.
- Accepted range 21.6 Hz to 22.05 kHz; the string itself reaches about 12.6 kHz.

The body, tone, drive and limiter stages are shared by all voices (`dsp/body_model.c`).

### 6.2 Bowed-string model

`dsp/bowed_model.c`, after J. O. Smith's bowed-string waveguide: two delay lines (bow to nut, bow to bridge) meet at the bow, where a bounded friction curve, (|x| + 0.75)^-4 capped at 1, passes part of the bow-string velocity difference to the string. That stick-slip is what sustains the tone; there is no oscillator with an envelope in it.

- Bow pressure sets the friction curve's slope; bow speed (and note velocity, and CC11) the bow's velocity, kept where the string still follows it once a cycle; bow position the split between the two lines.
- The bridge reflection is a one-pole lowpass whose delay at the note's frequency comes off the string's length, so pitch holds from 41 Hz to 2.6 kHz.
- Output level is evened out across pressure and pitch, so pressure changes the tone more than the loudness.
- Every sample of string state is hard-bounded to ±4: the model cannot run away, whatever its parameters.
- Attack and release envelopes on the bow's speed; vibrato at audio rate with a delayed onset; legato pitch changes without a reset.

Its CPU cost on the FM-1 is the main open question (section 14).

## 7. Performance, expression and MIDI

| ID | Requirement | Acceptance intent | Status | Evidence / notes |
|---|---|---|---|---|
| MIDI-001 | MIDI Note On/Off | Play and release allocated voices | `[implemented]` | Note on/off on any channel, velocity 0 as note-off; `test_engine` midi, `tests/scenarios/midi.txt` |
| MIDI-002 | Velocity | Map note velocity to excitation/output strength | `[implemented]` | Velocity scales excitation and its brightness; bowed: bow speed. The keys give velocity through `lib/touch.h` |
| MIDI-003 | Pitch bend | Continuous pitch adjustment without voice reset | `[implemented]` | Delay glide on both engines, no voice reset; range 1 to 24 semitones per patch. +-2 semitones measured within 12 cents (`test_engine` midi) |
| MIDI-004 | Modulation wheel CC1 | Map to a useful instrument expression parameter | `[implemented]` | Per-patch destination: vibrato on bowed, damping or pluck position on plucked, pickup blend on electric (`test_engine` midi) |
| MIDI-005 | Volume CC7 | Channel or instrument volume control | `[implemented]` | CC7 squared to gain, ramped per block; `test_engine` midi, `tests/scenarios/midi.txt` |
| MIDI-006 | Expression CC11 | Performance expression control | `[implemented]` | CC11: level on plucked; bow speed and level on bowed; same tests |
| MIDI-007 | Sustain CC64 | Sustain/release behaviour for allocated voices | `[implemented]` | Pedal holds released voices; lifting it releases them and not keys still down. PLAY latches it on the panel |
| MIDI-008 | Channel pressure | Use after confirming platform event support and a sensible mapping | `[implemented]` | Channel and poly pressure to a per-patch destination; the keys send their own pressure too (`lib/touch.h`) |
| MIDI-009 | Voice allocator | Handle note priority, voice stealing and release state deterministically | `[implemented]` | Free voices in order, then released before pedal-held before held, oldest first; the same performance gives identical samples (`test_engine` allocator) |
| MIDI-010 | Multi-voice operation | Support 4 voices as a minimum goal, 8 as target and 12 as stretch goal, subject to measurement | `[implemented]` | 1 to 12 voices on the PERF page, default 8. Host only: the device limit awaits measurement (VOX-005..007) |

MIDI mappings should be configurable by instrument where practical. The same controller may control bow pressure on violin and pluck position or pickup blend on guitar, but mappings must remain predictable and documented.

## 8. FM-1 controls and user interface

The final UI should be instrument-oriented, with four context-sensitive performance parameters exposed prominently. The exact physical control assignment must follow the actual FM-1 control map and avoid conflicting with platform-reserved controls.

### Example parameter pages

| Instrument | Primary parameters |
|---|---|
| Violin / viola / cello | Bow pressure, bow speed, vibrato, body resonance |
| Acoustic guitar | Pluck position, pluck intensity, damping, body resonance |
| Electric guitar | Pickup position/response, pluck intensity, damping, drive/colour |
| Bass | Excitation style, pluck position, damping, pickup blend |

| ID | Requirement | Acceptance intent | Status | Evidence / notes |
|---|---|---|---|---|
| UI-001 | Instrument selection | Select instrument family and model | `[implemented]` | ALGORITHM picks the family, PRESET the instrument in it, SELECT walks all of them (`tests/scenarios/play.txt`) |
| UI-002 | Four context-sensitive controls | Four primary parameters are visible and immediately editable | `[implemented]` | K1 to K4 on two pages per instrument (EDIT switches), and CC16 to CC23 over MIDI ([manual](manual.md)) |
| UI-003 | Parameter feedback | Display names and values clearly on the 240 × 240 display | `[implemented]` | Name, value and bar per row; changes shown and sent to a screen reader; screenshots in `docs/img` |
| UI-004 | Preset browser | Browse and load parameter presets | `[implemented]` | A user family on ALGORITHM; SAVE picks a slot; program change selects any instrument (`tests/scenarios/edit_save.txt`) |
| UI-005 | Performance status | Provide useful voice/activity and audio status without clutter | `[implemented]` | Voice boxes, CPU, xruns, octave, and sustain, MIDI, limiter and shed indicators |
| UI-006 | Safe parameter updates | UI task publishes parameter changes safely; audio task owns live DSP state | `[implemented]` | The UI only posts to a bounded single-producer queue drained at block start; patches handed over with a skip-if-locked copy (`test_engine` queue) |

## 9. Voice management and polyphony

- Voice state must be preallocated or allocated during setup, never from the audio callback.
- Voice allocation must be deterministic and bounded.
- Note stealing must avoid clicks where practical, using short release/fade transitions.
- A voice must not be reinitialised concurrently by UI and audio contexts.
- Polyphony targets are engineering goals, not release guarantees.

| ID | Requirement | Acceptance intent | Status | Evidence / notes |
|---|---|---|---|---|
| VOX-001 | Static/preallocated voice state | No heap allocation during rendering | `[implemented]` | The engine is one static object (151,160 bytes) in the platform pool; no allocation anywhere in `dsp/` |
| VOX-002 | Voice count benchmark | Exercise 1, 2, 4 and 8 simultaneous voices | `[implemented]` | BENCH on the PERF page holds 1, 2, 4, 8 or 12 voices of the current instrument; `bench_engine` on the host |
| VOX-003 | Voice allocator | Allocate, release and steal voices predictably | `[implemented]` | See MIDI-009 |
| VOX-004 | Click-safe transitions | Avoid discontinuities on note-off, stealing and parameter changes | `[implemented]` | Stolen voices fade over 96 samples before restarting; all-off and instrument changes fade too; steps measured below the music's own (`test_engine` click safety) |
| VOX-005 | Four-voice minimum goal | Sustain four useful voices with stable audio on target hardware | `[implemented]` | Host: 4 voices pass; firmware builds. **Hardware measurement pending** ([benchmark-procedure.md](benchmark-procedure.md)) |
| VOX-006 | Eight-voice target | Sustain eight useful voices with stable audio on target hardware | `[implemented]` | Host: 8 voices pass. **Hardware measurement pending** |
| VOX-007 | Twelve-voice stretch | Investigate twelve voices if measured budget permits | `[implemented]` | Host: 12 voices pass (about 160 ns a frame on a desktop). **Hardware measurement pending** |

## 10. Real-time, memory and firmware constraints

These constraints apply to every DSP and application requirement.

| ID | Requirement | Acceptance intent | Status | Evidence / notes |
|---|---|---|---|---|
| RT-001 | No allocation in audio callback | No malloc/free or equivalent in render path | `[implemented]` | `dsp/` includes only `<stdint.h>` (enforced by `tests/engine/run.sh`); no allocation anywhere |
| RT-002 | No blocking in audio callback | No locks, waits, filesystem, logging or synchronous I/O | `[implemented]` | The render makes no platform call but `plat_midi_in`; flash writes happen in the main loop only |
| RT-003 | Bounded processing | Work per frame/block has a known upper bound | `[implemented]` | Every loop has a fixed bound (voices, frames, queue depth, guard counters). Worst-case device timing still to be measured |
| RT-004 | Preallocated memory | Voice and scratch buffers allocated before rendering | `[implemented]` | Engine, voices and scratch are static, initialised in `app_init` before audio starts |
| RT-005 | Flash safety | Persistent flash operations only from permitted control context | `[implemented]` | `plat_store_save` only from `app_frame`, after the voices fade (60 ms at most); a failed write is reported (`tests/scenarios/edit_save.txt`) |
| RT-006 | Bootloader preservation | Never overwrite or modify bootloader/recovery as part of normal app development | `[implemented-policy]` | Explicit project constraint |
| RT-007 | Audio diagnostics | Capture CPU load, peak timing and xruns on hardware | `[implemented]` | Status line shows CPU and xruns live; `app_overload` sheds one voice at a time when the render passes 90 % of its block |
| RT-008 | Memory accounting | Track per-voice and total app memory, including shared pool usage | `[implemented]` | Firmware build: image 119,104 B; RAM .data+.bss 18,900 of 98,304 B; pool 185,984 of 344,064 B; engine 151,160 B; voice slot 12,460 B |
| RT-009 | Second-core offload | Evaluate core 2 only if profiling shows a useful, safe partition | `[deferred]` | No device profile yet to justify a split. Revisit if VOX-006 fails on one core |
| RT-010 | Output headroom | Prevent clipping when voices are mixed | `[implemented]` | 0.3 mix headroom, a bounded drive stage and a limiter at 0.85; four voices at moderate velocity never reach the limiter (`test_engine` polyphony) |

### Real-time callback rules

The audio callback must not:

- Allocate or free memory.
- Access the filesystem or persistent flash.
- Wait on locks, semaphores, queues or other tasks.
- Log, print or perform blocking diagnostics.
- Perform unbounded work based on input data.
- Reconfigure or initialise a live voice from another task.

Parameter communication should use bounded, concurrency-safe handoff. Any cross-core processing must define ownership, buffer lifetime, synchronisation and worst-case latency before implementation.

## 11. Testing, profiling and acceptance

### Host tests

`sh build.sh test` runs five native suites (string, bowed, body and output, instrument, engine); `sh build.sh --host && sh tests/scenarios/run.sh` drives the whole firmware in fm1-base's simulator. Together they cover:
- Output is finite and non-silent after note-on.
- Fundamental period is within a defined tolerance.
- Invalid inputs are rejected.
- Velocity scaling behaves as specified.
- Long renders remain stable.
- Voice reset/retrigger behaviour is deterministic.
- Parameter extremes do not produce NaN/Inf or runaway state.

### Hardware tests

The PERF page's BENCH row holds 1, 2, 4, 8 or 12 voices of the current instrument, so both engines can be measured. Follow [benchmark-procedure.md](benchmark-procedure.md).

For every device measurement, record:

- Device/build identity and firmware commit.
- Voice count and instrument/model configuration.
- Sample rate and block size.
- CPU percentage and clock reading.
- Peak timing ticks if exposed by the platform.
- Xrun count before and after the run.
- Memory footprint and pool reservation.
- Listening notes, glitches, pitch stability and thermal/long-run observations.

### Release acceptance principles

A model is not considered hardware-ready until:
- Native correctness tests pass.
- Target firmware builds with the pinned platform.
- The model runs on a physical FM-1.
- Audio remains stable for a defined soak period.
- CPU and memory budgets are documented at the intended voice count.
- Relevant controls and MIDI mappings are tested.
- Recovery/bootloader safety has been preserved.

## 12. Status tracking and update process

### Status vocabulary

Use exactly one status for each requirement row:

- `[not-started]` — requirement is accepted but work has not begun.
- `[planned]` — design or implementation is scheduled, but no qualifying implementation exists.
- `[in-progress]` — implementation or validation has started but acceptance criteria are incomplete.
- `[implemented]` — code or documentation exists, but required verification may still be incomplete.
- `[verified]` — acceptance criteria passed with recorded evidence on the required target.
- `[blocked]` — progress is blocked by a dependency, missing hardware/tooling or unresolved decision.
- `[deferred]` — intentionally moved out of the current milestone.
- `[not-applicable]` — requirement was reviewed and does not apply; explain why.

**Important:** `implemented` is not the same as `verified`. A host test does not verify FM-1 hardware behaviour. A successful target build does not verify sound quality or polyphony.

### Updating a requirement

When changing a status:
1. Update the status cell for the requirement ID.
2. Add evidence: test name, test result, commit SHA, hardware build, measurement record or decision.
3. If blocked, state the blocker and the condition needed to unblock it.
4. If acceptance criteria change, update the requirement and this document's version/history.
5. Do not mark a requirement verified without evidence that matches its acceptance intent.

### Suggested review cadence

Review this table during each weekly development review. Prioritise requirements by dependency: platform correctness, one reliable plucked voice, benchmark, one bowed voice, then instrument breadth and UI.

## 13. Roadmap and milestone gates

| Milestone | Scope | Exit criteria | Status |
|---|---|---|---|
| M0 — Platform baseline | Pin CHOPPAH-compatible `fm1-base`, reconcile audio contract, preserve recovery path | Reproducible host build and documented platform pins | `[implemented]` |
| M1 — Plucked-string core | Karplus–Strong voice, fractional delay, damping and native tests | Correct pitch, stable output, deterministic tests | `[implemented]` |
| M2 — DSP benchmark | 1/2/4/8 voice host and device benchmark | Real FM-1 measurements recorded; safe operating point identified | `[in-progress]` |
| M3 — Bowed-string prototype | One nonlinear bowed voice | Stable sustained tone, controllable bow parameters, native tests and measured device cost | `[in-progress]` |
| M4 — Resonance and instrument identity | Modal body, dispersion, pluck position and pickup response | At least one convincing acoustic and one electric configuration | `[in-progress]` |
| M5 — Performance system | MIDI mapping, allocator, sustain, pitch bend and expression | Repeatable polyphonic performance with safe transitions | `[implemented]` |
| M6 — Instrument UI and presets | Instrument pages, four primary controls and parameter presets | Usable FM-1 UI and documented preset format | `[implemented]` |
| M7 — Optimisation and release | Memory/CPU optimisation and hardware validation | Release acceptance criteria met on target device | `[in-progress]` |

M1, M5 and M6 are implemented and tested on the host. M3 and M4 are built and tested on the host but wait on measured device cost (M3) and a listening pass (M4). The firmware builds and passes fm1-base's checks with the pinned toolchain. Nothing in this document is `[verified]` on hardware yet.

### Current next actions

1. Install v0.1 on an FM-1 and run the benchmark at 1, 2, 4, 8 and 12 voices on NYLON and on CELLO; record CPU, xruns and listening notes as [benchmark-procedure.md](benchmark-procedure.md) describes.
2. Set the default polyphony (now 8) from those numbers; move VOX-005..007 to `[verified]` or `[blocked]`.
3. A listening pass on every instrument; tune the bodies and patch values by ear (INS-006).
4. If bowed voices cost too much: move the render's hot loops into RAM code, or revisit RT-009.

## 14. Risks and open engineering questions

| Risk / question | Why it matters | Required response |
|---|---|---|
| Bow friction CPU cost | Nonlinear models may exceed the audio block budget | Start with one voice, profile worst-case processing, then scale |
| Shared memory pool | Platform pool is shared; not all documented capacity is available to this app | Measure actual linker/map and runtime allocations |
| 128 vs 256 frame discrepancy | Public SDK and pinned fm1-base differ | Preserve pinned platform contract; re-audit on submodule updates |
| Second-core use | Core APIs exist but data transfer and latency may erase gains | Do not offload until profiling justifies a concrete design |
| Polyphony expectations | Voice count depends on model complexity and other platform work | Treat 4/8/12 as goals until device evidence exists |
| Physical realism | Simple delay-line models may sound generic | Add dispersion, excitation, body and pickup stages incrementally |
| UI/control mapping | Physical controls have existing platform roles | Audit hardware mapping before finalising assignments |

## 15. Glossary

- **Digital waveguide:** a model of travelling waves along a string using delay lines and scattering/filter operations.
- **Karplus–Strong:** a plucked-string algorithm using a short excitation burst in a feedback delay loop.
- **Fractional delay:** a delay whose length is not restricted to an integer number of samples.
- **Dispersion:** frequency-dependent propagation that creates inharmonic partials, common in stiff strings.
- **Stick-slip:** alternating sticking and sliding friction at the bow/string contact point.
- **Modal synthesis:** synthesis using resonant modes with defined frequencies, damping and amplitudes.
- **Xrun:** an audio processing deadline miss or buffer underrun/overrun reported by the platform.
- **Physical-model preset:** a stored collection of synthesis parameters, not a recorded sound.

## 16. Change history

| Version | Date | Change |
|---|---|---|
| 1.1 | 2026-11-12 | Every INS, DSP, MIDI, UI and VOX row implemented with host evidence; RT-009 deferred; models described as built; hardware verification still pending. |
| 1.0 | 2026-11-11 | Consolidated product vision, instrument scope, DSP, MIDI, UI, real-time constraints, acceptance criteria, roadmap and per-requirement status tracking. |
