# TICKET-SVG-RENDER-PLAN — Resolve SVG assets into shape plans

## Stato
SOFTWARE PARITY COMPLETE for the bounded SVG importer contract below. The
software lane supports nested animated transform/opacity scopes, isolated group
compositing, per-element clip/mask scopes, styled text chunks, local text
gradients and patterns, strokes, textPath, and transformed text coordinates.
Strict-native Vulkan hardware certification remains with the user. Arbitrary
filter graphs and browser-complete SMIL timing are outside this bounded
importer contract.

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
  opaque white geometry. Distinct inherited clip/mask scopes are intersected
  per element. Rich mask paint remains unsupported. Text patterns currently
  require explicit `userSpaceOnUse` tile/content units, integer tile dimensions
  from 1 to 256, no href inheritance, and solid-color path/primitive tile
  content (`path`, `rect`, `circle`, `ellipse`, `line`, `polyline`, or `polygon`).
  Each tile is bounded to 32 shapes and 1024 path commands per shape; child
  transforms, fill-rule, opacity and fill-opacity are applied. Group tile
  content, child strokes, object-bounding-box units, and inherited patterns
  remain unsupported. Static filters and unknown CSS declarations are rejected.
  Stroke caps, joins, miter limits, and dash arrays are preserved.
  Static `display:none` suppresses its full subtree. `visibility:hidden` and
  `collapse` are inherited, and a descendant may restore `visibility:visible`.
  Nested opacity-bearing groups are isolated through ADR-035, up to depth 16;
  each opacity is applied once after that scope's child compositing. SVG text
  runs have bounded positioned-tspan fidelity; see Phase 4. Text spacing,
  character rotation, and `textLength` follow the bounded cases listed in
  Phase 4. Vertical writing is rejected; nested textLength/rotate scopes and
  textPath length adjustment remain unsupported and fail closed.
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
- A `<g>` can animate translate, uniform scale, origin-centered rotation and
  opacity directly; ordered ancestor/descendant tracks compose on child paths.
  Static transforms on animated ancestry and group paint animations fail closed.
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
- [x] End-to-end encoded CLI gate verifies visible 64×64 content, changing
  endpoints and repeat-render equality. The old PNG hash `4fe7b59a…` was an
  empty-surface hash, not evidence of visible SVG; PNG is not pixel truth.
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

- [x] Complete bounded SVG text parity through the existing shaping, font
  resolver, and text renderer. The `<text>`/`<tspan>` lowering creates
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
- [x] Carry local linear/radial SVG gradient fills and strokes into TextPaint
  and the software Blend2D glyph renderer. Raw-pixel checks verify direction,
  visibility and deterministic repeats for fill and stroke gradients.
- [x] Complete bounded SVG pattern text paint. The software path carries immutable
  RGBA tiles through TextPaint and uses Blend2D repeat patterns for fill and
  stroke. The importer accepts bounded integer-pixel tiles with explicit
  `userSpaceOnUse`, no inheritance, and solid path/primitive children, including
  child transforms and fill-rule. The raw gate covers fill, stroke, path and
  rect paint, translucency, and deterministic repeats. Group children, gradient
  or stroke paint inside tiles, objectBoundingBox units, and href inheritance
  fail closed by contract; browser-complete pattern inheritance is out of scope.
- [x] Add font-metric `dominant-baseline` support for central, middle and
  hanging baselines, and inherited `baseline-shift` values for sub/super,
  pixel, em and percentage inputs. The current sub/super values use bounded
  typographic approximations.
- [x] Accept all twelve SVG 1.1 `dominant-baseline` identifiers and resolve
  horizontal script defaults and named baselines from a font's OpenType BASE
  table when matching records exist. Coordinates are converted to pixel
  offsets relative to the Roman baseline; when the font lacks a matching
  record, bounded ascent/descent/x-height estimates remain the fallback.
  FreeSerif's BASE table has no matching horizontal Latin or Arabic records;
  the text-health regression verifies this fallback, while the software raw
  gate covers all twelve identifiers, inherited `reset-size`/`no-change`
  behavior, named baseline placement, and deterministic output.
- [x] Apply supported inheritable text paint recursively through nested
  `<tspan>` chunks and restore the parent's style after each child. Importer
  coverage checks fill, stroke, stroke opacity, overall opacity, font size, and
  source identity over three nested levels.
