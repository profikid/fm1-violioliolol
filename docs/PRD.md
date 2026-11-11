# fm1-violioliolol — Product Requirements Document

**Document:** Product Requirements Document  
**Version:** 1.0  
**Status:** Living specification  
**Target hardware:** M-VAVE FM-1  
**Firmware foundation:** `fm1-base`, pinned to the same revision used by CHOPPAH  
**Primary implementation language:** C  
**DSP sample rate:** 44.1 kHz  
**Audio block:** 256 frames in the pinned platform baseline  
**Last status review:** 2026-11-11

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
| INS-001 | Electric and acoustic bass family | Finger, pick, slap, fretless and upright-style configurations | `[planned]` | No instrument presets yet |
| INS-002 | Guitar family | Classical/nylon, steel-string, clean electric, distorted electric and jazz-style configurations | `[planned]` | No guitar model yet |
| INS-003 | Bowed-string family | Violin, viola, cello and double bass configurations | `[planned]` | Bowed model not implemented |
| INS-004 | Other plucked/folk family | Harp, mandolin, banjo, koto and zither configurations | `[planned]` | Later family; depends on plucked engine |
| INS-005 | Parameter-driven presets | Presets store physical-model parameters, not recorded audio | `[planned]` | Preset system not implemented |
| INS-006 | Instrument-specific response | Each family should have a recognisable excitation, string and resonance configuration | `[planned]` | Requires listening tests and reference criteria |

## 6. Physical model requirements

| ID | Requirement | Acceptance intent | Status | Evidence / notes |
|---|---|---|---|---|
| DSP-001 | Plucked-string engine | Stable mono string voice with deterministic excitation and bounded state | `[implemented]` | `dsp/string_model.c`; native tests pass |
| DSP-002 | Fractional delay | Tune fundamental frequency between integer delay lengths | `[implemented]` | Linear interpolation in current prototype |
| DSP-003 | Damping/loss filter | Control decay without unstable feedback | `[implemented]` | Two-point averaging loss filter |
| DSP-004 | Deterministic excitation | Reproducible noise-burst excitation from a seed | `[implemented]` | Native model tests |
| DSP-005 | Velocity response | Velocity controls output amplitude linearly | `[implemented]` | Regression test compares 0.25 and 0.50 velocity |
| DSP-006 | Input validation | Reject invalid frequency, NaN decay and non-finite/out-of-range velocity | `[implemented]` | Native regression tests |
| DSP-007 | String stiffness/dispersion | Add controllable inharmonicity for stiff strings | `[planned]` | Not in current model |
| DSP-008 | Pluck position | Excitation spectrum changes with pluck position | `[planned]` | Comb/notch excitation not implemented |
| DSP-009 | Body resonator | Add instrument body modes and controllable resonances | `[planned]` | Modal engine not implemented |
| DSP-010 | Pickup model | Add pickup position/response for electric instruments | `[planned]` | Not implemented |
| DSP-011 | Nonlinear bowed-string engine | Sustain sound through a stable bow/string stick-slip interaction | `[planned]` | Next major synthesis milestone |
| DSP-012 | Bow pressure | Bow force changes friction and excitation behaviour | `[planned]` | Bow model dependency |
| DSP-013 | Bow speed | Bow velocity changes energy input and tone | `[planned]` | Bow model dependency |
| DSP-014 | Bow position | Contact point changes harmonic balance and stability | `[planned]` | Bow model dependency |
| DSP-015 | Vibrato | Controlled pitch modulation suitable for bowed instruments | `[planned]` | Not implemented |
| DSP-016 | Voice-level output safety | Prevent runaway amplitude and clamp/limit at the output boundary | `[in-progress]` | Current app clamps final Q23 conversion; model-level headroom/limiting needs further design |
| DSP-017 | Polyphonic mixing | Mix multiple model voices without blocking or dynamic allocation | `[in-progress]` | 8-voice benchmark app exists; actual device polyphony unverified |

### 6.1 Plucked-string prototype

The current prototype is intentionally narrow:

