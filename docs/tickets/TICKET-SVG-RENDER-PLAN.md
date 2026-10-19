# TICKET-SVG-RENDER-PLAN — Resolve SVG assets into shape plans

## Stato
IN PROGRESS — bounded SVG geometry and static object-bounding-box linear and
radial gradients lower through native Chronon paints. Path-local SMIL
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
- SVG `<text>` and `<tspan>` runs are lowered to canonical Chronon text layers.
  Font lookup uses exact bundled files under `fonts/`, currently accepting
  `<Family>-Regular.ttf|otf` and `<Family>-Bold.ttf|otf`; missing families fail
  with `svg_font_unresolved`. The importer does not invoke host fontconfig.
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
  unsupported. Static filters, patterns, and unknown CSS declarations are
  rejected. Stroke caps, joins, miter limits, and dash arrays are preserved.
  Static `display:none` suppresses its full subtree. `visibility:hidden` and
  `collapse` are inherited, and a descendant may restore `visibility:visible`.
  Group opacity is flattened into child paint alpha, so SVG isolated-group
  compositing is not represented. SVG text runs currently have limited
  positioned-tspan fidelity; see Phase 4.
- Path-local `animateTransform` supports translate, scale and origin-centered
  rotation, and `<animate attributeName="opacity">` supports bounded `from` / `to`
  or two-item `values`, integral composition-frame timing, one iteration,
  `fill="freeze"`, and linear/discrete interpolation. Unsupported timing,
  repeated animation, additive transforms, rotation pivots, static ancestor or
  path transforms, and animation on groups or primitives fail closed.
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
  missing/escaped references fail closed. Malformed SVG and text-run loader
  tests pass.
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
  (25/25 cases, 168/168 assertions), including geometry, timing, local
  references and bounded failure behavior. The complete C ABI suite with SVG
  enabled passes (273/273 cases, 6820/6820 assertions;
  executable:
  `build/chronon/linux-fast-dev/tests/chronon3d_c_abi_tests`).
- [x] Gradient integration coverage verifies linear/radial lowering and stop
  opacity and static normalized `gradientTransform`. Focused resolver-backed SVG
  C ABI coverage passes.
- [x] The complete C ABI suite passes after the gradient, animated paint,
  `<use>`, clip, mask, `gradientTransform`, element addressing validation,
  and font-metric text layout changes (273/273 cases, 6820/6820 assertions at
  that implementation milestone).
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

## Demolition debt

- **Closed on:** source SHA recorded by the implementation and gate commits.
- **Closure:** gradient and multi-style importer gates pass; the SVG feature
  scope remains bounded as stated above.

## Chronon SVG Motion Graphics roadmap

This roadmap targets a headless deterministic renderer and authored motion
graphics. It does not add an SVG editor or a second runtime scene graph. SVG is
parsed into a temporary import representation, then lowered into RenderPlan V3
and the existing compiler, sampler, and renderer.

### Phase 0 — Pixel-truth baseline

- [x] Keep the SVG CLI gate on the encoded video route. A still/PNG-sequence
  output is not pixel-truth for this gate.
- [x] Render an inline control shape and a resolver-backed animated SVG in the
  same plan; decode the MP4 and assert both color classes are visible, distinct
  authored endpoints, and repeatable frame hashes across two renders.
- [x] Record the tested checkout SHA, plan/SVG hashes, backend, dimensions, frame
  rate, codec, decoder, and decoded opening/middle/ending frame hashes in the
  showcase README. A green local run does not replace clean-SHA CI.

### Phase 1 — Addressable SVG elements

- [x] Preserve source element IDs and group/use ancestry in the temporary import
  representation and the decoded RenderPlan.
- [x] Add bounded `element_animations` bindings by target ID and canonical
  property path. Resolve group targets to their descendants during lowering.
- [x] Lower bindings to existing `AnimationTrackPlan` tracks; do not create a
  second sampler or timeline.
- [x] Cover missing IDs, duplicate IDs, ambiguous `<use>` instances, unknown
  targets, unsupported properties, fingerprints, and sampled frame values.
  Unsupported properties fail both JSON decoding and direct in-memory plan
  compilation; duplicate source IDs fail SVG import before target resolution.

### Phase 2 — Existing path operators

- [x] Certify the currently supported path operators through decode,
  validation, fingerprint, sampling, path-operator evaluation, and encoded
  rendered output.
- [x] Enable target-scoped Trim Paths and animated Repeater on imported SVG
  elements, preserving source paint order and operator order.
