# TICKET-SVG-RENDER-PLAN — Resolve SVG assets into shape plans

## Stato
IN PROGRESS — the six base SVG/shape animation certification gate
(`scripts/ci/check_svg_motion_animations_gate.py`) passes end to end; c_abi
(280 cases) and svg loader suites are green. Animated compositor repeaters
(plan-level `repeater` operators with keyframe tracks) sample per frame for
native shapes and lowered Lottie paths, including the schema branch and a
fingerprinting hook. Bounded
SVG geometry and object-bounding-box or user-space linear and radial gradients
lower through native Chronon paints, including `gradientTransform` animation baked
into gradient keyframes. Path-local SMIL
`animateTransform` (translate/scale/rotate), opacity and solid paint animations
lower to sampled native tracks, with `calcMode="spline"` cubic-bezier easing and
bounded `repeatCount` expanded into repeated keyframe cycles. Local geometry
references through `<defs>/<use>` are expanded at compile time. Static clip paths
and opaque white vector masks on the root, or on a group scope covering every
enclosed path, lower to the native path-mask plan. Root-level `feGaussianBlur`
and `feDropShadow` filters lower to layer effects. Direct-child group
translate/uniform scale/rotate and opacity animations fan out to child path
tracks. Two-keyframe `stop-color` and `stop-opacity` SMIL animations on gradient
stops lower to native gradient animation tracks. Per-element (distinct)
clipping, complex masks, nested transform stacks, multi-keyframe gradient-stop
animation, arbitrary filter graphs and SVG text remain open for the
architectural reasons recorded below.

## Obiettivo
Allow a render plan shape to refer to an SVG asset through the mounted
`AssetResolver`. Import its geometry into the existing `ShapePathCommandPlan`
vocabulary before graph compilation; rendering continues through the current
shape pipeline.

## Contratto

- Only `.svg` assets are imported. The logical path is resolved with
  `AssetResolver::resolve_logical`; absolute paths, traversal, missing assets,
  malformed XML and unsupported SVG elements fail with typed plan diagnostics.
- SVG `<text>` is explicitly unsupported and must fail; no font lookup or
  implicit text conversion occurs.
- Supported SVG path and primitive geometry is lowered to the current shape
  path commands in document order. Multiple solid/gradient fill, solid stroke and
  per-element opacity are preserved as sibling path nodes; unsupported style
  semantics fail explicitly. The implemented geometry/style vocabulary is
  limited to `svg`, `g`, path/rect/circle/ellipse/line/polyline/polygon, solid
  colors, stroke width, cap/join/miter-limit, `stroke-dasharray` /
  `stroke-dashoffset`, numeric opacity, local `<use href="#id">` references to
  paths/primitives/groups in `<defs>`, and `<defs>` gradients referenced through
  `url(#id)`. Gradients support `objectBoundingBox` and `userSpaceOnUse` units,
  pad spread, at least two ordered stops, and no inheritance. `gradientTransform`
  applies in the gradient's coordinate space; animated `translate`/uniform
  `scale` transforms bake into gradient keyframes. Radial transforms must
  preserve circular geometry and object-bounding-box radial gradients require
  square path bounds; user-space percentages are rejected because the viewport
  is not resolved at this boundary. A root `clip-path` or `mask`, or a group
  scope whose reference covers every enclosed path under one shared transform,
  lowers to the native path mask; masks require explicit user-space units and
  opaque white geometry. Distinct per-element clip scopes and richer mask paint
  remain unsupported. Root-level `filter` accepts `feGaussianBlur` and
  `feDropShadow` and lowers them to layer effects (lengths scaled by the declared
  viewport); per-element filters, filter regions and other primitives are
  rejected. Unknown CSS style declarations are rejected.
- Path-local `animateTransform` supports translate, scale and origin-centered
  rotation, and `<animate attributeName="opacity">` supports bounded `from` / `to`
  or two-item `values`, integral composition-frame timing, `fill="freeze"`, and
  linear, discrete or `calcMode="spline"` (`keySplines`, single authored
  interval mapped to per-keyframe cubic-bezier easing) interpolation. A bounded
  integer `repeatCount` (2..1000) is expanded into repeated keyframe cycles: the
  end value holds one frame before each boundary and the boundary frame restarts
  from `from`. `repeatCount="indefinite"` stays rejected because a plan has a
  finite timeline. Unsupported timing, `additive`/`accumulate="sum"`, rotation
  pivots, static ancestor or path transforms, and animation on primitives fail
  closed. Direct-child group transform and opacity animation use the bounded
  inherited native-track subset described below.
