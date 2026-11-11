# FM-1 benchmark procedure

This is a first-pass measurement app, not the final instrument UI.

## Controls

- Turn **K1** clockwise to step through 1 → 2 → 4 → 8 voices.
- Turn **K1** counter-clockwise to reduce the load.
- The first N key LEDs light to indicate the selected voice count.
- The screen reports the platform's render CPU percentage, CPU clock reading
  and cumulative audio xrun count.

Each selection retriggers the active string voices. The strings naturally decay,
but the renderer continues to process the selected voice count, so the load
remains active after the initial transient.

## Measurement run

1. Build and install a test firmware from this repository only after the
   bootloader/recovery path is known good. This project does not alter the
   bootloader.
2. Use a wired output for measurements; keep Bluetooth disabled.
3. At each voice count (1, 2, 4, 8), wait at least 10 seconds.
4. Record the displayed CPU percentage, CPU clock, xrun count and the voice
   state memory from the build.
5. Repeat each count at least three times. Record the maximum CPU percentage
   and whether the xrun count increased.
6. Stop at the first count that produces xruns or unstable audio. Do not treat
   the next count as a target.

The display reads `plat_cpu_pct()`, `plat_cpu_khz()` and `plat_xruns()` from
the pinned fm1-base platform. CPU percentage is the platform's audio-render
load relative to its block budget; xruns are cumulative since boot.

## Current memory footprint

The native build reports 8,216 bytes per voice:
- 8,192 bytes for the 2,048-sample float delay line;
- 24 bytes for the remaining voice state (compiler ABI dependent).

The FM-1 app reserves eight voice states in the platform pool: 65,728 bytes.
This is a static reservation, not a measured total firmware RAM figure.

## What the numbers do not mean

- Desktop nanoseconds per sample are not FM-1 CPU timing.
- A successful host build is not a successful device firmware build.
- One board's result is not a guaranteed polyphony limit for every unit.