- [x] Preserve paint changes inside a shaped grapheme without changing its
  geometry. The normal composed glyph remains the alpha source; a separately
  shaped mark glyph is applied as a software `SRC_ATOP` paint overlay. The raw
  gate verifies exact alpha parity, black base paint, red combining-mark paint,
  and deterministic output, alongside Latin, ligature, mixed-script and Arabic
  RTL paint chunks.
- [x] Import bounded `x`, `y`, `dx`, and `dy` coordinate lists and lower them
  through shaped text clusters into canonical text layers. Coordinate entries
  within one shaped cluster fail closed. Paired x/y and dx/dy coordinate lists
  retain both axes under rotated/skewed transforms; unpaired lists fail closed.
- [x] Preserve nested `<tspan text-anchor>` chunk scope and measure each
  independently anchored span with the existing font metrics during lowering.
- [x] Apply bounded SVG `letter-spacing` and `word-spacing` in the software
  renderer, support per-cluster `rotate` lists, and honor `textLength` with
  `lengthAdjust="spacing"` or `"spacingAndGlyphs"` on direct unpositioned text
  runs. Nested `textLength` scopes, textPath length adjustment, coordinate-list
  combinations, and nested rotate lists fail closed pending a separate scope
  implementation.
- [x] Lower bounded SVG `textPath` through the existing path sampler and text shaping pipeline. Local references to visible static paths in the same group coordinate scope and direct paths in `<defs>` are supported, with numeric/percentage `startOffset`, `text-anchor`, and per-cluster canonical text layers retaining the text element ID. `dx/dy` and absolute `x/y` lists place clusters along/perpendicular to the path; paired transformed coordinates are projected into the path basis. Unsupported `method`, `spacing`, and `side` values fail closed. Raw-pixel coverage verifies coordinate changes and deterministic repeats.
- [x] Prefer Chronon frame tracks for authored motion. Add SMIL timing forms
  only when real assets require them; do not pursue browser-complete SMIL.
  Authored element motion lowers to the shared Chronon `AnimationTrackPlan`
  timeline (Phases 1–2); the bounded SMIL forms documented above exist only
  for imported SVG assets and are sampled on that same timeline.

Text lowering preserves explicit nested style and positioning scopes.
Compatible paint-only chunks now share shaping for same-font LTR, mixed
Latin/Greek/Cyrillic, and Arabic RTL runs; the raw gate checks unchanged alpha,
per-range paint, and deterministic output. Font changes, explicit positioning
changes, and boundaries inside a shaped cluster retain their separate handling.
Text layout is covered by a C ABI integration test, and whitespace/coordinate
inheritance by the SVG loader test. Current
`textPath` limits: targets must be static (visible paths must share the same
group coordinate scope; paths inside `<defs>` use their own transform), only
numeric/percentage `startOffset` and default `method=align`, `spacing=exact`,
`side=left` are accepted, and each shaped cluster becomes an independent text
layer. Cross-cluster script shaping is therefore not guaranteed.
For ordinary positioned text, coordinate lists use shaped-cluster indices;
lists that place a coordinate inside a multi-character shaped cluster are
rejected with `svg_text_coordinate_cluster_unsupported`. Paired x/y and dx/dy
lists preserve both axes under rotated/skewed transforms. TextPath dx/dy lists
are applied along and perpendicular to the path. TextPath x/y lists reset
cluster positions by projecting absolute coordinates onto the path's start
tangent and normal; paired lists retain their two axes under rotated/skewed
transforms. Same-style adjacent text chunks now share one shaping run where
no explicit tspan scope or positioning attribute changes the chunk.

### Phase 5 — Cinematic acceptance scene

- [x] Author one realistic company/finance infographic SVG with stable IDs for
  logo, nodes, arrows, connectors, and labels.
- [x] Render a 10–15 second scene with ID-addressed reveals, an animated
  Repeater, supported path morphs, synchronized typography, 2.5D depth, and
  camera movement on the shared composition timeline.
- [x] Deliver MP4, contact sheet, editable RenderPlan/source SVG, assets and
  attribution, plus opening/middle/ending frame checks.