- Path-local animated paint supports solid `fill`/`stroke` colors,
  `stroke-width`, `fill-opacity`, and `stroke-opacity` through the native color
  and scalar tracks. Gradient stop color/opacity animations support only a
  synchronized two-keyframe `from`/`to` or two-item `values` pair beginning at
  zero, with one iteration and `fill="freeze"`; stop animations share duration
  and interpolation mode. Gradient coordinates stay authored, while
  `<animateTransform>` inside a gradient bakes animated `translate`/uniform
  `scale` into gradient keyframes and must share the stop-animation duration.
  Each path has a
  bounded set of nine distinct animation entries; conflicting target tracks
  fail during compilation.
- A `<g>` can animate transform and opacity directly; translate, uniform scale,
  origin-centered rotation and opacity are inherited by its path children as
  native tracks. Nested animated groups, static transforms on animated ancestry,
  and group paint animations fail closed.
- Path `d` animation accepts 2 to 256 path values with matching command
  topology, begin at zero, integral composition-frame timing, and linear or
  discrete interpolation. `keyTimes` must be strictly increasing from 0 to 1;
  unsupported topology and timing fail closed.
- Input and expansion are bounded: maximum document bytes, XML nodes, emitted
  path commands and absolute coordinate magnitude are fixed constants. Exceeding
  a bound returns a typed resource-limit error before graph construction.
- All emitted coordinates are finite and quantized to 1e-4 before conversion
  to the plan's float representation and before fingerprinting. Equivalent
  imported geometry therefore hashes from the normalized lowered commands.
- Existing inline path plans remain backward compatible. No rasterizer,
  renderer, or second asset loading path is added.

## Acceptance gates

- [x] Resolver-backed SVG plan decode lowers paths/primitives and retains
  multiple solid paint styles in source order;
  missing/escaped references fail closed. Malformed and `<text>` loader tests pass.
- [x] Geometry fixtures cover path commands, arcs, transforms, document order,
  and 1e-4 quantization.
- [x] Resource-limit fixtures cover 8 MiB input, 100k XML nodes, 65536 path
  commands, and out-of-bound coordinates.
- [x] The SVG source digest enters the prepared manifest; normalized lowered
  commands enter the content fingerprint. Unit coverage verifies plan fingerprint behavior.
- [x] End-to-end CLI plan render produces a visible 64x64 PNG; decoding it to
  tightly packed RGBA and running `tools/raw_frame_hash.py` produced frame hash
  `4fe7b59af6de3b665b67788cc2f99892ab827efae3a467342b3bb4e3bc8e5bfe` twice.
- [x] Authored integration coverage checks sibling-path order, fill/stroke,
  inherited opacity, missing assets, local `<use>` expansion, root clip/mask
  lowering, and rejection of text, unresolved gradients, unsupported stroke attributes, and invalid
  opacity values.
- [x] Focused SVG importer tests pass after the current import extensions
  (21/21 cases, 116/116 assertions), including geometry, timing, local
  references and bounded failure behavior. The complete C ABI suite with SVG
  enabled passes (246/246 cases, 6296/6296 assertions;
  executable:
  `build/chronon/linux-fast-dev/tests/chronon3d_c_abi_tests`).
- [x] Gradient integration coverage verifies linear/radial lowering and stop
  opacity and static normalized `gradientTransform`. Focused resolver-backed SVG
  C ABI coverage passes.
- [x] The complete C ABI suite passes after the gradient, animated paint,
  `<use>`, clip, mask and `gradientTransform` changes
  (246/246 cases, 6296/6296 assertions).
- [x] Sampled SVG integration coverage exercises animated fill/stroke colors,
  stroke width, fill/stroke opacity alongside transform and overall opacity.
- [x] Resolver-backed coverage checks `<defs>` path expansion through local
  `<use>`, style inheritance and x/y placement; reference expansion is bounded
  by depth and instance count.
- [x] Root-level `clipPath` and path-scoped opaque-white `mask` definitions
  lower to the native path mask; integration coverage confirms both.
- [x] Group translate and opacity animations sample across child paths at
  start/mid/end frames through the same native imported-path tracks.
- [x] Path `d` morph coverage samples a three-value sequence at start, midpoint
  and end, and rejects topology changes.
