# RenderPlan and motion feature closure

This ticket records the RenderPlan/motion backlog and its verifiable repository
contracts. Branch and upstream metadata are intentionally not asserted. Test
status below is updated only from commands actually executed in this workspace.

## 0. Reconcile implementation surface

- [x] Audit the present RenderPlan type/schema/decoder/compiler surface; do not
  infer branch or upstream state from unavailable Git metadata.
- [x] Confirm current `ShapeContentTypePlan` initially contained only the five
  basic shapes; Star/Polygon/Arrow/Arc/Grid/DotGrid now have typed contracts
  and generated paths, while WorldMap/Chart remain unsupported.
- [x] Confirm `SpringPlan` and spring decode support are not present here.
- [x] Confirm V3 has no `shape.counter` payload and reject it at schema/decode
  time. The distinct `text_counter` contract remains supported on text layers.
- [x] Keep subsequent checklist items scoped to source files and APIs that
  actually exist; absent contracts are recorded as dependencies and have
  fail-closed decoder regressions instead of silent no-op behavior.

## 1. Spring track evaluation

- [x] Define bounded `SpringPlan` parameters and track/keyframe ownership in
  the V3 typed model using canonical `SpringConfig`.
- [x] Align V3 schema and both decoder validation stages, including config
  ranges, keyframe precedence, and positive segment spans.
- [x] Add deterministic spring progress baking through canonical
  `sample_spring()`; preserve overshoot within a documented finite bound, and
  execute its bake and prepared-evaluation regressions in C ABI tests.
- [x] Wire baked keys into supported layer, text animator/selector, fill-color,
  and stroke-dash animation paths without changing `AnimatedValue` runtime
  semantics.
- [x] Execute spring bake, decoder, compiler evaluation, overshoot, zero-span
  rejection, and fingerprint coverage in the passing Chronon3D C ABI suite.
- [x] Update checklist from authored-only to executed acceptance evidence.

## 2. Prepared shape lowering

- [x] Verify every currently accepted primitive (Rect, RoundedRect, Ellipse,
  Line, Path) emits a prepared node; existing compiler-boundary coverage locks
  that contract.
- [x] Add typed contracts, decoder/schema validation, generated geometry,
  and path-based prepared lowering for Star, Polygon, Arrow, and Arc.
- [x] Add deterministic path generation with bounded inputs and include the
  generated commands plus source shape kind in the plan fingerprint.
- [x] Execute decoder acceptance, generated geometry, prepared-node, and
  fingerprint-sensitivity coverage in the passing Chronon3D C ABI suite.
- [x] Add Grid and DotGrid contracts with checked arithmetic and a named
  maximum of 4096 generated lines/dots, enforced before path allocation.
- [x] Execute exact-limit, over-limit, invalid-input, and deterministic
  generation coverage; the grid-cap focus passes 5 assertions.
- [x] Add negative decoder coverage for WorldMap, Chart, and `counter`;
  unsupported payloads cannot silently lower to no-op.
- [x] Add typed phone/browser DeviceFrame contracts and lower phone notch and
  browser toolbar/traffic lights to independent prepared subnodes.
- [x] Execute decoder, fingerprint, and prepared subnode-count regressions for
  both DeviceFrame variants in the passing C ABI suite.

## 3. Effect catalog and software processors

- [x] Audit current source: Vignette uses the single existing `VignetteParams`
  / `EffectType::Vignette` processor; DirectionalBlur and RadialBlur already
  have catalog entries, software processors, registrations, and focused tests.
- [x] Add Chromatic Aberration and Halftone to the canonical X-macro catalog,
  IDs, typed params, effect type enum, and clipped deterministic processors.
- [x] Execute RenderPlan contracts, schema, decoder, fingerprint, lowering,
  catalog checks, and pixel-level processor regressions for both effects in
  the passing C ABI and effects suites.

## 4. Grid resource cap

- [x] Establish `kMaxGeneratedGridElements = 4096` for Grid lines and DotGrid
  dots, using checked axis counts and multiplication before vector allocation.
- [x] Enforce the cap in deterministic path generation before allocation and
  before the paths enter the typed plan.
