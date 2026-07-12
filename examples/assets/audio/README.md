# examples/assets/audio/

Placeholder directory for the ProductLaunch audio asset (the user-spec
remotion-style audio cue "launch pad" sting).  This directory exists
because the user-spec lists the audio asset as one of the canonical
elements to ship, but **no `scene.audio()` / `l.audio(...)` /
sound_play API surface exists in the codebase today** (the Chronon3D
engine has no audio-cue integration).

## §honesty gap

Per AGENTS.md §`Non segnare verde una suite che restituisce failure` +
the Cat-3 freeze rule *no gratuitous additions*:

- **No `launch_pad.wav` is committed** in this commit, because no
  engine API consumes it.  Committing an orphan asset would violate
  Cat-3 ("no new symbols unless justified") and silently bloat the
  repo with a binary file that no test, golden, or gate reads.
- **The composition `product_launch()` does not invoke any audio
  API** for the same reason.

## Forward-point

When the engine gains an audio-cue API surface, the natural cut is:

1. Drop a small ~1s placeholder synth at
   `examples/assets/audio/launch_pad.wav` (44.1kHz mono, 16-bit PCM,
   ~88 KB).
2. Add `l.audio("launch_pad", {.path = "examples/assets/audio/launch_pad.wav"})`
   to the `subtitle` layer in `content/launches/product_launch.cpp`
   with `l.opacity_anim()` analogous to the subtitle fade-in
   keyframe window (Frame{55..70}) — the audio cue will fire at
   subtitle-reveal time.
3. Extend `tools/check_product_launch_demo.sh` to assert that
   `ffprobe -v error -show_streams | grep codec_type=audio` returns
   exactly one stream + `codec_name=aac` (or `mp3` if the user chose
   lossy).

The 3 points above are deferred to a future
**TICKET-AUDIO-CUE-API** ticket (one ticket per rot-pattern per
AGENTS.md Cat-3 + the canonical one-ticket-per-feature pattern).

## Files in this directory (none today, only this README)

- `README.md` (this file, the §honesty-gap placeholder).
- `launch_pad.wav` (NOT YET — see forward-point above).

## Existing references

The audio-cue intent is documented in:

- `docs/CHANGELOG.md` — `feat(content): ProductLaunch composition` entry.
- `examples/product_launch.json` — `assertions_for_gate[]` array
  intentionally does NOT include an `audio stream present` assertion
  per the §honesty gap.