- 44.1 kHz, mono internal voice output.
- Fixed 2048-sample circular delay line.
- Fractional read position using linear interpolation.
- Two-point averaging loss filter.
- Deterministic noise-burst excitation.
- Single-precision state and arithmetic.
- No heap allocation, locks, I/O or platform calls in render.
- Approximate supported fundamental range: 21.6 Hz to 22.05 kHz, constrained by the delay capacity.

This is a first DSP building block, not a finished acoustic instrument model.

### 6.2 Bowed-string model

The bowed-string engine is a priority after the plucked-string baseline is stable. It should use a nonlinear friction relationship between bow and string velocity, with controllable bow force, bow speed and contact position. It must be tested for stable sustained oscillation, pitch behaviour, parameter transitions and CPU cost.

A physically inspired model is preferred over a simple oscillator with an envelope. The initial implementation may use a bounded approximation of the friction curve if it produces stable behaviour within the available compute budget.

## 7. Performance, expression and MIDI

| ID | Requirement | Acceptance intent | Status | Evidence / notes |
|---|---|---|---|---|
| MIDI-001 | MIDI Note On/Off | Play and release allocated voices | `[planned]` | FM1 platform MIDI APIs available; app mapping not implemented |
| MIDI-002 | Velocity | Map note velocity to excitation/output strength | `[in-progress]` | Plucked model supports linear velocity; MIDI event integration absent |
| MIDI-003 | Pitch bend | Continuous pitch adjustment without voice reset | `[planned]` | Requires delay tuning/modulation strategy |
| MIDI-004 | Modulation wheel CC1 | Map to a useful instrument expression parameter | `[planned]` | Per-preset mapping required |
| MIDI-005 | Volume CC7 | Channel or instrument volume control | `[planned]` | Not integrated |
| MIDI-006 | Expression CC11 | Performance expression control | `[planned]` | Not integrated |
| MIDI-007 | Sustain CC64 | Sustain/release behaviour for allocated voices | `[planned]` | Voice lifecycle not implemented |
| MIDI-008 | Channel pressure | Use after confirming platform event support and a sensible mapping | `[planned]` | Platform capability needs verification |
| MIDI-009 | Voice allocator | Handle note priority, voice stealing and release state deterministically | `[planned]` | No allocator yet |
| MIDI-010 | Multi-voice operation | Support 4 voices as a minimum goal, 8 as target and 12 as stretch goal, subject to measurement | `[planned]` | Targets only; no device measurements |

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
| UI-001 | Instrument selection | Select instrument family and model | `[planned]` | Current screen is benchmark-only |
| UI-002 | Four context-sensitive controls | Four primary parameters are visible and immediately editable | `[planned]` | Final mapping pending hardware control audit |
| UI-003 | Parameter feedback | Display names and values clearly on the 240 × 240 display | `[planned]` | No finished UI |
| UI-004 | Preset browser | Browse and load parameter presets | `[planned]` | Not implemented |
| UI-005 | Performance status | Provide useful voice/activity and audio status without clutter | `[planned]` | Benchmark screen currently displays voice count, CPU and xruns |
| UI-006 | Safe parameter updates | UI task publishes parameter changes safely; audio task owns live DSP state | `[planned]` | Benchmark app currently uses a narrow volatile voice-count handoff |

## 9. Voice management and polyphony

- Voice state must be preallocated or allocated during setup, never from the audio callback.
- Voice allocation must be deterministic and bounded.
- Note stealing must avoid clicks where practical, using short release/fade transitions.
- A voice must not be reinitialised concurrently by UI and audio contexts.
- Polyphony targets are engineering goals, not release guarantees.

| ID | Requirement | Acceptance intent | Status | Evidence / notes |
|---|---|---|---|---|
| VOX-001 | Static/preallocated voice state | No heap allocation during rendering | `[implemented]` | Benchmark app statically reserves eight voice states |
| VOX-002 | Voice count benchmark | Exercise 1, 2, 4 and 8 simultaneous voices | `[implemented]` | Device benchmark app and native harness exist |
| VOX-003 | Voice allocator | Allocate, release and steal voices predictably | `[planned]` | Not implemented |
| VOX-004 | Click-safe transitions | Avoid discontinuities on note-off, stealing and parameter changes | `[planned]` | Needs allocator/release design |
| VOX-005 | Four-voice minimum goal | Sustain four useful voices with stable audio on target hardware | `[planned]` | Hardware verification required |
| VOX-006 | Eight-voice target | Sustain eight useful voices with stable audio on target hardware | `[planned]` | Hardware verification required |
| VOX-007 | Twelve-voice stretch | Investigate twelve voices if measured budget permits | `[planned]` | Stretch goal only |

