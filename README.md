# Granny for Move Everything

`granny-grain` is a clean-room, CPU-bounded granular instrument module for [Move Everything](https://github.com/charlesvestal/move-anything).

## v1 scope

- MIDI notes trigger pitched granular playback from a loaded WAV sample.
- No custom `ui.js` (parameters are exposed via `module.json` + plugin metadata).
- Hard CPU guardrails:
  - polyphony clamped to 1-8
  - quality tiers (Eco/Normal/High) with bounded density and spray caps
  - fixed grain pools per voice (no dynamic allocation in the audio callback)
  - spawn clamp per block (`MAX_SPAWNS_PER_BLOCK = 4`)
- Safe sample swapping with atomic pointer exchange from non-audio thread.

## Parameters

- `position` (0..1): playback center in the sample
- `size_ms` (5..250): grain duration in milliseconds
- `density` (1..60): grains/second (tier-clamped)
- `spray` (0..1): random offset around position (tier-capped in samples)
- `jitter` (0..1): random spawn offset within current audio block
- `freeze` (0/1): lock position and PRNG seed for stable texture
- `pitch_semi` (-24..24): transposition in semitones
- `fine_cents` (-100..100): fine transposition
- `keytrack` (0..1): note-to-pitch tracking amount
- `window_type` (enum, v1 uses Hann only)
- `window_shape` (0..1): window warp amount
- `grain_gain` (0..1): per-grain level
- `polyphony` (1..8)
- `mono_legato` (0/1)
- `trigger_mode` (0 = per-voice, 1 = global cloud)
- `spread` (0..1): random stereo pan spread per grain
- `quality` (0..2): Eco / Normal / High
- `sample_index` (int): selects a file from `wavs/` (sorted alphabetically)

Debug/readback params (plugin side):
- `active_grains`
- `active_voices`
- `sample_loaded`

## Sample loading

The module loads from a fixed sample bank in `wavs/`.

Recommended structure:

```text
wavs/kick.wav
wavs/pad.wav
wavs/texture.wav
```

At startup it scans `wavs/`, sorts filenames alphabetically, and loads `sample_index = 0`.
Set `sample_index` to switch between these bundled files.

Supported WAV formats:
- PCM 8/16/24/32-bit
- float32 WAV

Input is converted to mono float internally.

## Build

```bash
./scripts/build.sh
```

Outputs:
- `dist/granny-grain/`
- `dist/granny-grain-module.tar.gz`

## Install

```bash
./scripts/install.sh
```

Installs to:

```text
/data/UserData/move-anything/modules/sound_generators/granny-grain/
```

Restart Move Everything after installation.
