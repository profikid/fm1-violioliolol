# Engineering baseline

## Pinned source baseline

The instrument firmware is intended to use the same platform base as CHOPPAH,
not a separate SDK port.

| Component | Pin | Meaning |
|---|---|---|
| CHOPPAH reference | `profikid/fm1-break-choppah@c31629f8d1e92bbd379e9d0fc74c2017e8ce06fc` | Reference application and build conventions |
| fm1-base submodule | `charlesvestal/fm1-base@317cb86cc60d6286fa80a02743e94e6a60ff842d` | Exact platform source referenced by that CHOPPAH commit |
| Public FM1 SDK | `m-vave/fm1_open@4c7496daac04a95ff10d1afdcc8f9ead2dbabc41` | SDK comparison point; not a substitute for fm1-base |

The repository now pins `base/` to the exact fm1-base Gitlink used by the
CHOPPAH reference commit. In that pinned platform, `device/unity.c` defines
the default `APP_BLOCK` as 256 frames; this app also sets `APP_BLOCK 256u`
explicitly in `app/app.h`. At 44.1 kHz this is a 5.805 ms audio block.
The public `fm1_open` SDK header separately declares 128 frames, but that is
not the platform contract used by this project. Do not mix those constants.

## The engine

Everything that makes sound is in `dsp/`, platform-free C99 in single
precision, and built at -O2 apart from the platform (`o2` in `app/fm1.toml`;
`app/dsp` links to it):

| File | |
|---|---|
| `pm_math.h` | sin, exp2, sqrt, atan in float: no libm, no doubles |
| `string_model.c` | plucked or struck string (PRD 6.1) |
| `bowed_model.c` | nonlinear bowed string (PRD 6.2) |
| `body_model.c` | modal body, and the output stage: tone, drive, level, limiter |
| `instrument.c` | the 19 factory patches, 13 bodies, preset bank ([presets.md](presets.md)) |
| `engine.c` | 12 voices, the allocator, MIDI, the control-to-audio queue |

`tests/engine/run.sh` fails the build if anything in `dsp/` includes more
than `<stdint.h>` or writes a floating literal without its `f`. The firmware
build checks again that no soft-double routine was linked.

`app/app.c` is the FM-1 side: the panel, the screen, the store. The audio
interrupt (`app_render`) reads MIDI and owns the engine; the main loop
(`app_frame`) only posts messages to it and is the one place flash is written.

## Measurement discipline

Run `sh build.sh test` for native correctness and throughput checks, and
`sh build.sh --host && sh tests/scenarios/run.sh` for the whole firmware in
fm1-base's simulator.
The host benchmark is only a repeatable regression signal. On-device
measurements must be made in the actual FM-1 render path using the platform's
audio timing diagnostics. Record voice count, block size, peak ticks,
overruns, and memory use. Do not infer FM-1 polyphony from desktop timing.

## Safety constraints

- Keep the bootloader and recovery path untouched.
- Do not write persistent flash from the audio callback.
- Keep all voice memory statically allocated or allocated during setup.
- Keep DSP independent of fm1-base headers so it remains host-testable.