- [x] Connect target-scoped Offset Paths and Wiggle Paths on imported SVG
  elements. Resolver-backed C ABI coverage checks the offset path coordinates,
  deterministic seeded wiggle output, and animated group repeater together.
- [x] Certify Trim Paths, animated Repeater, Offset Paths, and Wiggle Paths
  through encoded MP4 output. Pixel checks verify visible operator-targeted
  paths, compare Offset/Wiggle renders to plans with only that operator
  removed, and check distinct endpoints plus matching repeat-render hashes.
- [x] Extend encoded MP4 certification to Round Corners, Zig Zag, Pucker/Bloat,
  Twist, Morph, and Merge. The gate checks operator-target pixels, renders
  operator-removed variants, checks animated Trim/Repeater pixel changes,
  compares opening/ending hashes, and repeats the complete render.
- [x] Sample imported SVG Trim Paths at frames 0/15/30/45/60 through the
  canonical software raw sink. Pixel coverage is empty at frame 0, grows at
  frames 30 and 45, and holds at frame 60; two complete 61-frame renders match
  pixel-for-pixel (`scripts/ci/check_svg_trim_sampled_raw_gate.py`).
- [x] Lower targeted SVG Morph through the existing path-operator evaluator
  and shared timeline. Exact path IDs are required; topology mismatch fails at
  evaluation. C ABI coverage samples start/mid/end and checks the mismatch.
- [x] Lower targeted SVG Merge through the existing path-operator evaluator.
  Its bounded semantics append compound subpaths; exact path IDs are required
  and group targets fail. This does not claim SVG boolean or After Effects
  Merge Paths parity.

### Phase 3 — High-value SVG fidelity

- [x] Implement `fill-rule`, stroke caps/joins/miter limit, and dash arrays via
  existing path paint structures.
- [x] Add only the CSS cascade needed by authored assets: presentation
  attributes, `<style>` rules, classes, inheritance, and `currentColor`.
- [x] Extend clip and mask scope only when it maps to a canonical per-element or
  group compositing contract. Reject unsupported scopes explicitly.
- [x] Honor static `display:none` and inherited `visibility:hidden|collapse`
  for SVG geometry and text; allow a child to restore `visibility:visible`,
  while a `display:none` ancestor suppresses all descendants.
- [x] Reuse Chronon effects when SVG filter semantics map directly; defer
  arbitrary SVG filter graphs. The importer lowers one root-level,
  isotropic `<feGaussianBlur>` over path-only `SourceGraphic` into the existing
  native Gaussian blur effect, including isotropic root/viewBox scaling. Unsupported
  filter scopes, anisotropic deviation, custom regions, chaining, and other
  primitives fail closed. Chronon's existing blur implementation is an
  approximation of the SVG Gaussian kernel, so pixel-exact browser parity is
  not claimed.

### Phase 4 — SVG text and timeline

- [ ] Complete SVG text parity through the existing shaping, font resolver,
  and text renderer. The initial `<text>`/`<tspan>` lowering now creates
  canonical Chronon text layers and resolves exact family names against
  `fonts/<Family>-Regular|Bold.ttf|otf`; missing bundled families fail with
  `svg_font_unresolved` rather than silently producing empty glyphs.
- [x] Lay out sequential text and style runs with the resolved font's measured
  advances and ascent; preserve trailing whitespace, distinguish explicit
  `x`/`y` from inherited coordinates, apply `dx`/`dy`, and retain text element
  identity. C ABI coverage verifies the next run starts after the measured
  first run.
- [x] Import solid SVG text strokes and preserve separate fill/stroke opacity
  through RenderPlan into the canonical `TextRunShape` paint. C ABI coverage
  verifies stroke color, width, alpha, and fill alpha after materialization.
- [x] Preserve SVG text stroke dash, cap, join, miter limit, and dash offset
  through import, RenderPlan materialization, and the software outline raster.
  Non-default styles select the software fallback from MTSDF; importer and C ABI
  coverage verifies retained values. Blend2D's context dash setters are a no-op
  in the bundled build, so text glyph contours are flattened and split with
  Chronon's existing path dash sampler before software stroking. The raw-pixel
  gate confirms `stroke-dasharray="1 18"` reduces visible stroke pixels from
  2674 to 119 and repeated dashed output is identical.
- [ ] Add gradient/pattern text paint.
- [x] Add font-metric `dominant-baseline` support for central, middle and
  hanging baselines, and inherited `baseline-shift` values for sub/super,
  pixel, em and percentage inputs. The current sub/super values use bounded
  typographic approximations.
