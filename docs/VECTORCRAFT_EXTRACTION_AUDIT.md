# VectorCraft extraction audit for Chronon

**Scope:** read-only source comparison and tests of the upstream path-operations crate. No Chronon production code was changed. The upstream checkout, Rust toolchain, and Cargo build live under `/tmp`; no commit or push was made.

## 1. Snapshot and method

- Repository: [`storytold/vectorcraft`](https://github.com/storytold/vectorcraft/tree/fc996e0e0313f132868d71f8ca56c04dbf78d6a5)
- Exact snapshot: `fc996e0e0313f132868d71f8ca56c04dbf78d6a5`, commit dated 2026-10-22, “Merge pull request #3560 from dotCooCoo/feat/package-off-ui-thread”.
- Workspace release: `0.14.0`; Rust edition 2024, declared MSRV 1.95, workspace license `MIT OR Apache-2.0` ([manifest at pinned revision](https://github.com/storytold/vectorcraft/blob/fc996e0e0313f132868d71f8ca56c04dbf78d6a5/Cargo.toml)).
- Tested on Linux/WSL using rustc/cargo 1.99.1 installed in `/tmp`; no changes to system toolchain or Chronon build files.
- Inspected actual Rust source, package manifests, lockfile, tests and existing Chronon/ChrononMotion3D source. The claims below separate source behavior from README statements.

## 2. Executive decision

| Area | Decision | Evidence-based rationale |
|---|---|---|
| `pathops` Boolean arrangement | **ADAPT** | Demonstrated gap: Chronon `Merge` is concatenation of independent subpaths, not a Boolean union. `pathops` offers curve-aware boolean/normalize/regions, but is Rust-only and depends on a beta sweep crate. Porting the API or whole crate is not justified; first prototype a narrowly scoped C++ algorithm behind Chronon’s canonical path-operator route. |
| Curve offset and stroke outline | **ADAPT** | Chronon offset currently rejects curves and multiple subpaths; VectorCraft handles Béziers and compound paths through kurbo + boolean normalization. Prototype only if product scenes need editable offset geometry; Chronon already renders strokes directly. |
| `geom` | **KEEP Chronon** | Both use cubic Bézier paths and common geometric concepts. Chronon’s path command model already represents line, quadratic, cubic and close commands. Kurbo is useful as a reference, but replacing the C++ model with the Rust crate is not an integration plan. |
| Effects / warps | **KEEP Chronon** | ChrononMotion3D already has Bend, Twist, Lattice and PathWarp; Chronon has several path operators and raster/effect systems. VectorCraft effects crate has broad internal dependencies and would duplicate ownership. |
| SVG | **KEEP Chronon; selectively ADAPT tests** | Chronon already has a substantial SVG loader and fixtures. VectorCraft’s importer/exporter is a useful source of differential fixtures and edge cases, not evidence for replacing Chronon’s importer. |
| Typography | **KEEP Chronon** | Chronon has its own text shaping/render path and caches. VectorCraft text’s Harfrust/Skrifa/ICU stack is a parallel implementation; no demonstrated feature gap was established in this audit. |
| Gradient mesh / blends | **KEEP Chronon** | Gradient mesh is represented in Chronon’s fill/RenderPlan types. Chronon path morph requires compatible topology; VectorCraft blend behavior was not established as a missing Chronon capability by this audit. |
| Testkit | **ADAPT selectively** | Its fixture/oracle patterns are useful. Its use as `pathops` dev dependency pulls most of the application stack into a pathops test build, so do not adopt that dependency shape in a lightweight Chronon test target. |

## 3. P0: geometry and Boolean operations

VectorCraft’s `crates/pathops` implements `boolean`, `try_boolean`, `boolean_n`, `unite_all`, `normalize`, `area`, `regions`, `pathfinder`, `offset_path`, `outline_stroke`, and path simplification. Its source converts filled contours to closed Bézier paths, detects invalid non-finite coordinates in the checked API, resolves fill winding, and uses `linesweeper` to build an arrangement. Its README’s claim that original curve pieces are restored exactly and other smooth joins are refitted to 0.01 pt is a project claim; the code and tests support curve preservation, but do not establish a universal 0.01 pt guarantee for every scale or pathological input ([pathops README](https://github.com/storytold/vectorcraft/blob/fc996e0e0313f132868d71f8ca56c04dbf78d6a5/crates/pathops/README.md), [boolean implementation](https://github.com/storytold/vectorcraft/blob/fc996e0e0313f132868d71f8ca56c04dbf78d6a5/crates/pathops/src/boolean.rs)).

Chronon already represents path commands as line, quadratic, cubic and close commands, and renders paths by flattening them for rasterization. The gap is geometric editing: `src/scene/path_operators.cpp::offset_path` accepts only a single polyline subpath and rejects curves, multiple subpaths and zero-length segments. `tests/scene/shapes/test_path_utils.cpp` explicitly documents that `Merge` combines paths as independent subpaths without Boolean operations. This is concrete, actionable parity evidence.

### Geometry cases verified in upstream tests

The upstream `crates/pathops/tests/ops.rs`, `regressions.rs`, `prop_pathops.rs`, `props.rs`, plus unit tests in `boolean.rs`, `offset.rs`, `planar.rs`, `fit.rs` and `pathfinder.rs`, contain substantially more than 30 distinct named tests/property checks. The suite covers:

1. Rectangle union, intersection, difference and XOR.
2. Disjoint union and empty/disjoint cases.
3. Idempotent union and `A − A`.
4. Circle union with curve preservation and compact anchors.
5. Circle/rectangle intersection with curve preservation.
6. Circle difference and containment.
7. Hole creation from difference.
8. Fill rules: NonZero and EvenOdd.
9. Nested rings and opposite winding.
10. Region split for island inside a hole.
11. Multiple-input union.
12. Open input contour implicit closure for fills.
13. Cubic Bézier intersections and preservation.
14. Near-horizontal edge against a circular cap regression.
15. Tangent/near-tangent intersection property cases.
16. Coincident identical curves, including the documented tiny-lens caveat.
17. Self-crossing/self-overlapping paths.
18. Non-finite coordinates through checked Boolean API.
19. Degenerate/empty paths.
20. Offset circle growth.
21. Offset circle inset and collapse when inset exceeds bounds.
22. Rectangle miter offset.
23. Bevel offset area.
24. Round offset area.
25. Offset miter regression that previously caused a non-terminating sweep.
26. Butt-cap stroke outline.
27. Square-cap stroke outline.
28. Round-cap stroke outline.
29. Closed-square outline producing a ring.
30. Self-overlapping zigzag stroke normalization.
31. Coincident/collinear stroke spine compaction.
32. Curved stroke deviation after compaction.
33. Boolean area identities across union/intersection/XOR/difference.
34. Property-generated path combinations and invariant checks.

The upstream test suite was run as `cargo test -p vectorcraft-pathops --no-fail-fast`. It compiles `vectorcraft-testkit` as a dev dependency, which brings in `vectorcraft-engine`, fonts, renderer, SVG, PDF and other workspace crates. Result: **120 passed, 0 failed, 1 ignored**, across 44 unit tests, 47 ops tests (46 passed + 1 ignored), 1 pinned Pathfinder test, 17 pathops property tests, 9 general property tests, and 3 regressions. The ignored test is the explicit 1,000-circle timing test. The property suites ran their generated cases successfully. No Chronon geometry runtime comparison was run, so this is a source/test-suite audit, not a comparative numerical benchmark.

### Numerical / visual quality and known limits

- Bézier-aware sweep avoids polygonizing every input curve before intersections; this is a promising property for editable path results.
- The code restores subcurves where possible and refits generated joins. This preserves visual quality better than a pure flattened polygon result, but still has a tolerance and anchor-count cost.
- `offset_path` depends on kurbo’s stroker, then normalizes the result. It can create many anchors, and its offset quality inherits the stroker’s approximation behavior.
- README documents tiny residual lenses for identical coincident curves and sliver removal below precision. Therefore identical/coincident curves and near-degenerate inputs need deterministic regression fixtures before production use.
- `linesweeper` is an external dependency and is described by its package documentation as early/beta software; do not elevate its claimed robustness to a guarantee.
- No screenshot/raster oracle comparison or Chronon-vs-VectorCraft numerical error benchmark was done. The assertion evidence currently comes from VectorCraft’s unit/property tests; visual superiority remains unproven.

## 4. P1: effects, warps, gradients, SVG and testkit

### Effects and warps

VectorCraft source includes free distort, pucker/bloat, roughen, transform, tweak, twist, zig-zag, warp, reshape, text outline and stroke operations. Chronon already includes path operators such as RoundCorners, ZigZag, OffsetPath, PuckerBloat, Twist, WigglePath, Trim, Morph and Repeater; ChrononMotion3D has a deformer stack including Bend, Wave, Twist, Bulge, Taper, Lattice, PinWarp, PathWarp, NoiseWarp and SquashStretch. These are not guaranteed to be mathematically identical, but they demonstrate existing canonical implementations. Do not add parallel registries/resolvers. Gradient mesh is already a Chronon fill type and encoded in RenderPlan, so VectorCraft does not fill that gap.

The VectorCraft `effects` crate depends on `geom`, `three-d`, `doc`, `pathops`, `text`, `plugins`, `image`, `kurbo` and `serde_json`. Reusing it wholesale would bring overlapping model/effect ownership and unnecessary dependency breadth. If a specific effect shows a measurable quality difference, port the math only and connect it through Chronon’s existing operator/registry contracts.

### SVG and fixtures

VectorCraft’s SVG crate imports/exports via `usvg` and has a broad dependency surface (document, effects, brush, text, image and related libraries). `resvg` is a dev dependency, not a production dependency of that crate. Chronon already has an established SVG loader/importer with path, shape, fill and filter handling plus a large test file. No side-by-side SVG fixture render was performed here; there is no basis to replace the importer. Best use: selectively port minimized failing SVG fixtures and expected semantics into Chronon’s existing tests when a real mismatch is found.

The testkit is valuable as an oracle/fixture pattern, but pathops’ dev-dependency on it caused the Linux build to compile a large part of the application and embedded-font stack. For Chronon, keep geometric tests hermetic and add renderer-backed comparisons only in a separate integration layer.

### Typography and supplemental modules

VectorCraft text uses `skrifa`, `harfrust`, ICU segmentation, bidi and Unicode helpers. Chronon already has its own text shaping/font-rendering/caching infrastructure. No concrete missing shaping behavior was demonstrated here. VectorCraft trace and brush generation are additional candidate features, but this audit did not find a demonstrated Chronon need or run visual quality comparisons; keep them as future feature-specific investigations. Its `three-d` module includes extrusion/revolve/inflate-style geometry, overlapping Chronon’s existing 3D purpose; reject wholesale adoption.

## 5. Dependencies, licensing, portability and cost

### P0 runtime dependency set

`vectorcraft-pathops` direct production dependencies are `vectorcraft-geom`, `kurbo`, `thiserror`, and `linesweeper`; its dev dependencies are `vectorcraft-testkit` and `proptest`. The checked lockfile pins `kurbo 0.13.1` and `linesweeper 0.5.0`. Kurbo’s package is dual Apache/MIT; VectorCraft’s workspace is MIT OR Apache-2.0. Audit every transitive package and any copied source before shipping; do not infer the license of the entire dependency tree from the workspace manifest. VectorCraft’s `NOTICE` also flags third-party materials with separate terms ([NOTICE](https://github.com/storytold/vectorcraft/blob/fc996e0e0313f132868d71f8ca56c04dbf78d6a5/NOTICE)).

### Linux headless / C++ compatibility

The pathops crate is pure Rust and has no production UI or renderer dependency in its own manifest. It is therefore buildable as a headless Rust library on Linux, subject to the upstream Rust 1.95 MSRV and compatible target/dependency versions. It is **not a C++ library**: there is no C ABI or CMake integration. Chronon would need either a separately maintained C ABI wrapper plus Rust toolchain in the build, or a deliberate C++ port of a narrowly selected algorithm. Given the engine’s C++ production code and desire for light headless builds, a C++ port is more aligned, but should happen only after an agreed test corpus demonstrates the gap and the port proves equivalent behavior.

### Build and runtime cost evidence

- The pathops test command required 2m31s to build the testkit’s broad dependency graph, including engine, renderer, SVG, document/formats, text and bundled fonts. One Rust compiler process building the engine reached roughly 2.2 GiB resident memory in a process snapshot; this is **not** a measured total peak for the build or a runtime pathops benchmark.
- No throughput, memory-per-operation, or visual-error benchmark against Chronon was run. Therefore no performance claim or production cost estimate is made.
- Expected adoption cost: Rust toolchain integration/C ABI and release licensing review are material; a C++ port avoids runtime cross-language linkage but needs algorithm engineering and a robust conformance corpus.

## 6. Recommended interventions, ordered by value

1. **Add a Chronon Boolean path operation** only if a product use case needs true Boolean union/cut. Define acceptance with at least 30 fixtures (the categories listed above), cross-check area/winding and curve preservation, and expose it only via the canonical path operator/registry.
2. **Upgrade path offset/stroke-to-outline for cubic and compound paths** if generated scene assets need editable outlines. Keep direct rendering of ordinary strokes as the default; compare geometry and raster outputs at several scales and joins.
3. **Add differential SVG fixtures** from VectorCraft’s importer tests to Chronon’s current SVG test suite where they expose a confirmed missing feature. Do not replace the importer absent failures.
4. **Keep current Chronon/ChrononMotion3D effect ownership**; use VectorCraft as a reference when a specific warp/operator has an observable numerical defect.
5. **Revisit integration language only after parity tests**: a narrowly scoped C++ implementation is preferred over adopting the whole Rust application stack; if Rust is chosen, prototype C ABI, packaging, static linking and license audit first.

## Sources / code locations

- Pinned VectorCraft [`pathops README`](https://github.com/storytold/vectorcraft/blob/fc996e0e0313f132868d71f8ca56c04dbf78d6a5/crates/pathops/README.md), [`boolean.rs`](https://github.com/storytold/vectorcraft/blob/fc996e0e0313f132868d71f8ca56c04dbf78d6a5/crates/pathops/src/boolean.rs), [`offset.rs`](https://github.com/storytold/vectorcraft/blob/fc996e0e0313f132868d71f8ca56c04dbf78d6a5/crates/pathops/src/offset.rs), [`ops.rs`](https://github.com/storytold/vectorcraft/blob/fc996e0e0313f132868d71f8ca56c04dbf78d6a5/crates/pathops/tests/ops.rs), [`regressions.rs`](https://github.com/storytold/vectorcraft/blob/fc996e0e0313f132868d71f8ca56c04dbf78d6a5/crates/pathops/tests/regressions.rs), [`manifest`](https://github.com/storytold/vectorcraft/blob/fc996e0e0313f132868d71f8ca56c04dbf78d6a5/crates/pathops/Cargo.toml).
- [`linesweeper 0.5.0 package docs`](https://docs.rs/linesweeper/0.5.0/linesweeper/); [`kurbo Cargo manifest`](https://github.com/linebender/kurbo/blob/main/Cargo.toml).
- Chronon: `include/chronon3d/scene/model/shape/path_commands.hpp`, `include/chronon3d/scene/model/shape/path.hpp`, `src/scene/path_operators.cpp`, `tests/scene/shapes/test_path_utils.cpp`, `include/chronon3d/graphics/gradient_mesh.hpp`.
- ChrononMotion3D: `include/chrononmotion/motion/RigDeformers.hpp`, `src/chrononmotion/motion/RigDeformers.cpp`, `tests/motion_rig.cpp`.

## Implementation closeout (Chronon)

The two recommended geometry interventions have since been implemented in Chronon:

- Added a distinct `boolean_union` path operator end-to-end through the RenderPlan schema/decoder, compiler, materializer and fingerprint. Existing `merge` keeps its original subpath-concatenation semantics.
- Reworked `offset_path` to accept cubic and quadratic paths, multiple closed contours and holes. Curves are adaptively flattened to 0.05 path units, then Clipper2 normalizes and offsets closed geometry; open curves preserve the existing one-sided offset behavior using the flattened polyline.
- Clipper2 2.0.1 is now a base CMake/vcpkg dependency and an SDK `find_dependency`; vcpkg reports its license as BSL-1.0.
- Added regression coverage for overlapping/touching/coincident/contained/disjoint unions, curve input, compound contours, open curves, holes and collapsed insets. An isolated linked C++ smoke harness ran 30 geometric scenarios successfully. Changed C++ translation units and tests also passed `-fsyntax-only` compilation.
- A full preset reconfigure/build was attempted but stopped after nearly ten minutes waiting on WSL `/mnt/c` filesystem I/O. The full Chronon test executable was therefore not run in this session; the targeted linked smoke test is the runtime evidence.

Limit: Boolean and offset outputs are polygonal paths; source Bézier segments are approximated within the stated flattening tolerance rather than preserved as curves. The new operator is union-only; subtraction/intersection/XOR were not part of these two closeout items.