- [x] Execute exact-boundary, over-limit, invalid-spacing, and decoder
  rejection coverage in the passing C ABI suite and 5-assertion grid-cap focus.

## 5. WorldMap honesty / implementation

- [x] Reject WorldMap at V3 schema/decode time: the prior typed payload accepted
  geographic center/zoom/highlights/routes even though the RenderPlan compiler
  only emitted a base rectangle and a separate materializer ignored center/zoom.
- [x] Remove the unsupported WorldMap typed fields and both misleading lowering
  routes so accepted plans cannot fingerprint or render a placeholder as a map.
- [x] Add validator and decoder regressions with structurally meaningful
  center/zoom/route/highlight payloads; test execution status is recorded below.

## 6. Counter payload

- [x] Remove the schema-inconsistent `shape.counter` parser/model/fingerprint
  path and reject additive counter payloads at schema/decode time.
- [x] Preserve and test `text_counter` separately; it has a V3 schema, decoder,
  fingerprint, and text compiler path. No legacy shape-counter fingerprint
  migration applies because `shape.counter` is not accepted.

## 7. Additional animation tracks

- [x] Add animated trim start/end/offset, with schema, decoder, compiler,
  fingerprint, and progress-ring evaluation tests; these are in the passing C ABI
  suite.
- [x] Add RenderPlan track `loop_mode` values `none`, `cycle`, `pingpong`, and
  `offset`; wire cycle/ping-pong to `AnimatedValue`, implement signed
  per-cycle value delta for numeric/vector offset loops, include mode in v12
  fingerprints, and execute decode/compiler/core evaluation regressions in the
  passing C ABI suite.
- [x] Add bounded seeded `wiggle` drivers to scalar RenderPlan tracks for
  opacity, blur, text tracking, and stroke dash offset; fingerprint every
  driver input (v13) and reject unsupported vector/color properties during
  decode. Decoder, fingerprint, and evaluation regressions pass in the C ABI
  suite.
- [x] Add `auto_orient` lowering from sampled position tangents to generated
  `rotation_z` keyframes; decode requires moving position tracks, forbids
  explicit rotation tracks, and caps generated spans at 65535 frames. Include
  the flag in v14 fingerprints; decode and prepared-render regressions pass in
  the C ABI suite.
- [x] Reconcile PCM-to-keyframe boundaries: ChrononMotion3D is present and
  provides `OfflineBaking` PCM analysis plus audio-driver baking into ordinary
  Motion tracks; its opt-in RenderPlan adapter samples those tracks into regular
  keyframes. Chronon3D separately offers V3 `audio_react`, whose file decode
  requires native FFmpeg and whose tests disclose that capability when absent.
  There is no cross-library PCM/audio-react data contract, so RenderPlan-side
  decoder/file-based `audio_react` remains gated by Chronon3D native FFmpeg;
  Motion-authored PCM can still be pre-baked into emitted scalar/vector tracks.
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
  over the authored layer duration; exact transform sample and invalid input
  regressions pass in Motion authoring and all Motion CTests.
- [x] Connect DirectionalBlur and RadialBlur to RenderPlan V3 schema,
  decoder, fingerprint, and compiler lowering; existing software processors
  and pixel tests cover execution; the C ABI, effects, and Motion adapter gates
  all pass.
- [x] Add bounded `glitch`, RGB split, square-wave scanline, isotropic Gaussian
  lens-softening, and animated aurora-gradient presets on existing typed effect
  and gradient-animation contracts; prepared-stack and deterministic gradient
  regressions pass in the C ABI/effects acceptance suites.
- [x] Confirm DirectionalBlur and RadialBlur are already fully cataloged,
  registered in the software backend, and covered by focused effect tests.

## 9. Chart, layout, and Tier C boundaries

- [x] Resolve chart scope: remove the partially implemented ChartPlan and
  reject `shape.type=chart` and additive `shape.chart` at V3 schema/decode time.
  The former implementation ignored value formatting/donut options in one path,
  rendered pie data as a triangle wedge, and emitted only a base rectangle in
  another compiler path. Data-viz charts need a reviewed, complete contract.
  Path morph remains an independent implemented feature.