- [x] Local full-showcase comparison: two complete raw 360-frame renders
  match in all 360 RGBA hashes. Run B uses the corrected CLI; run A predates
  the Lottie-only fix. Same-binary clean-SHA encoded CI remains unverified.
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

## Verification update — 2026-10-19 (ADR-035 restore + raw-sink migration)

- **ADR-035 restored end to end after the integration refactor dropped the
  builder call**: the loader annotates `SvgStyleGroupRef` on every child of an
  opacity-bearing `<g>` (no more flattening into element opacity), nested
  isolated groups keep the fail-closed malformed-animation sentinel, the plan
  carries `ImportedSubpathPlan::SvgGroupIsolationPlan`, and the compiler calls
  `builder.svg_group_isolation` again at both emit points. The group-isolation
  gate passes with a pure-red background, exactly-overlapping top child
  (188,0,188,255 vs 188,0,188,255), a shared constant group factor, no fold
  double-darkening, and deterministic repeat rendering.
- The loader suite now enforces the restored contract: group opacity stays out
  of element opacity, siblings share one group id/opacity, single-element case
  preserves the element factor (0.75 × group 0.5), and nested opacity-bearing
  groups are rejected with the malformed-animation error (32 cases,
  228 assertions pass).
- **`check_svg_motion_animations_gate` migrated to the raw pre-encode sink
  (`--video-sink raw`)** because the PNG-sequence lane is not a pixel-truth
  surface (TICKET-TEXT-QUALITY-V2 P0-0b claim) — it silently skipped SVG doc
  layers. On the raw surface the six base animations certify pixel-truth
  (line reveal grows 4 → 138 → 282 alpha>100 px, morph endpoints differ,
  animated stroke thickens 192 → 480 px with viewport-correct placement,
  mask content stays in columns [0,32) and exits fully at 30,
  gradient hue rotates red → blue/green) and the nested animated groups case is
  now explicitly rejected (`SVG animation is malformed`) instead of rendering
  wrong or passing silently.
- **Repeater animation regression fixed**: Lottie import had applied static
  expansion and cleared animated operators. Animated repeater/morph operators
  now survive import and use the existing per-frame evaluator. Copies remain
  static; the five animated components are position X/Y, scale X/Y and rotation.
  Dedicated C ABI regression: 1 case / 11 assertions PASS. Strict raw gate:
  f0 `a0398c52a994…`, f30 `9871e1bc03f0…`, xmax 7 → 55.
  The temporary KNOWN DEFECT bypass has been removed: frozen endpoints fail.
  Min-repro and generator remain under `output/svg_frozen_repeater_repro/`
  and `tools/make_frozen_repeater_repro.py`. A fresh 31-frame reproduction
  now writes `repeater_fixed.dmp` with endpoint hashes and PASS recorded in
  `output/svg_frozen_repeater_repro/fixed_verification.json`.
- The motion gate checks exactly 31 complete raw frames. SVG fixtures use
  viewport placement [0,0]; centered inline geometry retains [32,32]. Group
  translation and rotation each have an ablation control, so dropping either
  transform fails the gate. Nested isolated opacity groups are additionally
  rejected through the real CLI. This does not claim nested transform-stack support.
- The encoded CLI gate remained passing end to end (endpoint hashes
  `6b232d3f71d8ccef3a409bd3830d0aac83f18907922fd54e96ed5ca2a00fc083` and
  `027e5a800091544efb3437baebcdd771f6a0bae1bfe5e07d518dbfdc574c6f69`, two full
  identical renders).
- Vulkan stroke-only draw remains failing (min-repro in
  TICKET-VULKAN-STROKE-ONLY-DRAW); software is the supported backend for
  console delivery.

### Final local acceptance and remaining SVG scope

Verified locally on the working tree based on `45561923d` (not a clean-SHA CI
certification):

