# Ticket: close CPU RenderPlan gaps

## Goal

Close the remaining CPU/software gaps identified in the RenderPlan parity audit,
using existing Chronon geometry, layout, effect and evaluation authorities.
Vulkan-only parity is recorded separately and is not a prerequisite for CPU
acceptance. Do not add a second renderer, sampler, layout authority or animation
runtime.

## Executable checklist

Each checkbox is marked only after its implementation, focused regression,
`-j12` build and relevant software test have passed.

### Intrinsic layout sizing

- [x] **LAYOUT-1 — measured content bounds:** measure rects, rounded rects,
  circles, imported paths (conservative control-point bounds), text runs and
  explicitly sized images from canonical scene payloads; reject or safely
  ignore empty/non-finite bounds. Intrinsic decoded image dimensions and
  rotated per-node bounds are outside this first bounded contract.
- [x] **LAYOUT-2 — variable Flow items:** opt-in intrinsic sizing in RenderPlan
  Flow with variable main/cross extents, wrapping, gap and all justify modes;
  preserve explicit uniform-cell behavior by default.
- [x] **LAYOUT-3 — variable Grid tracks:** derive row/column track maxima from
  measured item bounds and place items in those tracks; preserve existing
  explicit cell sizing and fit semantics by default.
- [x] **LAYOUT-4 — contract integrity:** decode/schema validation, stable
  fingerprints, builder lowering, and tests for text, geometry, transforms,
  wrapping, empty content and backward-compatible defaults.

### SVG filters on software

- [x] **SVG-F1 — bounded filter graph:** import ordered root-level
  `feGaussianBlur`, `feColorMatrix`, `feDropShadow`, and `feBlend` operations
  where each maps to an existing software effect/compositor contract; enforce
  input, operation-count, numeric and result-reference bounds.
- [x] **SVG-F2 — fail-closed semantics:** reject unsupported filter units,
  primitive subregions, color spaces, result references, element-scoped filters,
  and blend modes through the typed `svg_import_failed` compiler diagnostic;
  no partial effect list escapes on failure.
- [x] **SVG-F3 — pixel evidence:** render a blur → drop-shadow → color-matrix
  → blend chain with the software backend, check center/expanded-shadow alpha,
  and compare repeated frame pixels. SVG loader and renderer tests pass.

### Lottie layer expansion

- [x] **LOT-1 — bounded multi-layer import:** 2D shape layers lower to separate
  scene layers with stable z-order, parent links, blend modes, and clipped
  integral-frame in/out windows. Hidden layers are skipped; unsupported layer
  types and effects/expressions/mattes fail closed.
- [x] **LOT-2 — nested precompositions:** recursively expand `ty=0` assets to
  depth 8, with cycle detection, source `refId` resolution, parent carrier
  transforms, integral start time, unit time stretch, and parent/root time
  clipping. Expansion is bounded to 256 scene layers. Time remap, non-unit
  stretch, group opacity, precomp blend modes, effects, mattes, expressions,
  and non-shape child layers remain rejected.
- [x] **LOT-3 — regression gates:** tests cover multi-layer order and parent
  links, nested precomp hierarchy, cycle rejection, source asset manifest
  retention, existing single-layer paint/transform behavior, and preservation
  of RenderPlan path operators such as repeaters.

### Data visualization geometry

- [x] **CHART-0 — existing primitives audit:** confirm RenderPlan/software
  already support ring segments and conic gradients; generator/decoder and
  software gradient-sampling tests cover them. They needed no duplicate
  implementation.
- [x] **CHART-1 — monotone interpolation:** provide a bounded deterministic
  monotone cubic spline path generator with finite-input validation, duplicate-x
  policy and endpoint/tangent regressions.
- [x] **CHART-2 — RenderPlan exposure:** decode declarative `monotone_line`
  data points into canonical cubic path commands; coordinates participate in
  the content fingerprint and JSON schema bounds them to 2..4096 finite pairs.
- [x] **CHART-3 — area, axes and grid:** declarative `monotone_chart` creates
  bounded monotone spline line and closed area paths, domain tick positions,
  axes and grid lines as canonical imported subpaths. Domains,
  points, tick counts, colors and dimensions are validated and fingerprinted.
- [x] **CHART-4 — software output:** CPU pixel test checks visible area, line,
  axes and grid, transparent clipping away from the chart and deterministic
  repeated output.

### Completion and known hardware boundary

- [x] **VERIFY-1:** run `git diff --check`, relevant architecture/schema checks,
  SVG + C ABI + scene + software focused test suites, and CPU builds with
  `-j12`; record exact results below.
