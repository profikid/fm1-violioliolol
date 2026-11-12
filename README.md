# fm1-violioliolol — FM-1 Physical Modelling Instrument

A physical-modelling string instrument for the M-VAVE FM-1. No samples: every
note is a vibrating string, computed in real time.

![The violin on the FM-1 screen](docs/img/screen-violin.png)

- **19 instruments in 4 families:** finger, pick, slap, fretless and upright
  bass; nylon, steel, clean, driven and jazz guitar; violin, viola, cello and
  contrabass; harp, mandolin, banjo, koto and zither.
- **Plucked strings:** extended Karplus–Strong waveguides with stiffness,
  pluck position, pickup position, buzz and five excitation styles.
- **Bowed strings:** a nonlinear stick-slip waveguide with bow pressure, speed
  and position, and vibrato.
- **Modal bodies** shared by all voices; tone, drive and a limiter on the way out.
- **Up to 12 voices**, a deterministic allocator with click-free stealing.
- **Performance:** velocity and pressure from the FM-1's keys; MIDI pitch
  bend, mod wheel, volume, expression, sustain, pressure, the knobs on
  CC16–23 and program change.
- **16 user presets** that store model parameters, never audio.

Read the [manual](docs/manual.md) for the controls.

## Status

Experimental. Everything in the [PRD](docs/PRD.md) is implemented and tested
on the host, and the firmware builds and passes fm1-base's checks with the
pinned JieLi toolchain. **Nothing is verified on a physical FM-1 yet**: CPU
load and polyphony on the device are unmeasured, so the default polyphony of
8 is a guess. If you install it, the [benchmark procedure](docs/benchmark-procedure.md)
is the most useful thing you can do with it.

## Install

Download the `.fwsc` from the [latest release](https://github.com/profikid/fm1-violioliolol/releases/latest)
and install it with the official M-VAVE updater or the Firmware Hub. The
packages are built from source on GitHub by the shared FM-1 builder and
carry a build attestation:

```sh
gh attestation verify fm1-violioliolol-0.1.fwsc --repo profikid/fm1-violioliolol
```

This firmware does not touch the bootloader or the recovery path; the stock
firmware can be reinstalled the usual way.

## Build and test

```sh
git clone --recurse-submodules https://github.com/profikid/fm1-violioliolol.git
cd fm1-violioliolol
sh build.sh test                            # five native DSP suites + host throughput
sh build.sh --host && sh tests/scenarios/run.sh   # the whole firmware in fm1-base's simulator
sh build.sh                                 # FM-1 firmware: JieLi toolchain and AC79 SDK
```

The engine is in `dsp/` (platform-free C, see
[engineering-baseline.md](docs/engineering-baseline.md)); the FM-1 side is
`app/app.c`; the preset format is in [presets.md](docs/presets.md).

## Firmware base

Built on [fm1-base](https://github.com/charlesvestal/fm1-base) and
felucca-bsp, pinned as `base/` (fm1-base `f66ac4a`, felucca-bsp `d18879b`);
256-frame blocks at 44.1 kHz, the CPU at its stock 360 MHz.

## Project site

[profikid.github.io/fm1-violioliolol](https://profikid.github.io/fm1-violioliolol/), published from `site/`.

MIT licence.