- [x] Accept all twelve SVG 1.1 `dominant-baseline` identifiers; derive the
  ideographic, mathematical, before-edge, and after-edge positions from the
  resolved font's descent, cap height, and ascent. The importer regression
  checks all identifiers; non-Latin baseline tables still need script-aware
  font baseline data and remain part of the open text parity item.
- [x] Apply supported inheritable text paint recursively through nested
  `<tspan>` chunks and restore the parent's style after each child. Importer
  coverage checks fill, stroke, stroke opacity, overall opacity, font size, and
  source identity over three nested levels.
- [ ] Cover the remaining nested SVG text-chunk semantics and script-aware
  baseline table values across all supported text content.
- [x] Import bounded `x`, `y`, `dx`, and `dy` coordinate lists and lower them
  through shaped text clusters into canonical text layers. Coordinate entries
  within one shaped cluster fail closed; text coordinate lists under rotated
  or skewed SVG transforms fail closed until the text basis is preserved.
- [x] Preserve nested `<tspan text-anchor>` chunk scope and measure each
  independently anchored span with the existing font metrics during lowering.
- [x] Lower bounded SVG `textPath` through the existing path sampler and text shaping pipeline. Local references to visible static paths in the same group coordinate scope and direct paths in `<defs>` are supported, with numeric/percentage `startOffset`, `text-anchor`, and per-cluster canonical text layers retaining the text element ID. Unsupported `method`, `spacing`, and `side` values fail closed.
- [x] Prefer Chronon frame tracks for authored motion. Add SMIL timing forms
  only when real assets require them; do not pursue browser-complete SMIL.
  Authored element motion lowers to the shared Chronon `AnimationTrackPlan`
  timeline (Phases 1–2); the bounded SMIL forms documented above exist only
  for imported SVG assets and are sampled on that same timeline.

The current text lowering is an initial supported subset. Each run remains a
separate Chronon text layer; simple sequential runs now use measured advances,
but nested text chunks, complex baselines, and full paint
  semantics remain open. Text layout is covered by a C ABI integration
test, and whitespace/coordinate inheritance by the SVG loader test. Current
`textPath` limits: targets must be static (visible paths must share the same
group coordinate scope; paths inside `<defs>` use their own transform), only
numeric/percentage `startOffset` and default `method=align`, `spacing=exact`,
`side=left` are accepted, and each shaped cluster becomes an independent text
layer. Cross-cluster script shaping is therefore not guaranteed.
For ordinary positioned text, coordinate lists use shaped-cluster indices;
lists that place a coordinate inside a multi-character shaped cluster are
rejected with `svg_text_coordinate_cluster_unsupported`. TextPath coordinate
lists remain unsupported. Text coordinate lists support translation and
axis-aligned scale transforms; rotated and skewed scopes fail closed.

### Phase 5 — Cinematic acceptance scene

- [x] Author one realistic company/finance infographic SVG with stable IDs for
  logo, nodes, arrows, connectors, and labels.
- [x] Render a 10–15 second scene with ID-addressed reveals, an animated
  Repeater, supported path morphs, synchronized typography, 2.5D depth, and
  camera movement on the shared composition timeline.
- [x] Deliver MP4, contact sheet, editable RenderPlan/source SVG, assets and
  attribution, plus opening/middle/ending frame checks.
- [x] Certify deterministic full-showcase reruns through the in-process FFmpeg
  pipe encoder.
- [ ] Run the full gate on a clean-SHA CI checkout.

### Current checkout notes

- The current working tree contains the SVG parity changes under review. Do
  not reset or stage unrelated files as part of this roadmap.
- The old CLI PNG gate reported transparent output for both SVG and an inline
  control shape. The MP4 gate verifies the red control and yellow SVG pixels,
  an animated cyan Trim Paths reveal, increasing magenta Repeater coverage,
  green Offset Path and blue Wiggle Path output, and compares output with each
  of those operators removed in isolation. It also checks distinct frame 0/10
  hashes and identical hashes across two complete renders. The gate passed
  locally with the isolated SVG-enabled build. Frame 0 SHA-256 is
  `64e27e2e3feb51bb812a2749e23fc503e063dddf1cd31f9885e76d5f5553cd0a`; frame
  10 is `fee541ecb7d8a4900307076a94b3cd8271c8ec23fce5f11e47ecc1bd98c2bbb2`.
  Clean-SHA CI remains pending.