- [x] **VERIFY-2:** audit the identified gaps against current source and mark
  each one `PASS`, `PARTIAL`, or `NOT RUN` with evidence. Vulkan-native shadows,
  instanced GPU particles and Vulkan capability parity stay `NOT RUN` on a
  machine without a Vulkan runtime/device.

## Evidence log

- 2026-10-25: started. Existing CPU ring segments, path booleans, SVG clip-rule,
  and SVG gradient spread modes are implemented in prior commits; their focused
  tests passed. The criteria above describe the remaining work in this ticket.
- 2026-10-25: LAYOUT-1…4 complete. Software Flow/Grid intrinsic sizing is
  opt-in via `intrinsic_size`; positive `cell_size` axes override measurement,
  supported content is measured from rect/rounded-rect/circle/path/text-run and
  explicitly sized image payloads, and empty inputs use existing deterministic
  canvas fallback. Schema parses; scene suite 274/274 cases, 1620/1620
  assertions and C ABI suite 280/280 cases, 7498/7498 assertions pass after
  `cmake --build build/chronon/linux-fast-dev --target chronon3d_scene_tests
  chronon3d_c_abi_tests -j12`.
- 2026-10-25: implementation and focused regressions complete for bounded SVG
  root filter chains, Lottie multi-layer/nested-precomp expansion, and CPU
  monotone chart area/axes/grid. SVG filter parsing lives in `svg_filter_parser.cpp`
  (the validator is 392 lines); element-scoped filters fail closed rather than
  being widened to the whole SVG. Full verification results follow below.
- 2026-10-25: `-j12` CPU builds passed with `CHRONON3D_ENABLE_SVG=OFF` and
  `CHRONON3D_ENABLE_SVG=ON`; both set `CHRONON3D_ENABLE_VULKAN=OFF`. Full test
  results: scene 274/274 cases, 1620/1620 assertions; renderer without SVG
  193/193, 5933/5933; renderer with SVG 194/194, 5944/5944; SVG loader 52/52,
  414/414; C ABI without SVG 284/284, 7770/7770; C ABI with SVG 292/292,
  7995/7995. Architecture gate passed 209/209 rules; compiler boundary and IPC
  schema checks passed; SDK schema parity passed after installing the build to
  `sdk-dist`; JSON schema parsing and `git diff --check` passed.
- 2026-10-25: the architecture gate initially treated `ColorMatrix` as missing
  scalar animation metadata even though its 20 named coefficients are exposed
  through `kColorMatrixAnimatableParams`. Updated the gate to recognize this
  dedicated authority; it now passes without inventing a duplicate parameter.
- 2026-10-25: bounded Lottie multi-layer and recursive precomp expansion plus
  the declarative `monotone_chart` area/axes/grid geometry are implemented.
  Lottie compiler recursion is isolated in `render_plan_compiler_lottie_expansion.cpp`.
  After the final split, focused no-SVG tests pass (Lottie multi/precomp and
  chart: 2 cases, 26 assertions; chart CPU pixels: 1 case, 9 assertions).
  Full no-SVG C ABI: 285/285 cases, 7789/7789 assertions; full SVG C ABI:
  293/293 cases, 8014/8014 assertions. Renderer: no-SVG 194/194 cases,
  5941/5941 assertions; SVG 195/195, 5953/5953. Scene: 274/274,
  1620/1620. Both CPU builds passed with `-j12`; schema SDK parity, architecture
  gate (209/209), compiler boundary, IPC schema, JSON parse and diff checks pass.

## Final audit status

- **PASS — CPU layout and path geometry:** intrinsic Flow/Grid sizing, path
  booleans, ring segments, conic fill decoding/sampling, and monotone lines are
  available through existing CPU authorities.
- **PASS — bounded SVG filters:** root-level linear chains map to current CPU
  blur, drop-shadow, color-matrix, and blend contracts; unsupported scopes and
  graph features are rejected.
- **PASS — bounded Lottie multi-layer/precomp:** separate 2D shape layers,
  parent hierarchy, supported blend modes, layer timing and nested precomp
  expansion run through the canonical scene compiler. Time remap, group
  isolation semantics, mattes/effects/expressions and non-shape layers remain
  unsupported and fail closed.
- **PASS — CPU charts:** monotone line and area paths, axes, tick marks and
  adaptive grid lines render through the existing software path renderer.
- **DEFERRED — native map plates:** the acquisition performance gate and
  scaffolding are complete. Phases 2–7 remain optional per
  `TICKET-MAPS-NATIVE-PORT.md`, which measures tile acquisition rather than C++
  map rasterization as the current bottleneck.
- **NOT RUN — Vulkan-only parity:** native GPU shadows, instanced GPU particles,
  and registry/Vulkan capability parity require a Vulkan runtime/device. Both
  build configurations used here set `CHRONON3D_ENABLE_VULKAN=OFF`.