| Criterion | Result / evidence |
| --- | --- |
| Motion animations | PASS, strict raw checks including repeater and transform ablations |
| Negative controls | PASS, frozen repeater fails; truncated/extra raw sequences rejected |
| Group isolation | PASS, overlap oracle + repeated mid-frame hash |
| Encoded CLI | PASS, repeat-render endpoint hashes unchanged |
| Composite showcase | PASS, all 31 raw frames identical across repeats; h264 128×128, 31 frames |
| Architecture | PASS, 206/206 rules |
| Loader | PASS, 32 cases / 228 assertions |
| Final C ABI + loader CTest | PASS, 2/2 registered suites, 253.27s; C ABI binary contains 277 cases |
| Dedicated repeater C ABI regression | PASS, 1 case / 11 assertions |
| 1080p showcase render | PASS, exactly 360 RGBA frames (2,985,984,000 bytes) |
| 1080p MP4 derived from raw | PASS, h264 1920×1080, 30/1 fps, 360 frames, 12.000 seconds |
| Independent samples 0 / 180 / 359 | PASS, each byte-identical to full-render sample |
| Complete second 360-frame render | PASS, 360/360 raw RGBA hashes identical, zero differences |

Artifacts are local under `output/svg_showcase_determinism/`: `run_a.dmp`,
`showcase_1080p30.mp4`, `contact_sheet.png`, `metadata.json`,
`sample_comparison.json`, `run_a.frame_hashes.json`, `determinism.json` and
`verification.json`. MP4 was
encoded from the raw dump with FFmpeg, not directly exported by Chronon's MP4
sink; the smaller composite gate separately covers direct Chronon encoding.

Measured full-scene debug/software render on this shared host: wall
31m08.43s, user CPU 2760.31s, system CPU 103.66s, maximum RSS 2,976,600 KiB
(2.84 GiB), approximately 0.193 rendered fps. These are observed timings,
not an isolated benchmark or a speedup claim. Run A predates the Lottie-only
operator fix; the showcase uses an SVG asset, not Lottie. Its independent
sample rerenders and complete run B use the updated CLI. Run B completed in
28m08.77s (user CPU 2606.59s, system CPU 85.48s, max RSS 2,977,868 KiB).
All 360 raw RGBA frame hashes match run A, with zero differences. Shared-host
load differs, so the timing difference is not claimed as a speedup. This is
not a same-binary clean-SHA proof.

Historical open-work snapshot at this revision (superseded by the 2026-10-20 status below):

- Q2: nested static opacity scopes and ordered nested translate/scale/rotate
  animation tracks were implemented on the software path; animated group
  opacity was still open at this snapshot.
- Q3: software per-element clip/mask scopes were implemented and pixel-gated;
  Vulkan-native execution and GPU measurements remained unverified.
- SVG filter mapping onto existing effects; arbitrary filter graphs remain out
  of the bounded importer contract.
- At this snapshot, gradient/pattern text paint, text stroke fidelity,
  baseline/chunk semantics and transformed/textPath coordinate-list support
  were not yet verified; see the current status below for completed cases.
- Broader SMIL timing only when required by real authored assets.
- Strict-native Vulkan isolation and GPU time/RAM at 1/5/10 isolated groups;
  stroke-only Vulkan defect has its own ticket.
- Full clean-SHA CI and any new Drive delivery remain unverified; no push or
  upload is implied by these local checks.

### Local audit — 2026-10-20

The following dated audit entries preserve the state observed at each checkpoint;
those snapshots are historical. The current status and superseding gate results
are recorded in the final “Software parity audit” section below.

On the current checkout, the SVG loader suite exposed a mixed-content ordering
bug: Boost.PropertyTree concatenated direct text around nested `<tspan>` nodes
into `ptree::data()`. SVG parsing now uses `no_concat_text` so the existing text
lowerer receives ordered text nodes. Focused loader coverage passes (36 cases,
279 assertions), as do the SVG-enabled C ABI and renderer-core suites. Raw
pixel gates pass for motion/Repeater, isolated-group composition, Trim Paths,
static SVG composition, and text stroke style/determinism.

The encoded CLI and MP4 showcase gates could not complete on this host: its
FFmpeg headers expose `AV_PROFILE_H264_*`, while the native encoder source uses
`FF_PROFILE_H264_*`. The SVG raw sink works with native FFmpeg disabled, but
that configuration cannot exercise Chronon's encoded MP4 sink. Vulkan is also
disabled in this software-only build. The software implementation and raw
gates listed in the 2026-10-20 status below cover nested animated transforms,
element-specific clip/mask scopes, bounded styled text chunks, gradients,
rect-tile patterns, strokes, textPath, and transformed coordinate lists.
Remaining non-Vulkan SVG text gaps are broader pattern semantics, paint
boundaries inside a shaped grapheme, and positive script-specific OpenType BASE
record verification. Arbitrary filter graphs and broader SMIL remain outside
the bounded importer contract;
this audit does not claim full SVG 1.1/browser parity.