- [x] Expose bounded V3 `flow` and `grid_layout` contracts through LayerPlan,
  typed decoding/fingerprinting, LayerBuilder, and the existing SceneBuilder
  layout-solver pass. Decoder fingerprints pass in the 6-assertion focus;
  prepared scene placement and solver checks pass in the 444-case scene suite.
- [x] Record the architecture gates for 3D globe, expressions, full SVG/Lottie,
  SRT word highlighting, and animated/skinned glTF in proposed
  [ADR-033](../adr/ADR-033-render-plan-tier-c-foundations.md); these features
  remain unsupported until their contracts are reviewed.
- [x] Add validator and decoder regressions for Chart and WorldMap, including
  additive payload forms; neither is accepted by the V3 shape contract.

## 10. Acceptance review

- [x] Audit the implemented backlog payloads end to end with the executed
  Chronon3D C ABI, scene and Motion adapter suites, plus Motion authoring and
  showcase round-trip checks recorded in the 2026-09-30 progress entry.
- [x] Audit unsupported WorldMap, Chart, and shape.counter fail-closed paths,
  and preserved text_counter behavior through validator/decoder cases included
  in the passing complete C ABI suite.
- [x] Record exact test/build results and distinguish successful targeted gates
  from the broader, unrun Chronon3D test inventory.
- [ ] Run the repository-wide source-diff audit and remaining Chronon3D test
  aggregates; this ticket-specific bounded acceptance does not claim the full
  repository acceptance gate is complete.

## 11. Parametric inline mesh primitives (active)

- [x] Add V3 `inline_mesh.primitive` input for box, plane, sphere, cylinder,
  cone, torus, and bounded polygon extrusion; reject mixed primitive/raw
  geometry at schema and decoder boundaries. Extrusion ear-clips simple
  polygons deterministically and emits flat-normal caps and perimeter-UV sides;
  non-zero bevel, holes, and self-intersecting profiles fail closed.
- [x] Expand descriptors deterministically into the existing raw indexed
  triangle representation before typed budget validation, compilation, and
  fingerprinting. Normal, UV, per-mesh count, and aggregate byte limits remain
  owned by the existing inline-mesh contract.
- [x] Pin generated counts and deterministic identity, verify primitive input
  and equivalent raw triangle input have equal content fingerprints, and verify
  dimension changes affect fingerprints in the C ABI test.
- [x] Add a software frame-hash canary for a concave extruded mesh; the
  renderer core suite passes and checks stable hash plus visible pixel coverage.
- [ ] Add strict Vulkan capability certification for generated meshes; a CPU
  oracle does not certify Vulkan rendering.
- [x] Reconcile the V3 extension schema and run the full C ABI test target; it
  passes after preserving the V32 legacy hash for layers without a backdrop
  filter. Focused primitive, decoder, and legacy fingerprint tests pass
  (17, 12, and 2 assertions respectively). `chronon3d_sdk_tests` also passed
  before the kind-specific parameter restrictions were tightened.

## 12. Mesh post effects (bounded V3 tranche)

- [x] Allow only the currently certified mesh post effects: `color.tint`,
  `light.glow`, `light.bloom`, and `generate.noise`. Other effects continue to
  fail closed for mesh layers.
- [x] Add decoder coverage for accepted/rejected effect IDs and include mesh
  effects in typed budget validation.
- [ ] Certify the four effects through strict Vulkan using mesh-source pixel
  oracles before expanding the mesh effect allow-list.
- [x] Expose optional `casts_shadows` and `accepts_shadows` on V3 mesh layers,
  apply them in scene compilation, and include them in the content fingerprint.
  The frozen V2 projection removes these extension-only fields after V3
  validation. Default-absent plans keep their prior fingerprint.
- [ ] Add CPU and Vulkan shadow oracle scenes that prove a mesh casts onto a
  receiver and can independently opt out of receiving shadows.
- [x] Keep newly added optional backdrop-filter inputs additive in the
  fingerprint stream: absence emits no bytes, while present values are hashed;
  pin both the V32 no-filter golden and the new filter sensitivity.

## Progress log

