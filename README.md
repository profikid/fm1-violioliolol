# fm1-violioliolol — FM-1 Physical Modelling Instrument

An experimental physical-modelling instrument for the M-VAVE FM-1. The goal is to synthesise plucked and bowed strings in real time—no recorded samples or sample playback.

## Product specification

The complete living specification is in [docs/PRD.md](docs/PRD.md). It tracks every requirement by ID with an editable status (`not-started`, `planned`, `in-progress`, `implemented`, `verified`, `blocked`, `deferred` or `not-applicable`) and an evidence field. Update the requirement row when implementation or verification changes; do not mark host-only results as FM-1 hardware verification.

## Current engineering status

- **Implemented:** first mono Karplus–Strong-style plucked-string voice in `dsp/string_model.c`.
- **Implemented:** fixed-size fractional-delay loop, two-point loss filter and deterministic noise excitation.
- **Implemented:** FM-1 benchmark app. Turn encoder **K1** to select 1, 2, 4 or 8 concurrent voices; the first N key LEDs show the selected load. The screen shows live platform CPU load and cumulative xruns.
- **Verified:** native DSP test passes. One voice uses 8,216 bytes of state; eight voices use 65,728 bytes.
- **Not yet verified:** FM-1 target build, audio timing, overruns, and actual hardware polyphony. Desktop benchmark figures are not hardware performance claims.
- **Not implemented:** bowed-string synthesis, body/modal resonators, finished instrument UI or presets.

## Firmware base

The project uses the same platform base as [CHOPPAH](https://github.com/profikid/fm1-break-choppah), pinned as the `base/` Git submodule. The pin is recorded in [docs/engineering-baseline.md](docs/engineering-baseline.md). The application uses the fm1-base 256-frame audio contract at 44.1 kHz.

## Build and test

```sh
git clone --recurse-submodules https://github.com/profikid/fm1-violioliolol.git
cd fm1-violioliolol
sh build.sh test          # native correctness + host throughput harness
sh build.sh --host        # fm1-base host simulator
sh build.sh               # FM-1 firmware; requires the JieLi toolchain and AC79 SDK
```

The host benchmark exercises 1, 2, 4 and 8 voices. Use the [FM-1 benchmark procedure](docs/benchmark-procedure.md) for real device measurements; record CPU load, xruns and memory use before setting any polyphony target.

## Project site

[Open the fm1-violioliolol project page](https://profikid.github.io/fm1-violioliolol/).

The site is published from `site/` by GitHub Actions. GitHub Pages must be enabled for this repository with **GitHub Actions** as the publishing source.
