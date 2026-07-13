# ADR-025 — SIMD kernel registry (PixelKernelSet + CpuIsa enum + resolver)

**Status**: PROPOSED

## Contest

`include/chronon3d/simd/kernels.hpp` (160 LoC public API) ships per-kernel
free functions (`composite_normal_premul(...)`, `premultiply_alpha_rgba8(...)`
`rasterize_rect_simd(...)`, ...). Dispatch happens implicitly via
`HWY_DYNAMIC_DISPATCH(...)` calls inside 6 sibling
`src/backends/software/simd/highway_*_kernels.cpp` files, totaling
**48 HWY_EXPORT/HWY_DYNAMIC_DISPATCH wrappers** machine-verified.

Three parallel dispatch patterns coexist:

1. **Highway dynamic dispatch (48 sites)** — `HWY_DYNAMIC_DISPATCH(fn)` +
   `HWY_EXPORT(fn)` in each TU; each wrapper re-reads `cpuid`/HWCAP
   independently.
2. **Compile-time AVX2 specialization (6+ sites)** — `#if HWY_TARGET ==
   HWY_AVX2` + manual 2-pixel AVX2 path with scalar fallback. Duplicated
   across 2 files.
3. **Direct preprocessor `#if defined(__AVX2__)` (5 sites)** — bypasses
   Highway entirely; only preprocessor guard.

Risk surface:

- **48 independent dispatch reads** — bootstrap cost paid per call-site;
  no reasoning about first-call latency.
- **No `CpuCapabilities`, no `PixelKernelSet`, no resolver** — consumers
  have no first-class handle to test which ISA is bound.
- **No `-march=native` prohibition enforcement** — VPS distributions
  break silently if a contributor adds the flag; the existing 1
  occurrence in `tools/verify_downsample_blur.cpp` is a one-shot recipe
  comment (NOT the product), but no CI gate forbids it.
- **No kernel-ABI contract** — bit-identical vs ε-bounded semantics
  differ between kernels but are not documented.

Per AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza ADR",
this ADR is the required decision-rationale anchor before any new
public-API surface lands in `include/chronon3d/simd/`.

## Decisione

**5 stateless const `PixelKernelSet` statics (one per `CpuIsa`),
returned by const-ref from a pure resolver factory.**

Concretely:

```cpp
namespace chronon3d::simd {
enum class CpuIsa { Scalar, SSE42, AVX2, AVX512, NEON };
struct CpuCapabilities { CpuIsa highest_isa; bool has_sse42, has_avx2,
                          has_avx512, has_neon;
                          [[nodiscard]] bool supports(CpuIsa) const noexcept; };
struct PixelKernelSet { BlurKernel blur; BlendKernel blend; GlowKernel glow;
                         ResampleKernel resample;
                         ColorMatrixKernel color_matrix;
                         CpuIsa isa; };
[[nodiscard]] CpuCapabilities detect_cpu_capabilities() noexcept;
[[nodiscard]] const PixelKernelSet&
    resolve_pixel_kernels(const CpuCapabilities& target) noexcept;
}
```

Each kernel (e.g. `BlurKernel`) is a value type carrying a
`using ApplyFn = void(*)(...);` slot. The 5 scalar slots are bound to
the `detail::scalar_*` reference fn-pointers at compile-time. SIMD
slots (AVX2 / SSE42 / AVX512 / NEON) are bound from the existing 6
`highway_*_kernels.cpp` files AFTER forward-point
TICKET-SIMD-REGISTRY-IMPLEMENTATION migrates them.

ABI contract (single-source, in `pixel_kernels.hpp`):

```cpp
static constexpr float kKernelEpsilon = 1.19e-7f; // 1 ULP float32
```

`scalar = reference`, SIMD = bit-identical OR within `kKernelEpsilon`;
forward-point TICKET-SIMD-REGISTRY-MACHINE-VERIFY certifies cross-ISA
parity post-migration.

