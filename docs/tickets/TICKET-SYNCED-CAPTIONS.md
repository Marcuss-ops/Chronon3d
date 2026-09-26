# TICKET-SYNCED-CAPTIONS — Timed-text plan expansion

## Stato
IN PROGRESS — adapter/CLI lowering and SRT raw-frame smoke pass; broader fixture/gate automation remains.

## Contratto
Caption source formats dispatch by extension through the canonical timed-text
adapter collector: SRT, ASS, JSON (and existing VTT support). JSON word timing is
authoritative. SRT/ASS provide cue timing; kinetic word timing is distributed
evenly and emits the `caption_word_timing_estimated` render-plan warning.

`render --captions <file> --caption-preset <preset>` expands captions before
Chronon receives the plan. The font comes from an existing text layer style or
an explicit `--caption-font` logical asset. Each cue lowers to a text `LayerPlan` with exact
start/duration frames and existing `TextSelectorPlan` / `AnimationTrackPlan`
animators. Presets are data over the current text animator model; no compiler
or animation engine extension is introduced. Unsupported preset/source input
fails with a typed CLI error.

## Scope
- Adapter dispatch in `timed_text_adapter_support.hpp` by file extension.
- CLI flags and cue-to-layer lowering at the authoring boundary.
- Karaoke fill, word pop and typewriter presets using existing selectors.
- Keep editorial ids and timed-text parsing outside the render-plan compiler.

## Acceptance gates
- [x] Adapter dispatch is covered for SRT/ASS/JSON; existing fixture tests pin parsed cue/word data.
- [x] CLI expansion test pins exact start/duration frames and supported kinetic presets.
- [x] Estimated word timing produces a plan warning, never silent inference.
- [x] Repeated expansion preserves the plan fingerprint.
- [x] CLI `--captions` SRT typewriter smoke renders MP4; decoded raw frame hashes change during the cue.

## Demolition Debt
No new parser or timing engine is introduced. The adapter collector remains the
single format-dispatch point; the CLI lowering is the canonical boundary.
