# Move Anything - Granny Module

Granular sampler module for [Move Anything](https://github.com/charlesvestal/move-anything).

Granny plays short grains from a WAV file and gives you direct control over position, scan, windowing, and voice behavior.

## Quick Start

1. Install `Granny` from the Module Store.
2. Load `Granny` in a chain.
3. Open `Main` and choose `Sample File`.
4. Select a `.wav` from:
   - `/data/UserData/UserLibrary/Samples`
5. Play pads/notes.

By default, `sample_path` is empty, so Granny is silent until you select a file.

## Features

- File browser-based sample selection (`Sample File`)
- Granular controls for position, size, density, spray, and jitter
- Scan controls with end behaviors: `wrap`, `pingpong`, `clamp`, `stop`
- Window controls with three types:
  - `hann`: smooth, balanced default
  - `triangle`: sharper and more linear
  - `blackman`: strongest edge fade, softer highs
- Voice options: `mono`, `portamento`, `poly`
- Polyphony and portamento controls

## Prerequisites

- [Move Anything](https://github.com/charlesvestal/move-anything) installed on your Ableton Move
- WAV files available on device (recommended path below)

## Requirements

Recommended sample folder:

```text
/data/UserData/UserLibrary/Samples
```

Supported WAV formats:
- PCM 8/16/24/32-bit
- float32 WAV

Input is converted to mono float internally.

## Controls

Top-level pages:
- `Main`
- `Scan`
- `Window / Tone`
- `Pitch / Voice`
- `ADSR Envelope`

Main quick knobs:
- `Position`, `Size`, `Density`, `Spray`, `Jitter`, `Scan`, `Grain Gain`, `Quality`

Parameter guide by page:

### Main

- `Sample File`: Select the source `.wav` file.
- `Position`: Base playback point in the file (0-100%).
- `Size`: Grain length in milliseconds.
- `Density`: Grains emitted per second.
- `Spray`: Random position spread around `Position`.
- `Jitter`: Random timing shift per grain.
- `Grain Gain`: Per-grain output level.

### Scan

- `Scan Enable`: Enables or disables scan movement.
- `Scan`: Continuous position movement while note is held (negative = backward, positive = forward).
- `Scan End`: Behavior when scan reaches file edges (`wrap`, `pingpong`, `clamp`, `stop`).

### Window / Tone

- `Window`: Grain envelope shape (`hann`, `triangle`, `blackman`).
- `Win Shape`: Adjusts the active window shape response.
- `Quality`: Performance tier (`eco`, `normal`, `high`) that sets density/grain limits:
  - `eco`: max density `20`, max `16` active grains per voice
  - `normal`: max density `40`, max `32` active grains per voice
  - `high`: max density `60`, max `48` active grains per voice
- `Trigger`: Grain trigger mode (`per_voice`, `global_cloud`).

### Pitch / Voice

- `Pitch`: Coarse transpose in semitones.
- `Fine`: Fine pitch in cents.
- `KeyTrack`: How much note pitch affects playback pitch.
- `Play Mode`: Voice behavior (`mono`, `portamento`, `poly`).
- `Poly Voices`: Max simultaneous voices in `poly` mode.
- `Porta Time`: Glide time in `portamento` mode (ms).
- `Spread`: Stereo width between voices.

### ADSR Envelope

- `Attack`: Time for new grains to ramp from silence to full level.
- `Decay`: Time for new grains to move from peak to sustain level.
- `Sustain`: Spawn level used while the note is held.
- `Release`: After note-off, grains keep spawning while envelope fades out.

## Installation

### Module Store (recommended)

Install `Granny` from Move Anything's Module Store.

### Build from Source

Requires Docker (recommended) or ARM64 cross-compiler.

```bash
git clone https://github.com/handcraftedcc/move-anything-granny
cd move-anything-granny
./scripts/build.sh
./scripts/install.sh
```

Install target:

```text
/data/UserData/move-anything/modules/sound_generators/granny-grain/
```

Restart Move Anything after install.

## Current Limitations

- WAV-only input (no other file formats yet)
- Sample audio is processed as mono internally

## AI Assistance Disclaimer

This module is part of Move Everything and was developed with AI assistance, including Claude, Codex, and other AI assistants.

All architecture, implementation, and release decisions are reviewed by human maintainers.
AI-assisted content may still contain errors, so please validate functionality, security, and license compatibility before production use.
