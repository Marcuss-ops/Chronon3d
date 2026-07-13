# TICKET-SIMD-REGISTRY-V1 — SIMD registry implementation (15-site migration + macchina-verifica)

## Stato: OPEN-APERTURA (2026-07-13, post FASE 5.1 scaffold commit)

## Problema

`include/chronon3d/simd/kernels.hpp` (160 LoC public free-function API) +
6 sibling `src/backends/software/simd/highway_*_kernels.cpp` files
dispatch IMS implicitly via 48 `HWY_DYNAMIC_DISPATCH`/`HWY_EXPORT`
wrappers + 6 compile-time `#if HWY_TARGET == HWY_AVX2` specializations
+ 5 direct `#if defined(__AVX2__)` preprocessor guards, totaling
**15 site-level machine-verified SIMD touchpoints**.

FASE 5.1 (TICKET-SIMD-REGISTRY-V1 scaffold) lands the public API
surface in `include/chronon3d/simd/`:

- `cpu_isa.hpp` — `enum class CpuIsa { Scalar, SSE42, AVX2, AVX512, NEON }`
  + `struct CpuCapabilities` + `detect_cpu_capabilities()` (env-aware).
- `pixel_kernels.hpp` — `BlurKernel`/`BlendKernel`/`GlowKernel`/
  `ResampleKernel`/`ColorMatrixKernel` + `PixelKernelSet` (user-spec
  verbatim shape) + ABI epsilon `kKernelEpsilon` (1 ULP float32).
- `kernel_resolver.hpp` — `resolve_pixel_kernels(const CpuCapabilities&)`
  returning `const PixelKernelSet&` (5-statics const-ref pattern).
- `detail/scalar_kernels.hpp` — 5 scalar reference fn-pointers
  (the `Scalar` CpuIsa binding; SIMD bindings deferred to this ticket §Step 6).
- `docs/adr/ADR-025-simd-registry.md` — design rationale + ADR-025
  number collision disclosure (3 PROPOSED ADR-025 claims resolved by
  bumping siblings to ADR-026/ADR-027).

This ticket (TICKET-SIMD-REGISTRY-V1 = the IMPLEMENTATION ticket, NOT
the canonical-design ticket) covers the 15-site migration + macchina-
verifica + ABI parity certification.

## AGENTS.md compliance

- **No nuovi singleton/registry/resolver/cache senza ADR**: ADR-025
  landed in the scaffold commit (F5.1); this ticket CAN now land.
- **Cat-3 minimal-surface**: 4 NEW public headers (canonical) + 1 ADR
  + 1 ticket + 1 CHANGELOG entry. ZERO new symbol beyond user-spec.
- **NO `#include <msdfgen>`, `<libtess2>`, `<unicode[/...]>`** (Rule 5
  Gate 5): none introduced.
- **NO GUI/GPU**: pure headless CPU-first (per AGENTS.md §regole
  "Non introdurre GUI, browser o dipendenze GPU").

## Soluzione (6 ordered atomic sub-chore commits)

| Step | File(s) | Action | Risk |
|------|---------|--------|------|
| **1** | `highway_color_kernels.cpp` + `highway_rasterize_kernels.cpp` | Replace `HWY_DYNAMIC_DISPATCH` wrappers with `<runtime>.m_simd_kernels.<op>.apply(...)` | LOW (smallest TU; smoke-test on these 2 first) |
| **2** | `highway_conversion_kernels.cpp` | Replace 12 dispatch wrappers + 2 `#if HWY_TARGET` specializations | MEDIUM (AVX2 specialization complexity) |
| **3** | `highway_blend_kernels.cpp` | Replace 12 dispatch + 4 AVX2 specialization (hot path) | HIGH (canonical blend ops; verify with golden cb-test) |
| **4** | `highway_dof_kernels.cpp` + `highway_separable_kernels.cpp` | Replace 4 + 18 dispatch wrappers | MEDIUM (separable is biggest family) |
| **5** | cross-cutting: `include/.../per_pixel_dof.hpp` + `sampling_utils.hpp` + `composition.cpp` + `tests/bench/dof_benchmark.cpp` | Replace `using namespace hwy::HWY_NAMESPACE` + inline LoadU/StoreU with `engine.runtime->m_simd_kernels.<op>` | MEDIUM |
| **6** | preprocessor guard sites: `pip.{hpp,cpp}` + `effect_color.cpp` + `blend2d_bridge_{core,transforms_fb}.cpp` | Replace `#if defined(__AVX2__)` with branch via m_simd_kernels | LOW (already nullable) |