- **v12 — 2026-09-30:** continued the Chronon3D fast aggregate at `-j 8`.
  Confirmed `chronon3d_tests_fast` includes `chronon3d_cli_tests`; the configured
  build has `CHRONON3D_ENABLE_NATIVE_FFMPEG=OFF`. Fixed the concrete no-FFmpeg
  compile error in `gop_smart_copy_bitstream.cpp` by compiling the AVPacket
  helper only when native FFmpeg is enabled; that translation unit now builds.
  The CLI test target then stopped while archiving `chronon3d_cli_core` with
  `No space left on device` (filesystem at 99%, about 4 GiB available); no build
  artifacts were deleted. Built the ten other fast C++ test targets using
  `ninja -j 8`. Eight additional suites passed: scene, optimizer, cache,
  compositor, timeline, animation helpers, text health, and precomp. The core
  suite fails reproducibly: `TextUnitMap_8lvl` (including its ZWJ and combining
  mark cases) passes its assertions then crashes in destruction; GDB resolves
  the failing `TextUnitMap` destructor to the legacy `glyph_selector.hpp`
  declaration, consistent with the documented same-name narrow/canonical type
  conflict. The deterministic suite has two unrelated execution prerequisites:
  its CLI export cases need a discoverable CLI binary/native video export, which
  this FFmpeg-disabled build cannot link under the available disk capacity.
  Therefore the fast aggregate and full source-diff audit remain open. `git diff
  --check` in Chronon3D passes.

- **v11 — 2026-09-30:** verified the available cross-module acceptance targets
  using `ninja -j 8` (bounded builds). ChrononMotion CTest: **19/19 passed**;
  `chrononmotion_authoring`: **209 checks, 0 failures**;
  `chrononmotion_advanced_authoring`: **158 checks, 0 failures**;
  `chrononmotion-showcase --all`: **60/60 cases, 53,959 assertions**, and
  `renderplan_roundtrip`: **10 assertions**. The adapter's Chronon3D test suite
  passed **10/10 cases, 142 assertions**. Chronon3D C ABI: **227/227 cases,
  5260/5260 assertions**; Chronon3D scene tests (run from the source root so
  bundled relative font assets resolve): **444/444 cases, 3050/3050
  assertions**; overlay resolver focus **6/6 cases, 38/38 assertions**;
  TextRunBuilder focus **8/8 cases, 29/29 assertions**. ChrononTemplate CTest:
  **11/11 passed**, including all six editorial plan validations, and
  `tools/author_editorial_motions.py --check` reported the 24 motions + 6
  captions identical. The V3 extension schema parses via `python3 -m json.tool`.
  Fixed the stale prepared-media-frame assertion to check the actual
  `RoundedRect` frame and image radius contract; full C ABI returned green.
  Targeted WorldMap fail-closed (decoder 5 assertions; validator 5 violations)
  and `text_counter` (4 assertions) cases passed; the complete C ABI run also
  passed. Direct doctest name filters use substring matching and selected zero
  cases for two overqualified patterns; those invocations are not counted as
  evidence. Grid cap (5 assertions) and flow/grid contract (6 assertions)
  focuses passed. The broader `chronon3d_effects_tests` suite exposed a stale Halftone fixture
  (it used 0.75 linear luma while asserting the all-white, uninked result);
  the fixture now uses true white (1.0). The green rerun passes **251/251 cases
  and 245,833/245,833 assertions**. It also exposed a missing explicit
  `chronon3d_backend_text` link on this test target; added its additive link
  under the existing target guard so effects integration tests receive their
  text backend. Software backend passed 28/28 cases and 15,875 assertions;
  animation passed 3/3 cases and 49 assertions. Running effects from build cwd
  had separately exposed relative font-path resolution; the effects test suite
  is now linked to the text backend and run from the Chronon3D source root.
  Source inspection finds no RenderPlan `SemanticAnchor` or `DiagramGraph`
  contract; these remain outside the exposed implementation boundary, while text
  semantic IDs and scene `CalloutParams` exist independently. CTest passes all
  six selected Chronon3D aggregates (C ABI, scene, Motion adapter, effects,
  animation, and software backend); remaining CMake-registered aggregates were
  not built/run, and full cross-repository source-diff validation remains open.

