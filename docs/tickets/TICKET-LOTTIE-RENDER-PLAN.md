# TICKET-LOTTIE-RENDER-PLAN — Bounded Lottie lowering

## Stato
IN PROGRESS — resolver-backed lowering supports one flat static shape layer
with path/ellipse/rectangle geometry and bounded solid paint. Animation,
nested groups, and full Bodymovin paint semantics remain open.

## Obiettivo
Resolve a logical `.json` Bodymovin/Lottie asset from a render plan and lower
its supported shape layers, transforms, and keyframes into existing
`ShapePathCommandPlan`, `AnimationPlan`, and `AnimationTrackPlan` fields.

## Contratto

- Lottie is a resolver-backed shape asset extension, separate from SVG. The
  logical reference must be relative, confined to the mounted root, and end in
  `.json`; missing or malformed assets fail with typed render-plan errors.
- The implemented subset is explicit: one flat shape layer containing static
  path, ellipse, rectangle, fill, stroke, and identity transform items. A
  single solid fill/stroke can apply to every geometry, or each geometry can
  have one corresponding style; lowering emits sibling path nodes in geometry
  order. Nested groups, non-identity transforms, and animation are currently
  rejected. Every accepted value uses existing render-plan vocabulary.
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

- [ ] Golden minimal fixtures cover path, ellipse/rectangle, shared and
  per-geometry fill/stroke, transform, hold keyframe, linear keyframe, and
  explicit frame range.
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

## Implementation progress

- [x] v3 plan schema accepts a logical `.json` Lottie source; decoder validates
  the path form and the asset enters the prepared manifest/content fingerprint.
- [x] Compiler resolves the asset under `AssetResolver`, limits input to
  10 MiB, lowers static cubic paths and ellipse/rectangle primitives, supports
  one shared or per-geometry solid RGB fill/stroke, and quantizes geometry to
  1e-4. Unsupported layers and non-identity or animated transforms fail.
- [x] Flat static geometry preserves authored geometry order as sibling path
  nodes; aggregate path command count is bounded.
- [x] Authored compiler fixture covers multiple geometries, per-geometry
  fills/strokes, and explicit rejection cases for masks, nested groups,
  animated geometry, gradients, style-count mismatch, dashed strokes, and
  expressions. Build/test execution remains deferred and is not claimed as
  passing.
- [ ] Broader Bodymovin paint-order/group semantics, animated keyframes,
  nested group transforms, non-identity layer transforms, and rendered golden
  frames remain unsupported or unverified.

## Demolition debt

- **Owner:** assets and render-plan maintainers.
- **Reason:** Lottie currently has no decoder or resolver path.
- **Exit condition:** every acceptance gate passes on one source SHA.
- **Removal scope:** none; this ticket defines the supported interchange subset.