### Nested static group opacity — 2026-10-20

Implemented an ordered opacity-scope chain from the SVG loader through the
render plan, scene node, and multi-source compositor. The software compositor
uses nested isolated surfaces and closes them from inner to outer; maximum
supported depth is 16. The legacy single-group view remains the innermost
scope for compatibility. Cache identity includes each scope.

Verification on the local `linux-video-test` software CLI:

- Build of CLI, SVG loader test and C ABI test targets: PASS.
- `ctest --test-dir build/chronon/linux-video-test -R svg_path_loader`: 1/1
  PASS.
- `chronon3d_svg_path_loader_tests`: 44/44 cases and 350/350 assertions pass;
  `chronon3d_c_abi_tests` and `chronon3d_text_health_tests` both pass through
  CTest (text health: 7 cases, 938 assertions).
- `check_svg_group_isolation_gate.py`: PASS; nested overlapping children
  produced `(231, 0, 124, 255)` at the overlap with both opacity factors.
- `check_svg_motion_animations_gate.py`: PASS; nested animated transforms
  differ at their endpoints and nested static isolation remains stable.
- `check_svg_text_gradient_raw_gate.py`: PASS; local linear gradient fill
  and stroke show the expected direction and repeat identically.
- `check_svg_text_chunk_raw_gate.py`: PASS; styled LTR `AV`, mixed-script, and
  Arabic RTL chunks preserve the plain-run alpha mask, per-range paint, and
  repeat hash. RTL paragraph composition now maps logical clusters to their
  visual glyph ranges before applying line baselines; a text-health regression
  checks the materialized Arabic glyph baselines.
- `check_svg_text_path_coordinates_raw_gate.py`: PASS; textPath dx/dy and
  x/y lists plus rotated ordinary x/y lists change pixels deterministically.
- `check_svg_text_pattern_raw_gate.py`: PASS; text fill and stroke repeat an
  8×8 multi-color tile deterministically, including a path, overlapping
  translucent rects, and repeat equality.
- `check_svg_text_spacing_raw_gate.py`: PASS; letter/word spacing and both
  bounded textLength modes change measured pixel bounds, while per-character
  rotation is deterministic.
- `check_svg_text_stroke_raw_gate.py`: PASS; solid/dashed stroke coverage
  and repeat determinism hold.
- Python gate syntax compilation and `git diff --check`: PASS.

The bounded software text parity scope is complete, including paint changes
inside a shaped grapheme and deterministic pattern fill/stroke. Pattern support
uses explicit user-space integer tiles with solid path/primitive children as
stated above. Arbitrary SVG filter graphs and broader SMIL timing remain outside
the bounded importer contract. Strict-native Vulkan hardware certification and
full clean-SHA CI remain external verification items; this software-only build
does not certify Vulkan.

### Nested animated transforms and per-element clips — 2026-10-20

The software path now composes bounded ancestor/descendant translate, scale,
and rotate animation tracks in matrix order. It also intersects each element's
own inherited clip/mask scopes, including distinct child clips and masks nested
under group scopes. The SVG motion gate, group isolation/clip/mask pixel gate, SVG loader CTest,
SVG text gradient raw gate, textPath coordinate raw gate, and SVG text stroke
raw gate pass on the local software CLI.

The bounded non-Vulkan SVG text scope is complete. Broader pattern semantics
remain outside the importer contract. Same-font
LTR, mixed-script, and Arabic RTL paint-only chunks now share shaping where
cluster boundaries permit and retain per-range solid color. The raw gate
compares alpha masks to unchunked runs and checks paint and deterministic
repeats. Local linear/radial gradient fill and stroke paint is carried through
TextPaint; the raw gate checks gradient direction and deterministic repeats.
TextPath x/y coordinates are projected onto the path basis; the raw gate checks
ordinary and rotated coordinate lists and deterministic repeats. Arbitrary
filter graphs and broader SMIL remain outside the bounded importer contract.
Vulkan strict-native checks are intentionally left to the user on a suitable
host.

