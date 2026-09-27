# RenderPlan and motion feature closure

This ticket tracks the user supplied feature backlog against the current `main`
checkout. Checkboxes are updated as each repository acceptance criterion is
implemented. Build execution is intentionally deferred by user instruction;
tests authored but not executed remain marked `NOT RUN`.

## 0. Reconcile implementation surface

- [x] Confirm checkout is `main` at `b12503629`, with no upstream divergence.
- [x] Confirm current `ShapeContentTypePlan` initially contained only the five
  basic shapes; Star/Polygon/Arrow/Arc/Grid/DotGrid now have typed contracts
  and generated paths, while WorldMap/Chart remain unsupported.
- [x] Confirm `SpringPlan` and spring decode support are not present here.
- [x] Confirm the V3 schema and decoder expose no `counter` payload, and add
  decoder coverage proving unsupported primitive types/counter data fail closed.
- [x] Keep subsequent checklist items scoped to source files and APIs that
  actually exist; absent contracts are recorded as dependencies and have
  fail-closed decoder regressions instead of silent no-op behavior.

## 1. Spring track evaluation

- [x] Define bounded `SpringPlan` parameters and track/keyframe ownership in
  the V3 typed model using canonical `SpringConfig`.
- [x] Align V3 schema and both decoder validation stages, including config
  ranges, keyframe precedence, and positive segment spans.
- [x] Add deterministic spring progress baking through canonical
  `sample_spring()`; preserve overshoot within a documented finite bound.
- [x] Wire baked keys into supported layer, text animator/selector, fill-color,
  and stroke-dash animation paths without changing `AnimatedValue` runtime
  semantics.
- [x] Add spring unit, decoder, compiler evaluation, overshoot, zero-span
  rejection, and fingerprint tests (authored; execution NOT RUN).
- [x] Update checklist: implementation/tests authored; execution remains
  `NOT RUN` per user instruction.

## 2. Prepared shape lowering

- [x] Verify every currently accepted primitive (Rect, RoundedRect, Ellipse,
  Line, Path) emits a prepared node; existing compiler-boundary coverage locks
  that contract.
- [x] Add typed contracts, decoder/schema validation, generated geometry,
  and path-based prepared lowering for Star, Polygon, Arrow, and Arc.
- [x] Add deterministic path generation with bounded inputs and include the
  generated commands plus source shape kind in the plan fingerprint.
- [x] Add decoder acceptance, generated geometry, prepared-node, and
  fingerprint-sensitivity tests (authored; execution NOT RUN).
- [x] Add Grid and DotGrid contracts with checked arithmetic and a named
  maximum of 4096 generated lines/dots, enforced before path allocation.
- [x] Add exact-limit, over-limit, invalid-input, and deterministic-generation
  tests for procedural paths (authored; execution NOT RUN).
- [x] Add negative decoder coverage for WorldMap, Chart, and `counter`;
  unsupported payloads cannot silently lower to no-op.
- [x] Add typed phone/browser DeviceFrame contracts and lower phone notch and
  browser toolbar/traffic lights to independent prepared subnodes.
- [x] Add decoder, fingerprint, and prepared subnode-count regressions for both
  DeviceFrame variants (authored; execution NOT RUN).

## 3. Effect catalog and software processors

- [x] Audit current source: Vignette uses the single existing `VignetteParams`
  / `EffectType::Vignette` processor; DirectionalBlur and RadialBlur already
  have catalog entries, software processors, registrations, and focused tests.
- [x] Add Chromatic Aberration and Halftone to the canonical X-macro catalog,
  IDs, typed params, effect type enum, and clipped deterministic processors.
- [x] Add RenderPlan contracts, V3 schema, decoder, fingerprint, lowering,
  catalog checks, and pixel-level processor regressions for both effects
  (authored; execution NOT RUN).

## 4. Grid resource cap

- [x] Establish `kMaxGeneratedGridElements = 4096` for Grid lines and DotGrid
  dots, using checked axis counts and multiplication before vector allocation.
- [x] Enforce the cap in deterministic path generation before allocation and
  before the paths enter the typed plan.
- [x] Add exact-boundary, over-limit, invalid-spacing, and decoder rejection
  tests for oversized Grid/DotGrid inputs (authored; execution NOT RUN).

## 5. WorldMap honesty / implementation

