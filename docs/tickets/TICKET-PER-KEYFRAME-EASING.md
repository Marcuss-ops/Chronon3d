# TICKET-PER-KEYFRAME-EASING — Segment timing on animation keys

## Stato
OPEN — contract pinned; implementation and acceptance gates pending.

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
- [ ] Decoder/schema fixtures cover linear, hold, bezier, track fallback, invalid x/NaN and final-key warning.
- [ ] Sampler unit tests pin exact endpoints, C0 boundaries, hold behavior, and Bezier inversion within 1e-6.
- [ ] Determinism test repeats sampling and pins identical output/hash.
- [ ] Consumer tests cover compiler properties, camera, text animator/selector, morph, and audio gain.
- [ ] Fingerprint tests prove each new field changes identity and pin the new domain golden.
- [ ] Targeted render-plan and C ABI test suites pass.

## Demolition Debt
Reuse the shared `AnimationTrackPlan` and existing `EasingCurve` / animated-value
sampling path. Do not add expressions, runtime callbacks, or per-consumer easing
implementations.
