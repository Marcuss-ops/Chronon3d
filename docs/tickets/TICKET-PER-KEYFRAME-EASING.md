# TICKET-PER-KEYFRAME-EASING — Segment timing on animation keys

## Stato
COMPLETE — shared outgoing-key resolver, v3 decode path and v8 fingerprint golden are in place; targeted C ABI and CLI gates pass.

## Contratto
Render-plan schema v3 accepts optional `interpolation`, `easing`, and
`bezier_curve` on each animation keyframe. A key controls the outgoing segment
to the next key. Missing per-key fields preserve the existing track-level
easing. `interpolation` accepts `linear`, `hold`, or `bezier`; explicit linear
and hold override track easing, while bezier requires that key's curve.
Without `interpolation`, `easing` and `bezier_curve` may override the track as
a pair. Curves require finite values, x handles in [0,1], and y handles in
[-10,10], matching the existing track curve contract. Invalid combinations
fail validation/decode. Authored timing fields on the final key are ignored
and produce one stable `animation_last_key_timing_ignored` warning per track.

One shared segment-easing resolver is used by layer properties, camera,
text selectors/animators, morph, effect/time tracks, and audio gain. Cubic
Bezier x inversion uses fixed Newton and bisection iteration counts and fixed
epsilon; endpoint samples return the exact authored key values. No runtime
expression language or second sampler is added.

The canonical fingerprint includes all three optional key fields and advances
its domain version. Existing plans without these fields retain their decoded
behavior; the intentional fingerprint domain change is pinned by regenerated
goldens.

## Scope
- Extend the v3 extension schema (and v3 nested audio gain key schema).
- Decode and validate outgoing per-key timing, including ignored final-key warning.
- Resolve segment easing once for every existing shared track consumer.
- Keep fixed, deterministic Bezier inversion and exact endpoints.
- Update fingerprint and golden contract tests.

## Acceptance gates
- [x] Decoder/schema fixtures cover hold, bezier, track fallback, invalid x handles and final-key warning; shared raw validation rejects non-finite/out-of-range curves.
- [x] Sampler tests pin exact key endpoints, hold behavior and Bezier inversion against a reference value within 1e-6.
- [x] Determinism test repeats the same sample and checks bit-identical float output.
- [x] The single resolver feeds layer properties, camera, text selector/animator, morph/effect timing and audio gain; focused consumer checks cover camera and audio, with existing C ABI coverage for shared compiler paths.
- [x] Fingerprint tests prove each new field changes identity and pin the v8 domain golden.
- [x] Targeted render-plan C ABI and CLI test suites pass.

## Demolition Debt
Reuse the shared `AnimationTrackPlan` and existing `EasingCurve` / animated-value
sampling path. Do not add expressions, runtime callbacks, or per-consumer easing
implementations.
