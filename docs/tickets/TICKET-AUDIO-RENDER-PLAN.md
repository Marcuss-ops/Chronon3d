# TICKET-AUDIO-RENDER-PLAN — Plan-driven audio mix

## Stato
COMPLETE — contract, native-FFmpeg mix path, and acceptance gates verified.

## Contratto
Render-plan v3 may carry an `audio` mix with ordered clips. Each clip has a
stable id, logical asset path, optional source trim (`clip_in`/`clip_out`),
timeline start, finite `gain_db`, `muted`, optional animated `gain_db` track,
and role `music|voice|sfx`. Mix controls are finite `master_gain_db`,
`duck_amount_db`, `duck_attack_ms`, and `duck_release_ms`. Audio beyond the
canvas duration is clipped with a structured warning; malformed trim, role,
gain, or unresolved assets fail validation.

`AudioMixCompiler` is the sole lowering point and emits a deterministic
`MixerSpec`: exact sample trim, frame-sampled gain automation, amix, and
voice-triggered sidechain ducking. The video render graph stays unchanged.
Native FFmpeg is required for plan audio. A plan containing audio with legacy
`--gop-source` fails explicitly. A pre-mixed temporary source is passed to the
existing mux timebase to preserve current A/V synchronization.

## Scope
- Render-plan v3 schema, decode, validation, asset resolution and fingerprint.
- Deterministic `AudioMixCompiler` and pre-mix integration before encode.
- Preserve the external single-source audio path for jobs without plan audio.
- Extend duration parity and output-contract verification.

## Acceptance gates
- [x] Decoder cases include validation and an out-of-canvas warning.
- [x] Fingerprint v7 golden updated with an explicit contract-change note.
- [x] Mixer unit case pins frame-to-sample trims and voice duck graph.
- [x] Native render smoke with two inputs yields one AAC stream; `ffprobe` confirms H.264 + AAC and matching 2.000 s durations.
- [x] `astats` RMS checks cover gain envelope and voice ducking segments. For a 3 s
  music/voice render at 48 kHz, AAC RMS was -19.61 dB over 0–0.8 s (ramp),
  -28.14 dB over 1–1.8 s (music gain), and -28.17 dB over 2.2–2.8 s. The
  300 Hz low-pass music band during voice measured -29.99 dB with ducking versus
  -26.63 dB with the voice clip changed to SFX; both controls were -29.27 dB
  after voice ended. This gate exposed and fixed short sidechain output and
  implicit voice-label fan-out by padding each bus to canvas duration and
  explicitly splitting the voice bus.
- [x] F3.5 duration parity: plan audio output and legacy AAC source output both
  contain H.264 + AAC streams at exactly 3.000 s (`ffprobe`). Plan mix has one
  AAC stream at 48 kHz.
- [x] Fail-closed checks: audio with missing `ffmpeg` exits 1 with
  `audio_ffmpeg_executable_missing`; plan audio with `--gop-source` exits 1
  with the explicit conflict error; plan audio with `--encoder-backend pipe`
  exits 1 with `render-plan audio requires the native FFmpeg encoder`.

## Demolition Debt
- **Owner:** Chronon media/render-plan maintainers.
- **Reason:** The existing mux API consumes one external audio source.
- **Exit condition:** The mux has a certified plan-native N-input/filter contract.
- **Equivalence gate:** Byte/sample timing, duration, AAC stream count and F3.5
  parity against the pre-mixed-source path.
- **Removal scope:** Temporary pre-mix source, subprocess bridge and its option.
- **Status:** ACTIVE.