Each step lands as an atomic commit with subject = `chore(simd):
STEP-N: <token> (15-site unification)` (≤ 72 char; user-spec
verbatim envelope constraint).

## Criteri di accettazione

- [ ] 6 steps landed as 6 atomic commits on `main`.
- [ ] `rg 'HWY_DYNAMIC_DISPATCH' src/ include/` → **0 matches** (machine-verified).
- [ ] `rg 'using namespace hwy::HWY_NAMESPACE' src/ include/` → **0 matches** (machine-verified).
- [ ] `rg '#if defined\\(__AVX2__\\)' src/backends/software/rasterizers/path/ src/backends/software/utils/effects/effect_color.cpp src/backends/software/utils/blend2d_bridge_*.cpp` → **0 matches** (the 5 preprocessor-guard sites; AVX2 detection now via `detect_cpu_capabilities()`).
- [ ] `bash tools/check_architecture_boundaries.sh` → exit 0 (the existing rule #11 covers the no-duplicate-definition invariant for `CpuCapabilities`).
- [ ] 11/11 baseline suite verde su working build host post-migration.
- [ ] Cross-ISA parity golden test (per TICKET-SIMD-REGISTRY-MACHINE-VERIFY forward-point): `for isa in Scalar SSE42 AVX2 AVX512 NEON; do ./bin/test_simd_parity --isa=$isa; done` → all 5 ISA outputs equal within `kKernelEpsilon` (1 ULP float32).

## Constraints (= scaffold contract from F5.1)

- **5 kernel ABI stays canonical** (`BlurKernel`/`BlendKernel`/
  `GlowKernel`/`ResampleKernel`/`ColorMatrixKernel`; each = fn-pointer
  value type with `ApplyFn` typedef; ABI epsilon `kKernelEpsilon`).
- **`-march=native` prohibition** must be enforced via
  TICKET-SIMD-REGISTRY-MARCH-NATIVE-GATE (forward-point (c) on
  ADR-025). NO additions of `-march=native` in any submodule build
  file.
- **`CHRONON3D_FORCE_CPU_ISA` env var** must be honored end-to-end
  (post-step-6 idempotency assertion per forward-point (d) on ADR-025).

## Out of scope (this ticket)

- ADR-025 itself: ALREADY LANDED in F5.1 scaffold commit.
- Forward-points (a) Implementation, (c) march-native-gate, (d)
  force-CPU-ISA-E2E — see ADR-025 §Forward-points.
- Cross-ISA macchina-verifica cert → forward-pointed to
  TICKET-SIMD-REGISTRY-MACHINE-VERIFY for working build host.

## Forward-points

- `TICKET-SIMD-REGISTRY-MACHINE-VERIFY` — WBH 11/11 + cross-ISA parity.
- `TICKET-SIMD-REGISTRY-MARCH-NATIVE-GATE` —
  `tools/check_no_march_native.sh` CI gate.
- `TICKET-SIMD-FORCE-CPU-ISA-E2E` — env-var end-to-end idempotency.

## Cat-5 2-doc same-commit (this scaffold commit)

- `docs/CHANGELOG.md` EDIT: F5.1 entry prepended at TOP per Cat-5
  newer-at-top convention; cite-only ticket link per AGENTS.md §github
  rule "Disciplina di aggiornamento dei canonici" + Section Cronaca-home
  for ticket = canonical site.
- `docs/tickets/TICKET-SIMD-REGISTRY-V1.md` NEW: this entire file.
- `docs/FOLLOWUP_TICKETS.md` INTENTIONALLY UNTOUCHED per Cat-3: the
  canonical-design ticket (`TICKET-SIMD-REGISTRY-CANONICAL`) already
  carries the OPEN row at §Open Blockers; the IMPLEMENTATION ticket
  (this one) inherits the same row (no new OPEN row needed).

## Cross-link

- `include/chronon3d/simd/{cpu_isa,pixel_kernels,kernel_resolver,
  detail/scalar_kernels}.hpp` (NEW in F5.1 scaffold commit).
- `include/chronon3d/simd/kernels.hpp` (existing 160-LoC public free-fn
  API — preserved unchanged across F5.1; rolled-in during step-1 step-3
  migration).
- `docs/adr/ADR-025-simd-registry.md` (NEW in F5.1).
- `docs/tickets/TICKET-SIMD-REGISTRY-CANONICAL.md` (the design-rationale
  parent ticket — this V1 ticket is the IMPLEMENTATION child).
- AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza
  ADR" — ADR-025 satisfies.
- AGENTS.md §regole "Cat-3 minimal-surface" — 4 NEW header + 2 docs.
content>