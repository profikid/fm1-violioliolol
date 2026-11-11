# VIRTUOSO — FM-1 Physical Modelling Lab

VIRTUOSO is an experimental physical-modelling instrument project for the M-VAVE FM-1.

The goal is to synthesise plucked and bowed string instruments in real time using DSP models — not recorded samples or sample playback.

## Firmware base

VIRTUOSO will use the same FM-1 firmware base as [CHOPPAH](https://github.com/profikid/fm1-break-choppah). It is a separate instrument project, not a separate hardware SDK port.

## First engineering milestones

1. Pin the CHOPPAH firmware base and SDK revision; verify the real audio callback, memory map and block size.
2. Build a hardware DSP benchmark harness for plucked-string voices at increasing polyphony.
3. Prototype one nonlinear bowed-string voice and measure its CPU and memory cost.

The initial voice-count goals are targets only; they must be established by measurements on real FM-1 hardware.

## Project site

[Open the VIRTUOSO project page](https://profikid.github.io/fm1-violioliolol/).

The site is published from `site/` by GitHub Actions. GitHub Pages must be enabled for this repository with **GitHub Actions** as the publishing source.
