# TICKET-SYNCED-CAPTIONS — Timed-text plan expansion

## Stato
OPEN — contract-first plan approved 2026-09-26.

## Contratto
Caption source formats dispatch by extension through the canonical timed-text
adapter collector: SRT, ASS, JSON (and existing VTT support). JSON word timing is
authoritative. SRT/ASS provide cue timing; kinetic word timing is distributed
evenly and emits the `caption_word_timing_estimated` render-plan warning.

`render --captions <file> --caption-preset <preset>` expands captions before
Chronon receives the plan. Each cue lowers to a text `LayerPlan` with exact
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
- [ ] SRT/ASS/JSON fixtures decode to stable cue/word timing goldens.
- [ ] Builder tests pin exact first/last frame boundaries.
- [ ] Estimated word timing produces a plan warning, never silent inference.
- [ ] CLI `--captions` E2E raw-frame hashes show present, changing text.
- [ ] Repeated expansion preserves the plan fingerprint.

## Demolition Debt
No new parser or timing engine is introduced. The adapter collector remains the
single format-dispatch point; the CLI lowering is the canonical boundary.