- **v10 — 2026-09-28:** executed the suites for the first time under an
  authorized build. The v9 ordered gradient dither had silently invalidated
  `tests/renderer/helpers/test_stroke_gradient_helpers.cpp`:
  `resolve_stroke_gradient_color` samples every pixel through
  `graphics::dither_gradient_sample`, while those expectations (authored
  2026-06-15) assert the undithered analytic stop colour at
  `epsilon(1e-5f)`. No Bayer cell has a zero token, so that can never hold.
  Measured red: `-tc="*stroke_gradient*"` 15 cases | 6 passed | 8 failed,
  41 assertions | 28 passed | 13 failed. Every observed value matches
  `analytic + ((token + 0.5)/64 - 0.5)/255` for its cell bit-for-bit, with
  alpha untouched and pure black surviving the clamp — i.e. a stale
  expectation, not a renderer defect. Expectations now compare against the
  analytic stop colour within the renderer's own ±0.5/255 amplitude, plus a
  new regression pinning the per-pixel dither itself against an isolated
  single-stop gradient. Green: 15 | 15 | 0 and 46 | 46 | 0, full suite
  176/176, and `tools/check_test_hygiene.sh` 4/4. Detection power is
  unchanged rather than relaxed: a +0.0002 per-channel deviation (10× below
  the bound) still fails the suite. This went unnoticed only because v6-v9
  were authored without executing the suites.

- **v9 — 2026-09-27:** started the cinematic-background follow-up in the
  available Chronon3D checkout: added deterministic ordered gradient dither,
  GridBackground edge fade/major glow/isometric geometry with CPU and Vulkan
  shader replicas, typed `background_look` expansion for film_grade/aurora/
  liquid, Vignette lowering, fingerprint v19, glass backdrop rim/refraction/noise,
  and GPU Multiply/Screen/Overlay
  shader modes. Added Vulkan bright-pass thresholding and native bloom dispatch
  through the effect stack and command planner. Focused regressions are
  authored; builds and Vulkan parity remain outside the bounded acceptance
  run. The bounded particle/bokeh contract is in [TICKET-CINEMATIC-PARTICLES-BOKEH](TICKET-CINEMATIC-PARTICLES-BOKEH.md);
  ChrononMotion3D/OfflineBaking is not present in this checkout.

- **v8 — 2026-09-27:** completed the source-level acceptance audit and closed
  the available Flow/Grid runtime contract. Chart data morph remains out of
  scope pending reviewed semantics. ChrononMotion3D is present and its audio
  baking plus the adapter are covered by the green Motion acceptance suites.
  The schema and targeted tests pass; later builds supersede the earlier
  `NOT RUN` status.
- **v7 — 2026-09-27:** exposed existing flow/grid solver layouts through V3
  RenderPlan decoding and runtime LayerBuilder lowering; bounded group,
  dimensions, spacing, axis/alignment/distribution inputs and included every
  layout input in v15 content fingerprints. Added decoder and evaluated-scene
  regressions; the 6-assertion decoder focus and full scene test target pass.

- **v6 — 2026-09-27:** added compositional glitch/RGB-split/scanline/lens-blur
  and animated aurora-gradient helpers using existing effect and fill-gradient
  lowerings; added effect-stack and gradient regressions; C ABI/effects suites
  later pass.
- **v5 — 2026-09-27:** added deterministic layer idle presets (`breathe`,
  `bob`, `pulse`, `floatDrift`) with bounded period/duration and sampled-track
  regressions. ChrononMotion3D `OfflineBaking` is available in its sibling
  source tree; its PCM baking tests pass in the Motion authoring suite.
- **v4 — 2026-09-27:** added `auto_orient` position-tangent sampling and
  generated rotation keys, with validation, v14 fingerprint input, and decoder
  plus prepared-evaluation regressions; C ABI suite passes.
- **v3 — 2026-09-27:** added bounded seeded scalar wiggle drivers to V3 track
  decoding/lowering, including second-based evaluation and v13 fingerprints;
  added decoder and deterministic render-plan evaluation regressions; C ABI
  suite passes.
- **v2 — 2026-09-27:** wired RenderPlan loop modes through V3 decoding,
  AnimatedValue evaluation, and v12 fingerprints; added cycle, ping-pong, and
  offset evaluation regressions; C ABI suite passes.
- **v1:** initial repository audit and implementation checklist.
