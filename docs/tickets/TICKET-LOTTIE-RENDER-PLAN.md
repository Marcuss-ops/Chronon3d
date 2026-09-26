# TICKET-LOTTIE-RENDER-PLAN — Bounded Lottie lowering

## Stato
CONTRACT APPROVED — no runtime Lottie renderer; decode and lower before plan compilation.

## Obiettivo
Resolve a logical `.json` Bodymovin/Lottie asset from a render plan and lower
its supported shape layers, transforms, and keyframes into existing
`ShapePathCommandPlan`, `AnimationPlan`, and `AnimationTrackPlan` fields.

## Contratto

- Lottie is a resolver-backed shape asset extension, separate from SVG. The
  logical reference must be relative, confined to the mounted root, and end in
  `.json`; missing or malformed assets fail with typed render-plan errors.
- The accepted subset is explicit: shape groups containing path, ellipse,
  rectangle, fill, stroke, and transform operators; layer transforms; and
  numeric keyframes with supported hold/linear interpolation. Every lowered
  value uses existing render-plan vocabulary and the existing sampler.
- Expressions, effects, masks, text layers, image layers, time remap, unsupported
  path operators, and unsupported easing/interpolation are rejected with a
  typed diagnostic. They are never dropped silently.
- Input bytes, nesting depth, layer count, shape count, keyframe count, path
  command count, frame range, and coordinate magnitude have fixed limits.
  Time range is explicit and bounded to the render canvas. Invalid values must
  be finite; lowered coordinates are quantized to 1e-4 before hashing.
- Lowering happens before the single render-plan compiler sees the plan. It
  adds no runtime subsystem, executor, shader, or expression evaluator.
- Prepared manifest content hash includes the source Lottie asset; the
  canonical content fingerprint includes its normalized lowered plan values.

## Acceptance gates

- [ ] Golden minimal fixtures cover path, ellipse/rectangle, fill/stroke,
  transform, hold keyframe, linear keyframe, and explicit frame range.
- [ ] Unsupported feature fixtures cover expressions, effects, masks, text,
  precomps, unsupported interpolation, and malformed structures; all fail
  explicitly.
- [ ] Limits cover deep nesting, oversized input, 100k shapes/commands, and
  coordinates outside bounds.
- [ ] Fingerprint is stable after normalization and changes with geometry or
  keyframe content.
- [ ] Render-plan CLI fixture lowers and renders an animated shape with a
  deterministic frame hash that changes across the expected keyframes.
- [ ] Focused decoder/compiler tests and relevant renderer suite pass.

## Demolition debt

- **Owner:** assets and render-plan maintainers.
- **Reason:** Lottie currently has no decoder or resolver path.
- **Exit condition:** every acceptance gate passes on one source SHA.
- **Removal scope:** none; this ticket defines the supported interchange subset.
