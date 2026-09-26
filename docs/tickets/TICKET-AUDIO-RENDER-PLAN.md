# TICKET-AUDIO-RENDER-PLAN — Plan-driven audio mix

## Stato
OPEN — contract-first plan approved 2026-09-26.

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
- [ ] Decoder and validation unit cases, including out-of-canvas warning.
- [ ] Fingerprint goldens updated with an explicit contract-change note.
- [ ] N-input mix integration yields one AAC stream; `ffprobe` confirms it.
- [ ] `astats` RMS checks cover gain envelope and voice ducking segments.
- [ ] F3.5 duration parity passes for plan audio and legacy source audio.
- [ ] No-native-FFmpeg and plan-audio plus `--gop-source` fail with typed errors.

## Demolition Debt
- **Owner:** Chronon media/render-plan maintainers.
- **Reason:** The existing mux API consumes one external audio source.
- **Exit condition:** The mux has a certified plan-native N-input/filter contract.
- **Equivalence gate:** Byte/sample timing, duration, AAC stream count and F3.5
  parity against the pre-mixed-source path.
- **Removal scope:** Temporary pre-mix source, subprocess bridge and its option.
- **Status:** ACTIVE.
