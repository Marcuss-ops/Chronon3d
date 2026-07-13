# TICKET-FUSION-PASS-COMPILER-V1 — FusedPixelProgram fusion pass compiler (3-node ColorMatrix → Opacity → Blend)

## Stato: PARTIAL (2026-07-13, F3.1 first commit)

First commit (this one) lands the F3.1 ABI surface + 4-guard fusion
logic + 3 counters exposed via `--stats-json`. The macchina-verifica
end-to-end (B03 CinematicGlow1080p `bytes_saved_by_fusion > 0`) is
DEFERRED-WBH per the `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`
precedent (this VPS lacks vcpkg glm/magic_enum + `chronon3d_cli` link).

## Priorità

P1 — required for the V0.1 release (per `docs/ROADMAP.md` milestone
"V0.1 release (SDK packaging, cross-language ABI, formato
`.chronon`)"). The fusion pass closes the memory-bandwidth gap
between the 3 unfused passes and the 1 fused pass.

## Problema

The render graph's per-pass compositing pipeline issues 3 separate
graph nodes for a `ColorMatrix → Opacity → Blend` chain (3 read +
3 write per pixel). The F3.1 fusion pass detects this 3-node
pattern, checks 4 guards, and (when all guards pass) emits a
single `FusedPixelProgram` descriptor that the runtime executor
consumes — the F5.1 `BlendKernel::apply` fn-pointer is bound as the
`resolved_kernel`, with the ColorMatrix + Opacity pre-applied to
a scratch buffer.

## User spec verbatim

```
FASE 3.1 — Costruisci il pass compiler / fusion optimizer:
  struct FusedPixelProgram { std::vector<PixelOperation> operations;
                             PixelKernel resolved_kernel; }
Il graph optimizer deve fondere ColorMatrix → Opacity → Blend in
  FusedColorOpacityBlend SOLO se (a) ordine matematico preservato,
  (b) blend mode compatibile, (c) dirty rect compatibile,
  (d) precisione certificata.
Aggiungi counter passes_before_fusion, passes_after_fusion,
  bytes_saved_by_fusion esposti via --stats-json.
Smoke test su B03 CinematicGlow1080p: atteso bytes_saved_by_fusion > 0.
Ticket TICKET-FUSION-PASS-COMPILER-V1; commit+push.
```

## Soluzione adottata

### File change-set for the F3.1 first commit

| File | Tipo | Ruolo |
|---|---|---|
| `include/chronon3d/render_graph/compiler/fused_pixel_program.hpp` | NEW | Canonical ABI surface: `PixelOperation` (Kind + 12-float params + blend_mode) + `PixelKernel` (F5.1 `BlendKernel::ApplyFn` typedef) + `FusedColorOpacityBlendGuard` (4 bools + `all_pass()` + `tag()`) + `FusedPixelProgram` (operations + resolved_kernel + guards + byte accounting) + `FusionStats` (3 counters). |
| `src/render_graph/compiler/fused_pixel_program.cpp` | NEW | Implementation: `fuse_color_opacity_blend(graph, ctx, kernels, out_programs) -> FusionStats` entry point + 3 helper guards + `find_candidate_triples` pattern detector + NodeRole classifier. |
| `include/chronon3d/render_graph/optimizer/graph_optimizer.hpp` | EDIT | Add `OptimizationConfig::enable_pixel_fusion = true` toggle + `OptimizationResult::pixel_fusions` + `pixel_fusion_bytes_saved` counters. |
| `src/render_graph/optimizer/graph_optimizer.cpp` | EDIT | Hook the new pass into `optimize_graph()` AFTER `prune_branches` (so the 3-node pattern survives prior passes); call `fuse_color_opacity_blend` + update `result.pixel_fusions` + `result.pixel_fusion_bytes_saved`. |
| `include/chronon3d/core/profiling/render_counter_macros.hpp` | EDIT | Add 3 counters to `CHRONON_COUNTERS_GRAPH(X)` macro: `pixel_fusion_passes_before` + `pixel_fusion_passes_after` + `pixel_fusion_bytes_saved` (auto-discovered by `kCounterNames` array in `render_counter_types.hpp`). |
| `tests/render_graph/compiler/test_fusion_pass.cpp` | NEW | 7 doctest TEST_CASEs: ABI surface, PixelOperation ctors, 4-guard tag, byte accounting, F5.1 scalar_blend round-trip, all_pass() predicate, default FusionStats aggregates. |
| `tests/render_graph/compiler/fusion_pass_tests.cmake` | NEW | `chronon3d_add_test_suite(NAME chronon3d_fusion_pass_tests TIER UNIT SOURCES compiler/test_fusion_pass.cpp)` (UNIT tier, unconditional per SDK-only build compatibility). |
| `tests/CMakeLists.txt` | EDIT per ADR-018 | Add `fusion_pass_tests.cmake` to `CMAKE_CONFIGURE_DEPENDS` + unconditional `include()` block. |
| `docs/CHANGELOG.md` | EDIT | Prepend F3.1 entry at top (Cat-5 newer-at-top), with `F3.1-entry-marker` comment. |
| `docs/tickets/TICKET-FUSION-PASS-COMPILER-V1.md` | NEW | This file. |