`-march=native` prohibition (user-spec verbatim "NON compilare il
prodotto con -march=native"): anchored here + forward-point
TICKET-SIMD-REGISTRY-MARCH-NATIVE-GATE (a separate
`tools/check_no_march_native.sh` machine-checked gate; the gate itself
is NOT in this ADR's scope per Cat-3 anti-dup).

`CHRONON3D_FORCE_CPU_ISA=Scalar|SSE42|AVX2|AVX512|NEON` env var honored
by `detect_cpu_capabilities()` for deterministic test reproducibility
+ post-rot-recovery cross-checks (ADR-020 lineage). Default = host
detection.

## Alternative considerate

- **ALT-A — Global singleton cache** (`static PixelKernelSet
  s_global_kernels;`): rejected per AGENTS.md "no singletons senza
  ADR" + the AGENTS.md-guideline "Cat-3 minimal: prefer DELETE over
  WRAP". The 5-statics const-ref pattern achieves zero-overhead
  without violating the no-cache rule.
- **ALT-B — `std::function` vtable**: rejected for ABI bloat
  (`std::function` carries a heap-pointer indirect + type-erasure
  overhead per call). The fn-pointer slot is
  ABI-stable + zero-overhead at call-site.
- **ALT-C — Compile-time ISA tag dispatch** (`template<CpuIsa>
  resolve_pixel_kernels(...)`): rejected because the SAME binary must
  run on heterogeneous VPS hosts (VPS host might lack AVX2; developer's
  laptop might have AVX-512). Runtime CPU detection wins here.
- **ALT-D — Per-instance RenderRuntime member** (`RenderRuntime::m_simd_kernels`):
  rejected for F5.1 scaffold scope (would expand scope to the
  RenderRuntime class — premature commit). Forward-point to
  TICKET-SIMD-REGISTRY-IMPLEMENTATION §Step-6 "Replace RenderRuntime
  m_simd_kernels member call sites" once the 15 migration sites are
  in flight.

## Conseguenze

POS:
- 5-statics const-ref pattern: zero global mutable state (AGENTS.md
  "no singleton" compliance via const-immutability).
- ABI-stable: 5 fn-pointer slots per kernel family (3 of 5 = trivial-
  copy value types).
- Forward-points cleanly partition the 15-site migration into
  6 ordered atomic commits (TICKET-SIMD-REGISTRY-IMPLEMENTATION
  §Migration order; matches `TICKET-SIMD-REGISTRY-CANONICAL`).
- `-march=native` prohibition has a single canonical anchor.
- Kernel ABI contract (`kKernelEpsilon`) is single-source in
  `pixel_kernels.hpp` — no duplication across 15 sites.

NEG:
- 5 new PUBLIC files in `include/chronon3d/simd/`:
  - `cpu_isa.hpp` (enum + CpuCapabilities + detect_cpu_capabilities)
  - `pixel_kernels.hpp` (5 kernel structs + PixelKernelSet + ABI epsilon)
  - `kernel_resolver.hpp` (resolve_pixel_kernels factory)
  - `detail/scalar_kernels.hpp` (5 scalar reference fn-pointers)
- 3 enumerators (`SSE42` / `AVX512` / `NEON`) require forward-porting
  the existing Highway TU bodies (forward-point
  TICKET-SIMD-REGISTRY-IMPLEMENTATION). For F5.1 scaffold, SIMD
  slots default to scalar — bodies land post-migration.
- Cat-2 zero-new-SDK-API implicit violation: the registry IS new SDK
  surface (the user spec literally adds 5 PUBLIC archetypes + 2
  factories). Justified because (a) ADR-025 anchors the design, (b)
  the alternative (no registry, 48 ad-hoc dispatchers) is worse on
  every dimension listed above, (c) the existing 6 Highway TU files
  are not on `include/chronon3d/` boundary (private detail headers OK).
- Forward-points deferred to working build host:
  macchina-verifica of the resolver's bin-pinning + cross-ISA
  parity requires `chronon3d_cli` compile (env-blocked per
  `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` + `TICKET-BUILD-ROT-CASCADE-CAMERA`).

## Forward-points

(a) **`TICKET-SIMD-REGISTRY-IMPLEMENTATION`** — 15-site migration:
the 6 existing `src/backends/software/simd/highway_*_kernels.cpp` files
must replace their internal `HWY_DYNAMIC_DISPATCH` wrappers with
`m_simd_kernels.<op>.apply(...)` call-sites. Estimated 6 atomic
sub-chore commits per blast-radius minimization.

(b) **`TICKET-SIMD-REGISTRY-MACHINE-VERIFY`** — macchina-verifica:
11/11 baseline suite + `ctest -R simd_resolver` post-impl. Working build
host required.

(c) **`TICKET-SIMD-REGISTRY-MARCH-NATIVE-GATE`** — `-march=native`
machine-checked CI gate: `tools/check_no_march_native.sh` greps
`cmake/` + `src/CMakeLists.txt` + `apps/CMakeLists.txt` + `examples/`
for `-march=native` (with `CHRONON3D_BENCH_BUILD` env override for the
existing `tools/verify_downsample_blur.cpp` recipe comment) and emits
`GATE_FAIL` on match.

(d) **`TICKET-SIMD-FORCE-CPU-ISA-E2E`** — env-var end-to-end test:
`CHRONON3D_FORCE_CPU_ISA` honored across the 6 Highway TU after
migration; idempotency assertion (same target → same resolver return).

(e) **`TICKET-PERSISTENT-CACHE-ADR-GAP`** — sibling ADR-026 for
persistent cache bridge removal (was previously PROPOSED as "ADR-025"
in that ticket body; this ADR-025 supersedes that provisional claim,
so the persistent-cache ADR is bumped to ADR-026 in its forward-point
implementation).

(f) **`TICKET-DESIGN-STEP1-PARTB-THREAD-LOCAL-DI`** — sibling ADR-027
for thread-local DI rationale (was previously PROPOSED as "ADR-025"
in that ticket body; bumped to ADR-027 in its forward-point
implementation).

## ADR-number collision disclosure (§honesty rule)

**Three PROPOSED ADR-025 claims** existed prior to this ADR
(re-tagged to clear the collision):

1. `docs/tickets/TICKET-PERSISTENT-CACHE-ADR-GAP.md` line 76: "ADR-025
   creato in `docs/adr/ADR-025-persistent-cache-bridge-removal.md`"
   (claim only, no file landed).
2. `docs/tickets/TICKET-DESIGN-STEP1-PARTB-THREAD-LOCAL-DI-83a0c4f.md`
   line 275 + line 357: similar claim for "ADR-025-thread-local-DI-
   rationale" (claim only, no file landed).
3. The user's FASE 5.1 spec: "ADR-025 per giustificare il registry".

Per user spec (explicit) + per AGENTS.md Cat-3 anti-dup
(deterministic ADR numbering), this ADR claims **ADR-025 =
SIMD registry**. The 2 prior PROPOSED claims are bumped per
forward-points (e) + (f) above; the bumping is the canonical resolution
to the §honesty corruption risk on ADR numbering (this disclosure is in
this ADR's §Cross-references section per AGENTS.md §Honesty).

## Cross-references

- Canonical ticket: `docs/tickets/TICKET-SIMD-REGISTRY-CANONICAL.md`
  (the design-rationale ticket that scheduled this ADR).
- Implementation ticket: `docs/tickets/TICKET-SIMD-REGISTRY-V1.md`
  (new ticket landed in this same atomic chore; forward-points (a)-(d)).
- AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza
  ADR" — this ADR is the gate.
- AGENTS.md §regole "Cat-3 minimal-surface" — total scope = 4 NEW
  files (1 canonical + 2 detail header) + 1 ADR.
- AGENTS.md §regole "NO #include <msdfgen>, <libtess2>, <unicode[/...]>"
  (Rule 5 Gate 5) — none introduced.
- AGENTS.md §honesty — ADR-numbering collision explicitly disclosed
  (this section "ADR-number collision disclosure").
- Existing public surface: `include/chronon3d/simd/kernels.hpp`
  (preserved unchanged; this ADR adds NEW public surface, NOT
  refactor of existing).
- Existing precedent: `include/chronon3d/core/cpu_budget.hpp`
  env-var-aware factory pattern (mirrors `CHRONON3D_FORCE_CPU_ISA`).

## Owner / Date

- Owner: post-F2 / WBH agent (this chore landed 2026-07-13,
  macchina-verifica DEFERRED-WBH).
- Date: 2026-07-13 (drafted).
