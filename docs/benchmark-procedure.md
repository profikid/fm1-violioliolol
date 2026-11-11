# FM-1 benchmark procedure

The instrument has the benchmark built in: the PERF page's BENCH row holds a
number of voices of the current instrument sounding and keeps them running
after they would have rung out, so the load stays constant while you read it.

## Controls

1. Pick the instrument to measure (ALGORITHM, PRESET).
2. Press **GLOBAL** for the PERF page.
3. Turn **K4** (BENCH): OFF → 1 → 2 → 4 → 8 → 12 voices.
4. The status line shows the render's CPU load and the xruns since power-on;
   the voice boxes show what is running.
5. **HOME** stops the benchmark.

Changing the instrument keeps the benchmark running on the new one.

## Measurement run

1. Install the release package with the official updater; this project never
   touches the bootloader or recovery.
2. Use a wired output; Bluetooth is not built in.
3. Measure NYLON (plucked, with a body) and CELLO (bowed, with a body) at
   minimum; DRIVE ELEC adds the drive stage.
4. At each voice count wait at least 10 seconds, then write down the CPU
   percentage and whether the xrun count moved.
5. Repeat each count three times; record the highest CPU percentage.
6. Stop at the first count that produces xruns or audible glitches. That count
   is over budget; the one below it is the candidate, not a guarantee.
7. Play the instrument at the chosen polyphony with chords and fast repeated
   notes for five minutes: does SHED ever light?

## Record

| Build | Instrument | Voices | CPU % (max of 3) | Xruns moved | SHED | Notes |
|---|---|---|---|---|---|---|
| v0.1 | NYLON | 1 | | | | |
| v0.1 | NYLON | 2 | | | | |
| v0.1 | NYLON | 4 | | | | |
| v0.1 | NYLON | 8 | | | | |
| v0.1 | NYLON | 12 | | | | |
| v0.1 | CELLO | 1 | | | | |
| v0.1 | CELLO | 2 | | | | |
| v0.1 | CELLO | 4 | | | | |
| v0.1 | CELLO | 8 | | | | |
| v0.1 | CELLO | 12 | | | | |

Then update VOX-005..007 and MIDI-010 in the [PRD](PRD.md) and set
`PM_DEFAULT_POLYPHONY` in `dsp/engine.h` to the result.

## Memory (from the v0.1 firmware build)

| | Bytes |
|---|---|
| Firmware image | 118,852 |
| RAM .data + .bss | 18,900 of 98,304 |
| Platform pool used | 185,984 of 344,064 |
| Engine (12 voices, body, output, queue) | 151,160 |
| One voice slot (the larger of the two models) | 12,460 |
| Plucked voice / bowed voice | 8,384 / 12,428 |

## Host reference

`sh build.sh test` ends with `bench_engine`: the whole engine at 1 to 12
voices per model on the build machine, about 130 ns a frame for 12 plucked
voices and 160 ns for 12 bowed ones on a desktop. That is a regression signal
only.

## What the numbers do not mean

- Desktop nanoseconds per frame are not FM-1 CPU timing.
- A firmware that builds and passes fm1-base's checks has not been heard.
- One board's result is not a polyphony guarantee for every unit.