- [x] Resolver-backed SVG coverage samples synchronized gradient stop-color
  and stop-opacity animation at start, midpoint and end through the native
  gradient animation track.
- [x] An SVG-enabled software CLI gate is implemented to validate and render a
  resolver-backed animated SVG fixture through the RenderPlan CLI, then verify
  decoded frame hashes differ at authored frames 0 and 30
  (`scripts/ci/check_svg_cli_frame_gate.py`, `golden/svg_cli_gate/`). Verified
  on the `CHRONON3D_ENABLE_SVG=ON` build: `SVG_CLI_GATE PASS`, frame 0 and 30
  hashes differ.
- [x] Run the SVG CLI gate and importer test suite on the current tree with
  `CHRONON3D_ENABLE_SVG=ON`; clean-SHA CI remains separately required.
  Local run: gate passes; `chronon3d_svg_path_loader_tests` 23/23 cases and
  `chronon3d_c_abi_tests` 293/293 cases pass. Clean-SHA CI is still outstanding.
- [x] `userSpaceOnUse` gradients, animated `gradientTransform`, `calcMode="spline"`
  with `keySplines`, bounded `repeatCount`, multi-path group clip/mask scopes and
  root-level `feGaussianBlur`/`feDropShadow` filters are covered by loader and
  resolver-backed compiler tests.
- [x] Latest local run on the SVG-enabled tree: `chronon3d_svg_path_loader_tests`
  36/36 cases (203 assertions), `chronon3d_c_abi_tests` 300/300 cases
  (7720 assertions), `check_svg_cli_frame_gate.py` PASS and
  `check_svg_motion_animations_gate.py` PASS. Clean-SHA CI still outstanding.
- [x] Composite path-operator showcase gate (`scripts/ci/check_svg_composite_showcase_gate.py`)
  certifies the canonical CLI render route end to end: six chained path
  operators (trim, zig_zag, offset_path, pucker_bloat, round_corners and an
  animated repeater). Observed run: all authored frames decode to visible RGBA
  pixels, endpoint hashes differ, a repeat render reproduces identical frame
  hashes (determinism), and the MP4 output is h264 128x128 with 31 frames
  (`COMPOSITE_SHOWCASE_GATE PASS`).
- [x] Observed motion gate details: `check_svg_motion_animations_gate.py`
  certifies line reveal, icon morph, animated stroke, group hierarchy, mask
  reveal, color/gradient and the animated repeater (17 PASS checks, endpoint
  hashes differ per operator) through the SVG-enabled CLI.

## Bounded out for architectural reasons

These are not importer gaps; they need work outside the SVG lowering boundary.

- **SVG text `<text>`/`<tspan>`/`<image>`/`<style>`.** The SVG layer lowers into
  `ImportedSubpathPlan` geometry only. `LayerPlan::TextBinding` carries a
  `key`/`text` pair for plan bindings, not a positioned text run, and there is no
  font-family/layout contract from an SVG asset to the text pipeline. The loader
  rejects these tags with an explicit message; adding text needs a text-run plan
  element plus a font resolution contract.
- **Isolated group compositing.** CLOSED at the software lane since
  2026-10-17 by [TICKET-SVG-GROUP-ISOLATION](TICKET-SVG-GROUP-ISOLATION.md)
  (ADR-035): `opacity < 1` groups lower through `svg_group` and compose once
  on an intermediate surface inside the layer's `MultiSourceNode`; native
  lane fails closed, Q2 nested groups stay pending on the same machinery.
- **Per-element filters and clip/mask.** `LayerPlan` exposes one path mask and one
  effect stack per layer; effects are layer post-transform stages with no
  per-subpath equivalent. Distinct per-element references therefore fail closed
  with typed diagnostics instead of rendering partially.
- **Arbitrary filter graphs.** Only `feGaussianBlur` and `feDropShadow` map onto
  existing layer effects; filter regions (`x`/`y`/`width`/`height`,
  `filterUnits`) are ignored, and primitives such as `feBlend`/`feTurbulence`
  have no plan representation.
- **SMIL `additive`/`accumulate="sum"`.** The lowered tracks are absolute;
  additive deltas would need a base-value contract per animated property.
  `repeatCount="indefinite"` is rejected because a plan has a finite timeline.

## Demolition debt

- **Closed on:** source SHA recorded by the implementation and gate commits.
- **Closure:** gradient and multi-style importer gates pass; the SVG feature
  scope remains bounded as stated above.