- Two current-tree 1920×1080 renders through the in-process FFmpeg pipe encoder
  were decoded and compared across all 360 frames: all RGBA frames match
  exactly, and both MP4 files have SHA-256
  `9bd9f2421b12ae56a69b452d6b7f2f56edc426c87aa822763f6d41db3204efbe`.
  Independent raw-sink runs of frames 0–59 also match exactly. The previous
  default native-encoder full renders differed only in decoded frames 7–59;
  use `--encoder-backend pipe` for reproducible showcase output. The contact
  sheet and review frames were regenerated from the passing run and visually
  inspected. Clean-SHA CI remains pending.
- Focused SVG verification passes in the isolated Linux build: both
  `chronon3d_svg_path_loader_tests` and `chronon3d_c_abi_tests` passed after
  adding root Gaussian blur lowering. The temporary build copy required a
  test-only compatibility shim for the checkout's unrelated missing
  `DepthOfFieldSettings::iris_blades` and `highlight_gain` fields; production
  sources were not changed for that mismatch.
- Build directories from the previous checkout contain stale CMake cache paths;
  use an isolated build directory and preserve the existing preset artifacts.

### Showcase scene contract

- **Subject and assets:** a fictional company growth infographic with a central
  headquarters mark, three business nodes, connector lines, a revenue arrow,
  numeric callout, and editable text labels. The SVG is authored in-repo with
  stable IDs; the RenderPlan supplies the camera, typography, and timeline.
- **Composition and camera:** 1920×1080, dark navy field, central diagram in
  the middle two thirds, title and metric in the safe upper/lower bands. Use the
  canonical perspective camera convention from existing RenderPlan fixtures;
  keep graphic planes at readable depth and avoid perspective distortion.
- **Motion and timing:** 12 seconds at 30 fps. Establish title and hub, reveal
  connectors with targeted Trim Paths, bring in business nodes with a scoped
  Repeater, then animate the revenue arrow and metric. End on a settled hold.
  Camera motion is a restrained push-in on the shared composition timeline.
- **Layers and sources:** independent SVG-backed layers for the diagram and
  arrow, editable RenderPlan text layers for title, labels, and metric, and a
  generated background layer. Preserve the source SVG and plan next to the MP4.
- **Must not appear:** browser/SVG editor UI, unrequested brands or logos,
  unreadable text, clipped objects, or a substitution for the authored diagram.
- **Preview acceptance:** at 854×480, the first, middle, and final states must
  show the same authored diagram, legible title and metric, visible progressive
  connector/revenue reveals, and no missing/transparent subject. Final delivery
  also includes decoded contact-sheet frames and repeatable endpoint hashes.

### Chronon SVG animation contract

Element tracks use stable imported SVG IDs (group IDs address descendants) and
the existing animation track decoder/sampler. A path operator can optionally
use `target` to scope it to one SVG element or group. Exact element IDs take
precedence over group IDs; duplicate exact IDs after `<use>` expansion are an
explicit ambiguity error. Example:

```json
{
  "shape": {
    "type": "path",
    "asset": "art/company.svg",
    "element_animations": [{
      "target": "revenue_arrow",
      "property": "opacity",
      "animation": {"keyframes": [
        {"frame": 0, "value": [0]},
        {"frame": 24, "value": [1]}
      ]}
    }],
    "operators": [{
      "kind": "trim",
      "target": "revenue_arrow",
      "params": {"start": 0, "end": 1,
        "animation": {"keyframes": [
          {"frame": 0, "value": [0, 0]},
          {"frame": 24, "value": [0, 1]}
        ]}}
    }]
  }
}
```

Supported element properties currently cover overall opacity, translate,
scale, rotation, solid fill/stroke color and opacity, and stroke width. Imported
path operators share the existing Chronon utility and can target SVG elements;
Merge and Morph require an exact target path ID within an imported SVG asset.
SVG merge is subpath concatenation, not a boolean geometry operation.

## Verification update — 2026-10-18

- Full SVG-enabled C ABI suite on the current working tree: 275/275 cases and
  6865/6865 assertions pass (including positioned text cluster coordinates,
  baselines, textPath, and materialized text paint checks).
- SVG path loader suite before the display/visibility change: 28/28 cases and
  197/197 assertions passed. The updated suite passes 30/30 cases and 205/205
  assertions, including inherited geometry and text visibility plus descendant
  visibility restoration.
- Encoded CLI gate passes on the software backend. Decoded 64×64 RGBA endpoint
  hashes: frame 0 `6b232d3f71d8ccef3a409bd3830d0aac83f18907922fd54e96ed5ca2a00fc083`,
  frame 10 `027e5a800091544efb3437baebcdd771f6a0bae1bfe5e07d518dbfdc574c6f69`;
  two complete gate renders match.
