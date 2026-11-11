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

## First DSP prototype

`dsp/string_model.c` is a platform-free Karplus–Strong-style plucked-string
voice:
- single-precision state and arithmetic;
- fixed 2048-sample circular delay line;
- fractional read position using linear interpolation;
- two-point averaging loss filter;
- deterministic noise-burst excitation;
- no heap allocation, locks, I/O, or platform calls in render.

The 2048-sample capacity bounds the lowest supported fundamental to about
21.6 Hz at 44.1 kHz. The current prototype is deliberately mono and one-pole;
it is not yet a complete instrument model (no dispersion, body modes,
pickup model, pluck-position comb, or bowed-string solver).

## Measurement discipline

Run `tests/engine/run.sh` for native correctness and throughput checks.
The host benchmark is only a repeatable regression signal. On-device
measurements must be made in the actual FM-1 render path using the platform's
audio timing diagnostics. Record voice count, block size, peak ticks,
overruns, and memory use. Do not infer FM-1 polyphony from desktop timing.

## Safety constraints

- Keep the bootloader and recovery path untouched.
- Do not write persistent flash from the audio callback.
- Keep all voice memory statically allocated or allocated during setup.
- Keep DSP independent of fm1-base headers so it remains host-testable.