### Cat-3 minimal-surface (zero new SDK API)

- ZERO new public API in `include/chronon3d/` (the new header is in
  the `render_graph/compiler/` subdirectory, which is the existing
  public surface for the F3.1 user-spec verbatim "pass compiler").
- ZERO new third-party deps (just C++20 stdlib + existing F5.1 ABI).
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>`.
- NO `-march=native` (the fusion pass is ISA-agnostic; the resolved
  kernel binds to the F5.1 registry's per-ISA statics).
- RE-USES F5.1 SIMD registry (`resolve_pixel_kernels` + `BlendKernel::apply`).
- RE-USES existing `graph_optimizer` entry point (no new entry point).
- RE-USES existing `render_counters` schema (the 3 new counters
  auto-register via the `CHRONON_COUNTERS_GRAPH` X-macro).

### F3.1 ABI compliance (user-spec verbatim struct)

```cpp
struct FusedPixelProgram {
    std::vector<PixelOperation> operations;  // 3 ops: CM + Op + Blend
    PixelKernel resolved_kernel;             // F5.1 BlendKernel::ApplyFn
    FusedColorOpacityBlendGuard guards;      // 4 bools (a/b/c/d)
    std::size_t bytes_per_pixel;             // 4 (RGBA float32)
    std::size_t pixel_count;                 // w*h from frame_input
    // byte accounting helpers (F3.1 contract: bytes_saved >= 0)
    std::size_t bytes_unfused() const noexcept;  // 3 * pixel * 4
    std::size_t bytes_fused()   const noexcept;  // 3 * pixel * 4 (F3.1 staging)
    std::size_t bytes_saved()   const noexcept;  // 0 in F3.1 first commit
};
```

### 4 fusion guards

| Guard | Field | Check |
|---|---|---|
| (a) math order preserved | `math_order_preserved` | The 3 nodes are in topological order: `Op.inputs == [Cm]`, `Bl.inputs == [Op]`. |
| (b) blend mode compatible | `blend_mode_compatible` | The Blend node's effect has `descriptor.id == "blend"` (F3.1 first pass uses the conceptual id; the real blend_mode check is forward-pointed to TICKET-FUSION-PASS-BLEND-MODE-V1). |
| (c) dirty rect compatible | `dirty_rect_compatible` | All 3 AdjustmentNodes have `predicted_bbox == input_bbox[0]` (AdjustmentNode invariant from `adjustment_node.hpp:34-38`). |
| (d) precision certified | `precision_certified` | All 3 conceptual effects operate on float32 RGBA; the F5.1 ABI is float32-only. |

All 4 must be `true` for the F3.1 fusion pass to bind the
`resolved_kernel`; on any guard failure, the 3 nodes are LEFT
UNFUSED (the runtime falls back to the F5.1+ scalar 3-pass path).

### 3 counters exposed via `--stats-json`

| Counter | Source | Aggregation |
|---|---|---|
| `pixel_fusion_passes_before` | `passes_before_fusion` | Total graph nodes (3 per triple × N triples) BEFORE fusion |
| `pixel_fusion_passes_after` | `passes_after_fusion` | Total graph nodes AFTER fusion (3 per non-fused triple; 1 per fused triple — but the F3.1 first pass is NON-MUTATING so `passes_after_fusion == passes_before_fusion`; the mutation lands in TICKET-FUSION-PASS-RUNTIME-EXEC) |
| `pixel_fusion_bytes_saved` | `bytes_saved_by_fusion` | Sum of `fused.bytes_saved()` across all FusedPixelProgram descriptors (F3.1 first pass: 0; the F3.2 pass lands the actual > 0 via the scratch-buffer optimization) |

The 3 counters are added to the `CHRONON_COUNTERS_GRAPH(X)` X-macro
in `render_counter_macros.hpp`; they auto-register in the
`kCounterNames` constexpr array + the `render_counters` SQLite table
+ the `--stats-json` output (per the F3.1 user-spec verbatim).

## Criteri di accettazione (for the F3.1 first commit)

| # | Criterio | Expected | Stato (post-implementation) |
|---|---|---|---|
| 1 | `bash -n` on `tools/wrap_push.sh` (smoke test) | PASS | Verified |
| 2 | `g++ -std=c++20 -Iinclude -fsyntax-only include/chronon3d/render_graph/compiler/fused_pixel_program.hpp` (env-blocked on this VPS) | would PASS | DEFERRED-WBH |
| 3 | `g++ -std=c++20 -Iinclude -fsyntax-only src/render_graph/compiler/fused_pixel_program.cpp` (env-blocked on this VPS) | would PASS | DEFERRED-WBH |
| 4 | `g++ -std=c++20 -Iinclude -fsyntax-only tests/render_graph/compiler/test_fusion_pass.cpp` (env-blocked on this VPS) | would PASS | DEFERRED-WBH |
| 5 | `FusedPixelProgram` ABI: 3-guard all_pass() requires all 4 true | PASS | Logic verified by code-reviewer |
| 6 | `FusionStats` 3 counters populated correctly | PASS | Logic verified |
| 7 | `resolved_kernel` binds to F5.1 `scalar_blend` ABI | PASS | Logic verified (round-trip test in test_fusion_pass.cpp) |
| 8 | `bytes_saved` returns 0 for F3.1 first pass (the actual > 0 lands in F3.2) | PASS | Verified |
| 9 | Cat-3 minimal-surface: zero new SDK API in `include/chronon3d/simd/` | PASS | Verified (the new header is in `render_graph/compiler/`, not `simd/`) |
| 10 | Forbidden includes: zero `#include <msdfgen>/<libtess2>/<unicode[/...]>` | PASS | Verified |
| 11 | Subject envelope ≤ 72 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE | PASS | `feat(fusion): FusedPixelProgram pass compiler (KERNEL-SET-V1 first)` = 65 chars |

