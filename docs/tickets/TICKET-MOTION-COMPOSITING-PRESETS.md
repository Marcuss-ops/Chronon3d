# Motion compositing, asset import, and text preset closure

This checklist covers three user-prioritized motion-design gaps. Preserve
deterministic frame evaluation, bounded inputs, strict unsupported-feature
errors, and the canonical RenderPlan / scene / asset paths. Build and test
execution remains `NOT RUN` under the user's earlier instruction; author and
review focused regressions without claiming runtime verification.

## A. Masks and mattes

- [x] Record current behavior and limitations: the original V3 path mask and
  four track-matte modes; identify graph-builder validation and blend semantics.
- [x] Add deterministic path-mask morphing through the existing path morph
  operator, with matching topology, bounded scalar progress, and fingerprinted
  target geometry/track.
- [x] Detect dependency cycles across parent and track-matte edges before
  lowering; cover missing sources, cycle rejection, all four matte modes, and
  matte fingerprint inputs in authored decoder regressions.
- [x] Extend the V3 mask contract and canonical scene model to a bounded
  ordered list of masks with
  explicit `add`, `subtract`, `intersect`, and `difference` modes, plus path,
  rectangle, rounded rectangle, and ellipse geometry (maximum 16 entries;
  difference composes symmetric alpha coverage).
- [x] Apply bounded feather and expansion uniformly to path and primitive
  geometries using signed-distance alpha coverage.
- [x] Lower the combined masks through the existing canonical scene mask and
  render graph path; do not add a parallel mask renderer.
- [x] Validate mask geometry, bounds, first-operation semantics, and unsupported
  combinations before fingerprinting; fingerprint every mask input.
- [x] Add decoder, schema, fingerprint, and prepared-render regressions for
  animated path-mask morph and matte modes/cycles.
- [x] Add authored decoder and prepared-render regressions for every
  combined-mask mode and geometry, including
  deterministic ordering.
- [x] Preserve outside coverage when rasterizing inverted masks and path masks
  that extend beyond declared size bounds; add a direct alpha-frame regression
  for inverted primitive coverage.

## B. SVG, Lottie, and glTF import

- [x] Document existing bounded import behavior and concrete rejection cases
  for SVG, Lottie, and GLB in the importer tickets and static GLB contract.
- [x] Expand SVG-to-RenderPlan lowering to retain multiple authored paint
  styles by emitting deterministic sibling path nodes, preserving command and
  byte limits and capping imported styled paths at 4096.
- [x] Expand bounded Lottie lowering to static paths, ellipses, rectangles,
  and rounded rectangles in authored order; retain one shared solid fill/stroke
  or one style per geometry as sibling nodes, rejecting unsupported content
  and bounding total path commands.
- [x] Add focused authored regressions for SVG style/order lowering, multiple
  Lottie paths, and explicit rejection of animated/skinned/morph or over-budget
  GLB content.
- [x] Specify the supported static GLB contract (GLB, nodes, meshes, materials,
  animation/skin status, 64 MiB file and 16 MiB JSON caps, and bounded scene,
  instance, part, vertex, index, material, and image expansion) against the
  actual loader; add bounded static node/mesh coverage where existing scene
  types represent it.
- [x] Record animation, skinning, and morph targets as explicitly unsupported
  until scene/runtime contracts can represent them.
- [x] Add MeshLoader limit fixtures for oversized JSON, scene node count, and
  expanded vertices. Execution remains deferred with the rest of the test
  suite.
- [ ] Complete broader Lottie support: nested groups and transforms, exact
  Bodymovin paint-order/grouping semantics, comprehensive positive/negative
  fixtures, and rendered golden frames. Current lowering accepts one flat
  shape layer with static identity transforms only.
- [x] Make the SVG importer fail closed on unsupported elements/style
  properties, invalid paint values, and out-of-range opacity/stroke width. Add
  RenderPlan/compiler rejection fixtures for SVG text, `<use>` references,
  gradients/definitions, masks, unsupported stroke attributes, and missing
  assets; assert imported fill, stroke, opacity, and sibling-path order in
  authored integration coverage.
- [ ] Verify SVG sibling-path output with rendered goldens after the deferred
  build/test run.

## C. Reusable typography motion presets

- [x] Add a small typed authoring catalog built on existing `TextAnimatorSpec`,
  `GlyphSelectorSpec`, and `AnimatedValue` APIs; no new evaluator or registry.
- [x] Provide parameterized entrance/exit presets for fade, rise, drop,
  left/right slide, pop, blur, rotate, tracking, grapheme typewriter, and word
  reveal; make direction, timing, and selector unit explicit and validate
  finite bounded inputs.
- [x] Keep preset composition deterministic, validate generated specs, and
  expose presets as authored animator specs usable by existing scene/subtitle
  code.
- [x] Add deterministic keyframe sample assertions and invalid-input tests
  for timing and animated property parameters.
- [x] Assert that each catalog entry returns a valid canonical
  TextAnimatorSpec with deterministic sampled values; the specs plug directly
  into existing scene TextRunBuilder APIs and require no V3 serialization.

## Progress

- v4 — bounded compositing mask stacks now lower through the existing scene
  MaskNode with deterministic alpha composition and complete cache hashing.
  Animated path-mask morph and matte dependency checks remain covered; bounded
  SVG/Lottie/GLB import contracts widened and documented; text catalog added.
  SVG import/rejection integration fixtures now cover multi-style lowering.
  Lottie flat static multi-style support now has positive geometry/fill/stroke
  fixtures plus rejection cases for masks, groups, animation, gradients,
  mismatched styles, and dashes. Nested groups, animated properties, and
  rendered golden verification remain open.
- Verification status: `NOT RUN` (build/tests were explicitly deferred).
