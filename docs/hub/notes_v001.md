The first release of fm1-violioliolol: a physical-modelling string instrument for the M-VAVE FM-1. No samples; every note is a vibrating string, computed as it plays.

**Experimental.** Everything is built and tested in fm1-base's simulator and with native DSP tests, and the firmware passes fm1-base's build checks, but it has not been measured on an FM-1 yet. CPU load and polyphony on the device are unknown; the default of 8 voices is a guess. The PERF page has a built-in benchmark: [docs/benchmark-procedure.md](https://github.com/profikid/fm1-violioliolol/blob/main/docs/benchmark-procedure.md) says how to measure it, and results are very welcome as issues.

**What is in it**

- 19 instruments in four families: finger, pick, slap, fretless and upright bass; nylon, steel, clean, driven and jazz guitar; violin, viola, cello and contrabass; harp, mandolin, banjo, koto and zither.
- Plucked strings: Karplus–Strong waveguides with Lagrange-interpolated tuning, stiffness, pluck and pickup position, buzz, five excitation styles.
- Bowed strings: a nonlinear stick-slip waveguide with bow pressure, speed, position, attack and vibrato.
- Modal bodies; tone, drive and an output limiter.
- Up to 12 voices with a deterministic allocator and click-free stealing; one voice is shed automatically if the render runs out of time.
- Velocity and pressure from the keys; MIDI pitch bend, mod wheel, CC7, CC11, sustain, pressure, CC16–23 for the knobs, program change.
- 16 user preset slots, CRC-checked in the store.

**Install:** with the [web installer](https://profikid.github.io/fm1-violioliolol/install/) (Chrome or Edge, USB), or the `.fwsc` below with the Firmware Hub. [Try it in the browser](https://profikid.github.io/fm1-violioliolol/emu/) first. The firmware never touches the bootloader; the installer's m-bootloader box is opt-in. The package is built from source on GitHub and attested: `gh attestation verify fm1-violioliolol-<version>.fwsc --repo profikid/fm1-violioliolol`.

[Manual](https://profikid.github.io/fm1-violioliolol/manual/) · [PRD and status](https://github.com/profikid/fm1-violioliolol/blob/main/docs/PRD.md)