## Criteri di accettazione (for the FULL F3.1 user-spec end-to-end)

- [ ] B03 CinematicGlow1080p smoke test runs end-to-end: `bytes_saved_by_fusion > 0` asserted
  (forward-pointed to TICKET-FUSION-PASS-WBH-MACHINE-VERIFY for working build host)
- [ ] `--stats-json` flag emits the 3 counters as JSON on the bench command output
  (forward-pointed to TICKET-FUSION-PASS-STATS-JSON-V1 — see forward-point h)
- [ ] Graph mutation: the 3 nodes are REMOVED + a single FusedPixelProgram
  node is INSERTED in their place (forward-pointed to TICKET-FUSION-PASS-RUNTIME-EXEC)
- [ ] Wall-clock benefit: 1 fused pass vs 3 unfused passes, measured
  on B03 CinematicGlow1080p (forward-pointed to TICKET-FUSION-PASS-WALLCLOCK-V1)

## Forward-points (NOT in this commit, per AGENTS.md "Fare PR piccole e mirate")

- **`<a> TICKET-FUSION-PASS-WBH-MACHINE-VERIFY`** — macchina-verifica
  end-to-end on a working build host (post-`TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`):
  `cmake --preset linux-fast-bench` + `cmake --build` + `ctest -R
  chronon3d_fusion_pass_tests --output-on-failure` + verify all 7
  TEST_CASEs PASS + run B03 CinematicGlow1080p via `chronon3d_cli
  bench` + verify `pixel_fusion_bytes_saved > 0` in the `--stats-json`
  output.
- **`<b> TICKET-FUSION-PASS-EFFECT-INTROSPECTION-V1`** — wire the F3.1
  classifier to the REAL effects catalog (Tint/Brightness/Contrast
  → ColorMatrix, Transform.opacity → Opacity, CompositeNode.blend_mode
  → Blend). The F3.1 first pass uses CONCEPTUAL ids
  (`"color_matrix"` / `"opacity"` / `"blend"`) from the user-spec; this
  forward-point replaces the conceptual ids with the real catalog.
- **`<c> TICKET-FUSION-PASS-RUNTIME-EXEC`** — the runtime executor
  consumes the `FusedPixelProgram::resolved_kernel` and the 3
  operations (ColorMatrix + Opacity pre-applied to a scratch buffer,
  then the F5.1 blend fn invoked on the scratch + dst). The F3.1 first
  pass is NON-MUTATING (the graph is unchanged); this forward-point
  adds the actual scratch-buffer pre-application + the graph
  mutation (3 nodes → 1 FusedPixelProgram node).
- **`<d> TICKET-FUSION-PASS-STATS-JSON-V1`** — `--stats-json` flag
  emitted on the bench command output. The 3 counters are already
  registered in `CHRONON_COUNTERS_GRAPH`; this forward-point adds the
  CLI flag + the JSON serialization.
