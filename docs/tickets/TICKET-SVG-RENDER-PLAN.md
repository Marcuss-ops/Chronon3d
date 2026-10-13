# TICKET-SVG-RENDER-PLAN — Resolve SVG assets into shape plans

## Stato
IN PROGRESS — the SVG-enabled CLI endpoint-frame gate is wired but has not yet
passed: current CLI render output is transparent even for a non-SVG control.
Compiler sampling coverage for animated path/transform subsets exists; broader
animated-importer and same-SHA certification work remains open. Bounded
SVG geometry and static object-bounding-box linear and radial gradients lower
through native Chronon paints. Path-local SMIL
`animateTransform` (translate/scale/rotate), opacity and solid paint animations
lower to sampled native tracks. Local geometry references through `<defs>/<use>` are
expanded at compile time. Static clip paths and opaque white vector masks on
the root, or on a single path/group scope, lower to the native path-mask plan.
Direct-child group translate/uniform
scale/rotate and opacity animations fan out to child path tracks. Two-keyframe
`stop-color` and `stop-opacity` SMIL animations on gradient stops lower to native
gradient animation tracks. Per-element clipping, complex masks, nested transform
stacks, animated gradient coordinates and multi-keyframe gradient-stop animation
remain open.

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
  limited to `svg`, `g`, path/rect/circle/ellipse/line/polyline/polygon and
  solid colors, stroke width, numeric opacity, local `<use href="#id">`
  references to paths/primitives/groups in `<defs>`, and `<defs>` gradients
  referenced through `url(#id)`. Gradients currently require objectBoundingBox
  units, pad spread, at least two ordered stops, and no inheritance. Static
  `gradientTransform` is applied to normalized coordinates; radial transforms
  must preserve circular geometry and radial gradients require square path bounds.
  A root `clip-path` or `mask`, or a single-path group/path scope, can refer to
  one path/primitive in `<defs>`; masks require explicit user-space units and
  opaque white geometry. Mixed per-element clip scopes and richer mask paint remain
  unsupported. Filters, dashes, alternate caps/joins, and unknown CSS style
  declarations are rejected. Group opacity is flattened into child paint
  alpha, so SVG isolated-group compositing is not represented.
- Path-local `animateTransform` supports translate, scale and origin-centered
  rotation, and `<animate attributeName="opacity">` supports bounded `from` / `to`
  or two-item `values`, integral composition-frame timing, one iteration,
  `fill="freeze"`, and linear/discrete interpolation. Unsupported timing,
  repeated animation, additive transforms, rotation pivots, static ancestor or
  path transforms, and animation on primitives fail closed. Direct-child group
  transform and opacity animation use the bounded inherited native-track subset
  described below.
- Path-local animated paint supports solid `fill`/`stroke` colors,
  `stroke-width`, `fill-opacity`, and `stroke-opacity` through the native color
  and scalar tracks. Gradient stop color/opacity animations support only a
  synchronized two-keyframe `from`/`to` or two-item `values` pair beginning at
  zero, with one iteration and `fill="freeze"`; stop animations share duration
  and interpolation mode. Gradient coordinates and animated transforms remain
  static. Each path has a
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
- [ ] An SVG-enabled software CLI gate is implemented to validate and render a
  resolver-backed animated SVG fixture through the RenderPlan CLI, then verify
  decoded frame hashes differ at authored frames 0 and 30. The gate implementation
  is pending: current CLI renders transparent frames for SVG and inline-path
  controls, and this runner cannot link the SVG-on build within its time limit.
  It must not be treated as end-to-end evidence yet
  (`scripts/ci/check_svg_cli_frame_gate.py`, `golden/svg_cli_gate/`).
- [ ] Run the SVG CLI gate and importer test suite on the current tree with
  `CHRONON3D_ENABLE_SVG=ON`; clean-SHA CI remains separately required.
  The source and fixture are in place, but the gate has not yet passed.

## Demolition debt

- **Closed on:** source SHA recorded by the implementation and gate commits.
- **Closure:** gradient and multi-style importer gates pass; the SVG feature
  scope remains bounded as stated above.
