# TICKET-LOTTIE-RENDER-PLAN — Bounded Lottie lowering

## Stato
SUPERSEDED by [`TICKET-LOTTIE-ANIMATED`](TICKET-LOTTIE-ANIMATED.md), the active
canonical ticket for bounded Bodymovin lowering. This file is retained as an
older RenderPlan-specific audit; its acceptance and status are governed by the
canonical ticket. The former `IN PROGRESS` scope below is historical.

Historical status: resolver-backed lowering supports a bounded subset of one 2D
shape layer, including nested groups, selected animated transforms, masks,
path morphs, trim paths, and animated solid paint. Focused decoder/compiler
fixtures pass locally; CLI pixel goldens, exhaustive limit/error coverage, and
broader Bodymovin semantics remain open.

## Obiettivo
Resolve a logical `.json` Bodymovin/Lottie asset from a render plan and lower
its supported shape layers, transforms, and keyframes into existing
`ShapePathCommandPlan`, `AnimationPlan`, and `AnimationTrackPlan` fields.

## Contratto

- Lottie is a resolver-backed shape asset extension, separate from SVG. The
  logical reference must be relative, confined to the mounted root, and end in
  `.json`; missing or malformed assets fail with typed render-plan errors.
- The implemented subset is explicit: one 2D shape layer with path, ellipse,
  and rectangle geometry; nested groups with bounded static transforms; and
  selected native animation tracks for layer/group transforms, path morphs,
  Trim Paths, fill color/opacity, and stroke width. Static linear gradients,
  strokes/dashes, masks, and repeaters are also lowered through existing plan
  vocabulary. Animated group scale is limited to uniform scale, and animated
  nested group transforms are rejected.
- Effects, expressions, time remap, other layer kinds (including text/image),
  unsupported path operators, and unsupported easing/interpolation fail with
  typed diagnostics. The implementation also rejects unsupported combinations
  such as non-simultaneous Trim Paths and out-of-contract mask/paint values.
- Input bytes, nesting depth, layer count, shape count, keyframe count, path
  command count, frame range, and coordinate magnitude have fixed limits.
  Time range is explicit and bounded to the render canvas. Invalid values must
  be finite; lowered coordinates are quantized to 1e-4 before hashing.
- Lowering happens before the single render-plan compiler sees the plan. It
  adds no runtime subsystem, executor, shader, or expression evaluator.
- Prepared manifest content hash includes the source Lottie asset; the
  canonical content fingerprint includes its normalized lowered plan values.

## Acceptance gates

- [ ] CLI pixel goldens cover path, ellipse/rectangle, shared and per-geometry
  fill/stroke, transforms, keyframes, and explicit frame range. Compiler
  fixtures currently prove lowering/evaluation only.
- [ ] Unsupported-feature fixtures cover expressions, effects, text/image
  layers, precomps, unsupported interpolation, and malformed structures; all
  fail explicitly. Masks have a supported bounded path/opacity subset and must
  not be listed as categorically unsupported.
- [ ] Limits cover deep nesting, oversized input, 100k shapes/commands, and
  coordinates outside bounds.
- [ ] Fingerprint is stable after normalization and changes with geometry or
  keyframe content.
- [ ] Render-plan CLI fixture lowers and renders an animated shape with a
  deterministic frame hash that changes across the expected keyframes.
- [x] Focused decoder/compiler Lottie cases pass locally (9/9, 173/173).
- [ ] Relevant renderer suite and CLI rendered-output oracle pass on the same
  source SHA.

## Implementation progress

- [x] v3 plan schema accepts a logical `.json` Lottie source; decoder validates
  the path form and the asset enters the prepared manifest/content fingerprint.
- [x] Compiler resolves the asset under `AssetResolver`, limits input to
  10 MiB, and lowers bounded geometry, gradients, strokes, groups, masks,
  repeaters, path operators, and the selected animation tracks listed above.
  Geometry values are quantized to 1e-4 where the compiler normalizes them.
- [x] Flat static geometry preserves authored geometry order as sibling path
  nodes; aggregate path command count is bounded.
- [x] Focused compiler fixtures exercise multiple geometries, paint,
  transforms, masks, morphs, animated paint, Trim Paths, repeater lowering, and
  explicit unsupported-input failures.
- [ ] Full Bodymovin layer/paint-order semantics, complete limit-boundary and
  malformed-input coverage, and a CLI-rendered deterministic pixel golden
  remain open. The compiler tests evaluate lowered scenes; they are not pixel
  output evidence.

## Demolition debt

- **Owner:** assets and render-plan maintainers.
- **Reason:** none identified; lowering reuses the existing asset resolver and
  RenderPlan/animation vocabulary, with no parallel runtime or shader path.
- **Exit condition:** if a temporary compatibility bridge is introduced,
  remove it once the native lowering path passes equivalent fixtures.
- **Removal scope:** none currently.

## Local decoder/compiler re-run — 2026-10-11

The existing `linux-fast-dev` C ABI binary ran `--test-case='*Lottie*'`:
**9/9 cases and 173/173 assertions passed**. The filter includes the schema
asset-path contract and compiler fixtures for static geometry, group/layer
transforms, masks, two- and multi-keyframe path morphs, Trim Paths, and
animated fill/stroke. The test source predates the binary, but the checkout is
dirty; this is local CPU evidence, not clean-SHA CI. No CLI pixel render was
performed, so the golden gate remains open.
