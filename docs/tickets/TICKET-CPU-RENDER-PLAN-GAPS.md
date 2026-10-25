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

- [ ] **SVG-F1 — bounded filter graph:** import ordered root-level
  `feGaussianBlur`, `feColorMatrix`, `feDropShadow`, and `feBlend` operations
  where each maps to an existing software effect/compositor contract; enforce
  input, operation-count, numeric and result-reference bounds.
- [ ] **SVG-F2 — fail-closed semantics:** reject unsupported filter units,
  primitive subregions, color spaces, result references and blend modes with
  typed diagnostics; verify no partially lowered graph escapes.
- [ ] **SVG-F3 — pixel evidence:** add software RGBA fixtures for each supported
  primitive and at least one ordered chain, including alpha and deterministic
  repeat checks; run SVG loader and software render targets.

### Lottie layer expansion

- [ ] **LOT-1 — multiple 2D shape layers:** lower bounded `ty=4` layers in
  source order into canonical imported subpaths while preserving each layer's
  transform, opacity and paint; reject semantics that cannot be represented
  without changing compositing order.
- [ ] **LOT-2 — nested precompositions:** support bounded `ty=0` precompositions
  with deterministic recursion/size limits and canonical frame remapping, or
  document a concrete RenderPlan model blocker and keep a typed rejection.
- [ ] **LOT-3 — regression gates:** fixtures cover overlap/order, transforms,
  opacity, timing, recursion/cycle rejection, fingerprints and software frame
  hashes; update the Lottie ticket only for verified capabilities.

### Data visualization geometry

- [ ] **CHART-1 — monotone interpolation:** provide a bounded deterministic
  monotone cubic spline path generator with finite-input validation, duplicate-x
  policy and endpoint/tangent regressions.
- [ ] **CHART-2 — RenderPlan exposure:** decode a declarative line/area chart
  using the spline generator and existing fill/path contracts; include data and
  options in fingerprints and schema.
- [ ] **CHART-3 — software output:** render line and filled-area fixtures on the
  software backend and verify clipping, alpha and repeat determinism.

### Completion and known hardware boundary

- [ ] **VERIFY-1:** run `git diff --check`, relevant architecture/schema checks,
  SVG + C ABI + scene + software focused test suites, and CPU builds with
  `-j12`; record exact results below.
- [ ] **VERIFY-2:** audit the identified gaps against current source and mark
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