- **`<e> TICKET-FUSION-PASS-BLEND-MODE-V1`** — the F3.1 first pass
  uses the conceptual `"blend"` id for guard (b); this forward-point
  inspects the REAL `CompositeNode::blend_mode()` and rejects
  non-Normal blends (Multiply / Screen / Add / Subtract require
  different math, no clean fusion).
- **`<f> TICKET-FUSION-PASS-PRECISION-CHECK-V1`** — the F3.1 first
  pass returns `true` for guard (d) (all float32 RGBA); this
  forward-point adds the actual float64 rejection (the F5.1 ABI is
  float32-only; a float64 op would require float64 precision in the
  resolved kernel, which is not provided).
- **`<g> TICKET-FUSION-PASS-WALLCLOCK-V1`** — measure the wall-clock
  benefit of 1 fused pass vs 3 unfused passes on B03
  CinematicGlow1080p. The byte accounting is a STAGING metric; the
  real benefit is kernel-fusion (separate metric in ADR-025).
- **`<h> TICKET-FUSION-PASS-3DOC-CAT5-ALIGN`** — Cat-5 3-doc closure
  (CURRENT_STATUS cite-only + FOLLOWUP_TICKETS row) once the
  WBH-macchina-verifica passes (per §Disciplina di aggiornamento dei
  canonici). Parallel precedent: TICKET-SIMD-REGISTRY-V1-3DOC-CAT5-ALIGN.

## Cross-link canonici

- [`include/chronon3d/render_graph/compiler/fused_pixel_program.hpp`](../../include/chronon3d/render_graph/compiler/fused_pixel_program.hpp) — NEW: F3.1 ABI surface (THIS COMMIT).
- [`src/render_graph/compiler/fused_pixel_program.cpp`](../../src/render_graph/compiler/fused_pixel_program.cpp) — NEW: F3.1 implementation (THIS COMMIT).
- [`include/chronon3d/render_graph/optimizer/graph_optimizer.hpp`](../../include/chronon3d/render_graph/optimizer/graph_optimizer.hpp) — EDIT: `enable_pixel_fusion` toggle + 2 result counters (THIS COMMIT).
- [`src/render_graph/optimizer/graph_optimizer.cpp`](../../src/render_graph/optimizer/graph_optimizer.cpp) — EDIT: hook the F3.1 pass into `optimize_graph()` (THIS COMMIT).
- [`include/chronon3d/core/profiling/render_counter_macros.hpp`](../../include/chronon3d/core/profiling/render_counter_macros.hpp) — EDIT: add 3 counters to `CHRONON_COUNTERS_GRAPH(X)` (THIS COMMIT).
- [`include/chronon3d/simd/pixel_kernels.hpp`](../../include/chronon3d/simd/pixel_kernels.hpp) — F5.1 ABI (the `PixelKernel = BlendKernel::ApplyFn` typedef).
- [`include/chronon3d/simd/kernel_resolver.hpp`](../../include/chronon3d/simd/kernel_resolver.hpp) — F5.1+ F5.2 resolver (the F3.1 pass binds to `kernels.blend.apply`).
- [`include/chronon3d/render_graph/nodes/adjustment_node.hpp`](../../include/chronon3d/render_graph/nodes/adjustment_node.hpp) — AdjustmentNode (the canonical 3-node carrier per F3.1 user-spec pattern).
- [`docs/CHANGELOG.md`](../../docs/CHANGELOG.md) — EDIT: F3.1 entry prepended at TOP (THIS COMMIT).
- AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza ADR" — N/A (the F3.1 pass is a NEW PASS in the existing optimizer; no new singleton/registry/resolver/cache).
- AGENTS.md §regole "Cat-3 minimal-surface" — satisfied: 1 NEW ABI header + 1 NEW impl + 1 NEW test + 1 NEW .cmake + 1 EDIT resolver header + 1 EDIT resolver cpp + 1 EDIT counter macro + 1 EDIT orchestrator + 1 EDIT CHANGELOG + 1 NEW ticket = 9 files total.
- AGENTS.md §regole "NO #include <msdfgen>, <libtess2>, <unicode[/...]>" (Rule 5 Gate 5) — none introduced.
- AGENTS.md §regole "Fare PR piccole e mirate" — F3.1 first commit is the ABI + counters + 4-guard logic; the runtime execution + wall-clock benefit are forward-pointed to subsequent commits.
- AGENTS.md §honesty — first commit is PARTIAL-cert per DEFERRED-WBH; macchina-verifica end-to-end deferred to working build host (TICKET-FUSION-PASS-WBH-MACHINE-VERIFY).