- [x] Confirm WorldMap is not currently accepted by V3 schema/decoder, so there
  is no placeholder projection that can claim to honor center/zoom.
- [x] Choose fail-closed behavior for this checkout: reject the WorldMap shape
  entirely because there is no typed contract or bundled geographic asset.
- [x] Reject WorldMap before fingerprinting; no center/zoom/highlight/dots
  fields can enter the accepted V3 plan.
- [x] Add decoder regression proving rejection of WorldMap; no WorldMap plan
  type, GeoJSON resolver, or projection exists in this checkout (execution NOT RUN).

## 6. Counter payload

- [x] Confirm no `shape.counter` exists in schema, decoder, typed plan,
  compiler, or fingerprint; add rejection tests so it cannot silently enter.
- [x] No legacy fingerprint migration is needed because the payload is not in
  the accepted V3 contract.

## 7. Additional animation tracks

- [x] Add animated trim start/end/offset, with schema, decoder, compiler,
  fingerprint, and progress-ring evaluation tests (authored; execution NOT RUN).
- [x] Add RenderPlan track `loop_mode` values `none`, `cycle`, `pingpong`, and
  `offset`; wire cycle/ping-pong to `AnimatedValue`, implement signed
  per-cycle value delta for numeric/vector offset loops, include mode in v12
  fingerprints, and add decode/compiler/core evaluation regressions (authored;
  execution NOT RUN).
- [x] Add bounded seeded `wiggle` drivers to scalar RenderPlan tracks for
  opacity, blur, text tracking, and stroke dash offset; fingerprint every
  driver input (v13) and reject unsupported vector/color properties during
  decode. Add decoder, fingerprint, and evaluation regressions (authored;
  execution NOT RUN).
- [x] Add `auto_orient` lowering from sampled position tangents to generated
  `rotation_z` keyframes; decode requires moving position tracks, forbids
  explicit rotation tracks, and caps generated spans at 65535 frames. Include
  the flag in v14 fingerprints and author decode/render regressions (execution
  NOT RUN).
- [x] Resolve PCM-to-keyframe lowering dependency: the current checkout
  contains neither `OfflineBaking` nor an audio PCM decoder/spectrum analyzer;
  only an optional ChrononMotion3D adapter is referenced and its source checkout
  is absent. This remains unavailable until that dependency is supplied, so no
  synthetic PCM driver was added.
- [x] Add animated trim to the current PathOperator contract/compiler, enabling
  progress-ring and line-draw animation for existing path geometry.

## 8. Motion presets and effects

- [x] Confirm deterministic camera shake is already implemented by the
  camera V1 `HandheldNoise` modifier and `CameraShakeConfig`/`ShakePreset`
  helpers; camera-program tests cover deterministic evaluation, seed changes,
  channel decorrelation, and random-access order. No RenderPlan camera modifier
  decoder exists, so this is complete in the existing camera authoring API;
  exposing modifiers through RenderPlan remains a separate contract gap.
- [x] Add deterministic `breathe`, `bob`, `pulse`, and `floatDrift` presets
  on the existing `Layer::anim_transform` tracks, sampled at period quarters
  over the authored layer duration; add exact transform sample and invalid
  input regressions (authored; execution NOT RUN).
- [x] Connect DirectionalBlur and RadialBlur to RenderPlan V3 schema,
  decoder, fingerprint, and compiler lowering; existing software processors
  and pixel tests cover execution (additional plan tests authored; NOT RUN).
- [x] Add bounded `glitch`, RGB split, square-wave scanline, isotropic Gaussian
  lens-softening, and animated aurora-gradient presets on existing typed effect
  and gradient-animation contracts; add prepared-stack and deterministic
  gradient regressions (authored; execution NOT RUN).
- [x] Confirm DirectionalBlur and RadialBlur are already fully cataloged,
  registered in the software backend, and covered by focused effect tests.

## 9. Chart, layout, and Tier C boundaries

- [x] Resolve chart morph-data scope: this checkout has no typed ChartPlan,
  chart data model, or chart lowering; V3 rejects `shape.type=chart` before
  fingerprinting (covered by the unsupported-shape decoder regression). Path
  morph is independently implemented and tested. Data-viz chart interpolation
  remains a future feature requiring a chart contract.