### Styled SVG text chunk shaping — 2026-10-20

Adjacent text chunks with the same font, language, direction, baseline,
coordinate scope and solid-fill-only paint now lower to a shared text layer.
The canonical text shaper shapes compatible LTR runs together, then assigns
clusters to their source paint ranges. If a paint boundary falls inside a
shaped cluster, it shapes the authored runs separately to retain both paints.
The software gate
`check_svg_text_chunk_raw_gate.py` confirms zero alpha-mask differences for
`AV` versus `A<tspan fill="red">V</tspan>`, verifies the first glyph remains
black and the second red, and repeats the styled render byte-for-byte. It also
checks black `f` / red `i` paint across an `fi` ligature boundary and confirms
deterministic output. A canonical RenderPlan text layer with byte-range
`TextSpanOverride`s exercises the same ligature fallback in the shared text
compiler and retains both colors.

### RTL SVG text chunk coordinates — 2026-10-20

RTL paragraph composition now gathers glyph ranges through each logical line's
cluster mappings before applying line baselines. HarfBuzz emits RTL glyphs in
visual order, so treating the first and last logical clusters as an ascending
glyph interval skipped Arabic baseline placement and caused the graph optimizer
to cull the resulting off-canvas run. `check_svg_text_chunk_raw_gate.py` now
passes Arabic plain/chunked alpha parity, per-range paint and deterministic
repeats. The same gate verifies that a combining-mark paint boundary preserves
the shaped grapheme's geometry and deterministic output. The focused
text-health regression verifies that materialized RTL glyphs receive nonzero
line baselines. Per-code-point paint inside a shaped grapheme, broader pattern
semantics, and positive verification with a font containing matching script-specific
BASE records remain open.

The loader now treats inherited scalar `x`/`y` as a positioning scope consumed
by the first emitted text run. Text after a styled `<tspan>` continues from the
cursor; an explicit coordinate on a nested `<tspan>` starts a new scope and is
also consumed once. This fixed the `A<tspan fill="red">Ω</tspan>Ж` case, where
the parent `x` had been reapplied to the trailing character and prevented the
chunks from sharing one layout. A loader regression verifies the root and
nested-scope flags, and the software raw gate verifies exact alpha parity for
Latin/Greek/Cyrillic chunks, the middle red paint, and byte-identical repeats.

Verification on the rebuilt `linux-video-test` software CLI:

- SVG path loader suite: 44/44 cases and 348/348 assertions pass, including
  inherited/restored text direction and absolute coordinate scopes.
- Mixed Latin/Greek/Cyrillic SVG paint chunks match alpha exactly. The Arabic
  RTL raw gate still fails: explicit RTL currently produces no visible pixels
  in a canonical one-run render, and chunked/unchunked alpha differs by 52
  pixels (gate threshold: 2). The negative HarfBuzz advance normalization is
  implemented but has not fixed the end-to-end direction path; bidi shaping
  remains open. A temporary software trace shows the matching LTR plan reaches
  text preparation and rasterization, while the RTL plan bypasses those stages;
  the remaining defect is in scene/graph lowering or execution dispatch, not
  HarfBuzz glyph production.
- C ABI suite: 278/278 cases and 6909/6909 assertions pass after adding
  `style.direction` to the V3 schema and preserving the frozen V2 projection.
- Core text regressions: the synthetic negative-advance placement case passes
  (11 assertions), and Arabic contextual shaping passes (3 cases, 54
  assertions). These validate shaping data; they do not certify RTL raster
  placement, which remains failing in the software gate.
- Nested group isolation/clip/mask, SVG motion, styled text chunk, text
  gradient, textPath coordinates and text stroke raw gates: all pass.
- Static SVG composition and Trim Paths raw gates: pass. `git diff --check`: pass.
- The encoded MP4 CLI gate cannot run with this build: its FFmpeg sink reports
  `create_video_sink() returned nullptr` / `Failed to open encoder`; the
  software raw-RGBA gates remain usable. Vulkan remains unbuilt here.

Vulkan certification remains with the user. Other text gaps listed above,
general SVG filters and broader SMIL timing remain open.

### CPU clip-rule support — 2026-10-25