- The 1920×1080, 30 fps, 12 second company-growth showcase was freshly rendered
  from `tests/fixtures/svg_motion_showcase/scene.plan.json` and visually checked
  at frames 12, 180 and 359. Their decoded PNG SHA-256 values are respectively
  `348d551abda64ac270b56b9e401f65192d7fc24d1d55db9665215ff815f8bf51`,
  `845e2ad53305420577e7157f8366c259856789658b46e66bd549fa69b86b5285`, and
  `ea029e7f1ba2ab27cd789ede2b1f66e5dd143d527477bd54f6aea6fd7597350e`.
  It is delivered as
  `Chronon3D_SVG_Motion_Showcase_1080p30.mp4` with a contact sheet in the
  requested `Altri_video` folder.
- Remaining scope: arbitrary SVG filter graphs, nested text chunks and full
  baseline semantics, complete text paint, coordinate lists that split a
  shaping cluster or occur on textPath/rotated SVG scopes, broader clip/mask
  composition and clean-SHA CI. These are not claimed complete.

## User-provided documentary SVG video acceptance — 2026-10-19

The reported reference is 19.63 seconds at 30 fps (approximately 589 frames).
Its requested peak-graph samples are 3.0/3.5/4.0/4.5/6.0 seconds and
15.0/15.5/16.0/17.5/18.5 seconds. In the latter section, marker motion and
overall framing/scale changes are separate requirements from path trim.
Illustrative linked images are not pixel references. The uploaded source video
is not present in this workspace, so reference matching and a scene preview
approval cannot be completed from the current checkout.

| Gate | Current evidence | Status for the supplied video |
| --- | --- | --- |
| SVG-01 static pixels | Encoded showcase plus SVG peak study verify visible imported path pixels; contact sheet is reviewed. | Generic behavior covered; reference shot still open. |
| SVG-02 trim at f0/15/30/45/60 | `check_svg_trim_sampled_raw_gate.py` measures imported-SVG pixel coverage 0/0/67/96/96 and repeat equality. Five-second peak study additionally measures 0/216/703/1147/1590/1591/1591 at f0/15/30/45/60/90/149. | Generic trim and composed hold covered; reference shot and its 3.0–6.0s timing remain open. |
| SVG-03 marker pixels | `svg_peak_pixel_gate.py` checks isolated and combined SVG+marker centroids; isolated maximum error is 0.735 px. It also reproduces the unguarded sequential path loss (132 pixels at f0–45, 6 at f60) and verifies workaround coverage. The 150-frame study visually confirms travel. | Marker behavior covered with workaround; the underlying cause is unconfirmed (disabling composite input-storage transfer did not change it), and target framing remains open. |
| SVG-04 glow | `svg_peak_pixel_gate.py` compares combined renders with/without native glow: 602 changed halo pixels, 0 changed pixels at the prior marker position, and deterministic repeat. | Synthetic moving-marker behavior passes; target-video halo and alpha behavior remain open. |
| SVG-05 transforms/pivots | Imported transform tracks and camera showcase exist. | Vehicle pivot and target shot scale/push are open. |
| SVG-06 temporal stability | Repeat showcase output has matching decoded frame hashes. | Target-video thin-line flicker review open. |
| SVG-07 vector plus footage | A plan-level vector/footage composition fixture exists. | Encoded compositing with the actual plate is open. |
| SVG-08 determinism | Full 360-frame showcase reruns match decoded pixels; synthetic gate also repeats raw RGBA samples. | Generic behavior covered; target plan open. |
| SVG-09 CPU/GPU parity | Capability-dependent Vulkan lane has not been run for this deliverable. | Open where Vulkan is supported. |

The deliverable-specific scene, reference timeline, source-clip dependency,
and remaining acceptance checklist are tracked separately in
[`TICKET-SVG-DOCUMENTARY-VIDEO-PARITY.md`](TICKET-SVG-DOCUMENTARY-VIDEO-PARITY.md).

The supplied analysis also asks for an SVG source asset with stable IDs and a
reusable `documentary_svg_peak_v1` template that includes the observed graph
framing and moving markers. The existing `reconstruct_peak_diagrams.py` still
authors inline path geometry, and its peak marker is static; the existing
showcase is a different 12-second scene. Do not treat either artifact as the
requested preset or claim exact-video completion. Once the source clip is
available, establish the scene contract, inspect the referenced frames, render
and inspect the 854×480 composition preview, then implement and gate the
video-specific preset. The SVG text stroke cap/join/miter/dash implementation
is included in the current working tree; software rendering supports these
styles and the MTSDF path falls back to software for non-default strokes.