## 10. Real-time, memory and firmware constraints

These constraints apply to every DSP and application requirement.

| ID | Requirement | Acceptance intent | Status | Evidence / notes |
|---|---|---|---|---|
| RT-001 | No allocation in audio callback | No malloc/free or equivalent in render path | `[implemented]` | Current string render uses fixed state |
| RT-002 | No blocking in audio callback | No locks, waits, filesystem, logging or synchronous I/O | `[implemented]` | Current DSP is platform-independent |
| RT-003 | Bounded processing | Work per frame/block has a known upper bound | `[in-progress]` | Fixed loops; full worst-case budget not yet measured |
| RT-004 | Preallocated memory | Voice and scratch buffers allocated before rendering | `[implemented]` | Static benchmark allocations |
| RT-005 | Flash safety | Persistent flash operations only from permitted control context | `[planned]` | No preset persistence implemented |
| RT-006 | Bootloader preservation | Never overwrite or modify bootloader/recovery as part of normal app development | `[implemented-policy]` | Explicit project constraint |
| RT-007 | Audio diagnostics | Capture CPU load, peak timing and xruns on hardware | `[in-progress]` | Benchmark screen reads platform CPU/xrun diagnostics |
| RT-008 | Memory accounting | Track per-voice and total app memory, including shared pool usage | `[in-progress]` | Current voice state size is 8,216 bytes; total app budget incomplete |
| RT-009 | Second-core offload | Evaluate core 2 only if profiling shows a useful, safe partition | `[planned]` | APIs exist; no offload strategy validated |
| RT-010 | Output headroom | Prevent clipping when voices are mixed | `[in-progress]` | Current output conversion clamps Q23; mix headroom requires listening/measurement |

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

Host tests should cover:
- Output is finite and non-silent after note-on.
- Fundamental period is within a defined tolerance.
- Invalid inputs are rejected.
- Velocity scaling behaves as specified.
- Long renders remain stable.
- Voice reset/retrigger behaviour is deterministic.
- Parameter extremes do not produce NaN/Inf or runaway state.

### Hardware tests

The FM-1 benchmark app currently exercises 1, 2, 4 and 8 plucked-string voices. Follow [benchmark-procedure.md](benchmark-procedure.md).

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
| M1 — Plucked-string core | Karplus–Strong voice, fractional delay, damping and native tests | Correct pitch, stable output, deterministic tests | `[in-progress]` |
| M2 — DSP benchmark | 1/2/4/8 voice host and device benchmark | Real FM-1 measurements recorded; safe operating point identified | `[in-progress]` |
| M3 — Bowed-string prototype | One nonlinear bowed voice | Stable sustained tone, controllable bow parameters, native tests and measured device cost | `[not-started]` |
| M4 — Resonance and instrument identity | Modal body, dispersion, pluck position and pickup response | At least one convincing acoustic and one electric configuration | `[not-started]` |
| M5 — Performance system | MIDI mapping, allocator, sustain, pitch bend and expression | Repeatable polyphonic performance with safe transitions | `[not-started]` |
| M6 — Instrument UI and presets | Instrument pages, four primary controls and parameter presets | Usable FM-1 UI and documented preset format | `[not-started]` |
| M7 — Optimisation and release | Memory/CPU optimisation and hardware validation | Release acceptance criteria met on target device | `[not-started]` |

### Current next actions

1. Build and validate the target firmware with the pinned toolchain; do not infer hardware readiness from host CI.
2. Run 1/2/4/8 voice measurements on the FM-1 and record CPU/xrun/memory evidence.
3. Design and implement the first nonlinear bowed-string voice, keeping it platform-independent and natively testable.

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
| 1.0 | 2026-11-11 | Consolidated product vision, instrument scope, DSP, MIDI, UI, real-time constraints, acceptance criteria, roadmap and per-requirement status tracking. |