The importer now accepts SVG `clip-rule="evenodd"` and `clip-rule="nonzero"`
on clip-path definitions and their descendant geometry, carrying the resolved
rule into Chronon's canonical `PathFillRule`. A clipPath whose geometry resolves
to conflicting rules is rejected because the current single-PathShape mask
contract cannot preserve per-subpath clip rules. Verification on the CPU/SVG
build: `chronon3d_svg_path_loader_tests`, 48/48 cases and 374/374 assertions;
strict Vulkan parity is not claimed.

### SVG text paint overlays and current software verification — 2026-10-20

Per-code-point paint inside a shaped grapheme is now implemented for the
software renderer. HarfBuzz's normal composed glyph supplies the exact geometry
and alpha coverage. When an SVG paint boundary splits that grapheme, a second
mark-positioning shape pass recovers the mark glyph and GPOS offset; the mark
color is applied with `SRC_ATOP`, preserving destination alpha. The extended
`check_svg_text_chunk_raw_gate.py` requires exact alpha parity, at least 100
black base pixels and 10 red mark pixels, and byte-identical repeated output.
Current result: 0 alpha pixels changed, 1083 black pixels, 81 red pixels.

The complete software CLI build passed (449/449 build steps). On that binary,
the nested group isolation/clip/mask gate, nested motion gate, baseline,
gradient, textPath coordinate, bounded text pattern, text stroke, and text
chunk raw gates all pass. Focused CTest also passes for the C ABI, SVG path
loader, and text health suites; the C ABI run completed in 144.11 seconds.
`git diff --check` passes. The bounded pattern tile contract exercised by the
gate is the supported contract for this importer. The baseline path uses
OpenType BASE records when a matching record exists and metric fallback
otherwise; positive BASE-record fixture coverage is useful follow-up but is not
a blocker for the SVG parity scope above. Arbitrary SVG filter graphs and
broader SMIL timing remain outside this importer contract. Strict-native
Vulkan verification remains with the user.

### Software parity audit — 2026-10-20

Re-ran all eight SVG software raw gates against
`build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli` on the current
`main` checkout. All passed: group isolation with distinct nested clips/masks,
nested motion animations, text chunks (LTR, ligature, mixed script, Arabic RTL,
and split combining grapheme), dominant-baseline, gradient fill/stroke,
textPath and transformed coordinate lists, bounded pattern fill/stroke, and
text stroke. The raw gates compare rendered RGBA pixels and repeat determinism.
The repository architecture scan passes 207/207 rules, and focused CTest passes
for the C ABI, SVG path loader, and text health suites. This verifies the
non-Vulkan SVG parity scope on the software renderer; it does not certify the
Vulkan backend.

### Bounded text spacing and character rotation — 2026-10-21

The software importer now lowers `letter-spacing`, `word-spacing`, bounded
per-character `rotate`, and direct-run `textLength` with both supported
`lengthAdjust` modes. Vertical writing and nested `textLength`/`rotate` scopes
are rejected explicitly; `textLength` combined with coordinate lists or
`textPath` also fails closed. On the rebuilt `linux-video-test` CLI, the new
raw gate measured widths of 148 px for the plain run, 306 px with letter
spacing, 274 px with word spacing, 353 px for `textLength=360 spacing`, and
335 px for `textLength=360 spacingAndGlyphs`; rotated output repeated byte for
byte. SVG loader tests pass (47 cases, 365 assertions), as do the styled text
chunk, baseline, textPath coordinate, and bounded pattern raw gates. These
results close the bounded software text contract; nested text scopes remain in
[`TICKET-SVG-TEXT-ADVANCED-SCOPES.md`](TICKET-SVG-TEXT-ADVANCED-SCOPES.md).

### CPU clip-rule and gradient spread — 2026-10-25

The software importer accepts SVG `clip-rule="evenodd"` and `"nonzero"`,
including inherited CSS rules on clip geometry. Linear and radial gradients
lower `spreadMethod="pad"`, `"repeat"`, and `"reflect"` to Chronon's existing
CPU sampler. `userSpaceOnUse` gradient coordinates were already implemented;
gradient inheritance and unknown spread values remain fail-closed. Focused
loader coverage verifies the rules and spread modes. Vulkan parity is not
claimed by these importer changes.