- [x] Expose bounded V3 `flow` and `grid_layout` contracts through LayerPlan,
  typed decoding/fingerprinting, LayerBuilder, and the existing SceneBuilder
  layout-solver pass. Decoder fingerprint and prepared runtime-placement
  regressions are authored (execution NOT RUN).
- [x] Record the architecture gates for 3D globe, expressions, full SVG/Lottie,
  SRT word highlighting, and animated/skinned glTF in proposed
  [ADR-033](../adr/ADR-033-render-plan-tier-c-foundations.md); these features
  remain unsupported until their contracts are reviewed.
- [x] Add fail-closed decoder regressions for Chart and WorldMap: neither is
  accepted by this checkout's V3 shape schema or decoder.

## 10. Acceptance review

- [x] Audit the implemented backlog payloads end to end: every newly accepted
  RenderPlan contract has typed decode/fingerprint/lowering coverage and focused
  regressions authored; execution remains `NOT RUN` under the no-build request.
- [x] Audit unsupported WorldMap, Chart, counter, and absent PCM paths: decoder
  tests assert fail-closed behavior or the dependency is documented above.
- [x] Record test execution status as `NOT RUN` until the user authorizes build
  and test execution.
- [x] Run final static-only checks: V3 schema parses as JSON and `git diff
  --check` passes; builds/tests remain `NOT RUN` by user instruction.

## Progress log

- **v9 — 2026-09-27:** started the cinematic-background follow-up in the
  available Chronon3D checkout: added deterministic ordered gradient dither,
  GridBackground edge fade/major glow/isometric geometry with CPU and Vulkan
  shader replicas, typed `background_look` expansion for film_grade/aurora/
  liquid, Vignette lowering, fingerprint v19, glass backdrop rim/refraction/noise,
  and GPU Multiply/Screen/Overlay
  shader modes. Added Vulkan bright-pass thresholding and native bloom dispatch
  through the effect stack and command planner. Focused regressions are
  authored; builds and Vulkan parity are pending. The bounded particle/bokeh
  contract is in [TICKET-CINEMATIC-PARTICLES-BOKEH](TICKET-CINEMATIC-PARTICLES-BOKEH.md);
  ChrononMotion3D/OfflineBaking is not present in this checkout.

- **v8 — 2026-09-27:** completed the source-level acceptance audit and closed
  the available Flow/Grid runtime contract. Chart data morph and PCM baking are
  recorded as unavailable because their typed chart and OfflineBaking/audio
  dependencies do not exist in this checkout. Schema and whitespace checks
  pass; builds and tests remain `NOT RUN`.
- **v7 — 2026-09-27:** exposed existing flow/grid solver layouts through V3
  RenderPlan decoding and runtime LayerBuilder lowering; bounded group,
  dimensions, spacing, axis/alignment/distribution inputs and included every
  layout input in v15 content fingerprints. Added decoder and evaluated-scene
  regressions. Tests authored and `NOT RUN`.

- **v6 — 2026-09-27:** added compositional glitch/RGB-split/scanline/lens-blur
  and animated aurora-gradient helpers using existing effect and fill-gradient
  lowerings; added effect-stack and gradient regressions. Tests authored and
  `NOT RUN`.
- **v5 — 2026-09-27:** added deterministic layer idle presets (`breathe`,
  `bob`, `pulse`, `floatDrift`) with bounded period/duration and sampled-track
  regressions. Recorded PCM analysis as unavailable in this checkout because
  the optional ChrononMotion3D `OfflineBaking` source/API is absent. Tests
  authored and `NOT RUN`.
- **v4 — 2026-09-27:** added `auto_orient` position-tangent sampling and
  generated rotation keys, with validation, v14 fingerprint input, and decoder
  plus prepared-evaluation regressions. Tests authored and `NOT RUN`.
- **v3 — 2026-09-27:** added bounded seeded scalar wiggle drivers to V3 track
  decoding/lowering, including second-based evaluation and v13 fingerprints;
  added decoder and deterministic render-plan evaluation regressions. Tests
  authored and `NOT RUN`.
- **v2 — 2026-09-27:** wired RenderPlan loop modes through V3 decoding,
  AnimatedValue evaluation, and v12 fingerprints; added cycle, ping-pong, and
  offset evaluation regressions. Tests authored and `NOT RUN`.
- **v1:** initial repository audit and implementation checklist.
