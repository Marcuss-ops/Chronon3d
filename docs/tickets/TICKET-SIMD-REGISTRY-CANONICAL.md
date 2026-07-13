# TICKET-SIMD-REGISTRY-CANONICAL — SIMD Registry Canonical (8+ Highway sites → 1 source of truth)

## Stato: OPEN-APERTURA (2026-07-13, opening-only)

## Problema
Il codice Chronon3D consuma la libreria SIMD **Highway** (include path `#include <hwy/...>`) in modo autonomo in **8+ translation unit**. Ciascun sito determina l'ISA supportato via 3 pattern paralleli + non-coordinati:

1. **`HWY_DYNAMIC_DISPATCH(fn)` wrapper per-TU** — `src/backends/software/simd/highway_blend_kernels.cpp` ridefinisce 5 dispatch helpers (`composite_normal_premul_dispatch`, `composite_add_premul_dispatch`, `composite_multiply_premul_dispatch`, `composite_screen_premul_dispatch`, `composite_overlay_premul_dispatch`); totali across 5 Highway TU: **separable=18, blend=12, conversion=12, dof=4, rasterize=2 = 48 dispatch wrappers locali**. Ciascun wrapper re-rilegge `cpuid`/platform-detection via `HWY_DYNAMIC_DISPATCH` invece di consumare un singleton-detection cached.
2. **`#if HWY_TARGET == HWY_AVX2` compile-time specialization** — `highway_blend_kernels.cpp:32, 91, 129, 194` + `highway_conversion_kernels.cpp:54, 176` — specializza path 2-pixel AVX2 con fallback scalare. Questa specializzazione è duplicata 6+ volte in 2 file.
3. **`#if defined(__AVX2__) && !defined(CHRONON3D_FORCE_SCALAR_BLEND)` direct preprocessor dispatch** — `src/backends/software/rasterizers/path/pip.{hpp,cpp}` (3 siti) + `src/backends/software/utils/blend2d_bridge_{core,transforms_fb}.cpp` (3 siti) — bypass totale di Highway, solo preprocessor guard.

Risultato: **48 HWY_EXPORT/HWY_DYNAMIC_DISPATCH wrappers + 6 AVX2 preprocessor guard + 2 file con `using namespace hwy::HWY_NAMESPACE` direct inline + 4 file con `#include <hwy/...>` direct**. Non esiste una `CpuCapabilities`, non esiste un `PixelKernelSet`, non esiste un `resolve_pixel_kernels(const CpuCapabilities&)`. Il dispatcher è ri-rileggibile da 48+ siti indipendenti.

## Specification (user-supplied, 2026-07-13)
- `enum class CpuIsa { Scalar, SSE42, AVX2, AVX512, NEON }`.
- `struct PixelKernelSet` (C function-pointer vtable: blend ops, dof ops, color ops, conversion ops; zero overhead; trivialmente copiabile).
- `[[nodiscard]] PixelKernelSet resolve_pixel_kernels(const CpuCapabilities& target) noexcept`.
- `[[nodiscard]] CpuCapabilities detect_cpu_capabilities() noexcept`.
- `struct CpuCapabilities { CpuIsa highest_isa; bool has_sse42; bool has_avx2; bool has_avx512; bool has_neon; }`.
- 8+ Highway sites consumano il **resolver**; `HWY_DYNAMIC_DISPATCH` wrapper per-TU RIMOSSO (single source of truth).

## Scope (ticket opening, NOT implementazione)
1. **ADR-025-simd-resolver-canonical.md** — design rationale + tradeoff + rejected alternatives (AGENTS.md rule: NO new singleton/registry/resolver/cache senza ADR).
2. **NEW `include/chronon3d/simd/cpu_capabilities.hpp`** — `enum class CpuIsa` + `struct CpuCapabilities` + `[[nodiscard]] CpuCapabilities detect_cpu_capabilities() noexcept` (single header ABI-stable).
3. **NEW `include/chronon3d/simd/dispatch.hpp`** — `struct PixelKernelSet` (function-pointer vtable) + `[[nodiscard]] PixelKernelSet resolve_pixel_kernels(const CpuCapabilities& target) noexcept` (pure factory, no global state).
4. **Migrazione 8+ siti** (vedi "8+ Highway sites" §sotto) consumando `PixelKernelSet` via membro `m_simd_kernels` di `RenderRuntime`/`RenderEngine` (cache locale istanza-bound, **NON singleton globale** per AGENTS.md compliance).
5. **Test idempotenza**: `test_simd_resolver_idempotent` cicla `CpuIsa ∈ {Scalar, SSE42, AVX2, AVX512, NEON}`, AND-mask con `detect_cpu_capabilities()` per abilitare solo le ISA supportate dall'host corrente (evita SIGILL su host senza AVX2).
6. **Cat-5 closure**: 1 NEW ticket file (`docs/tickets/TICKET-SIMD-REGISTRY-CANONICAL.md` questo) + 1 row in `docs/FOLLOWUP_TICKETS.md` (apertura) + 1 entry prepended in `docs/CHANGELOG.md`.

## AGENTS.md compliance
- **No singletons/registry/resolver/cache senza ADR** (rule hard-line): ADR-025 obbligatorio precedent all'implementazione. ✓ SPEC'd.
- **Cat-3 minimal-surface**: 2 nuovi header + 8 siti edit (sostituzione, non aggiunta) + 1 test file. NO new SDK semantic oltre il prototipo (resolver struct + function-pointer vtable + enum class).
- **NO `#include <msdfgen>`, `<libtess2>`, `<unicode[/...]>`** (rule #5 Gate 5): nuovo codice NON introduce dipendenze divietate.
- **NO GUI/browser/GPU dependencies** (rule #1): pure headless CPU-first.
- **NO `#define CHRONON3D_*` globali invasive** (rule #3): il env override `CHRONON3D_FORCE_CPU_ISA` è opt-in per test, NON in produzione.

## 8+ Highway sites (machine-verified list, 2026-07-13)

HWY namespace users (`using namespace hwy::HWY_NAMESPACE` oppure `namespace hn = hwy::HWY_NAMESPACE`):
1. `src/backends/software/simd/highway_blend_kernels.cpp` (12 HWY_EXPORT/HWY_DYNAMIC_DISPATCH + 4 `#if HWY_TARGET == HWY_AVX2`)
2. `src/backends/software/simd/highway_color_kernels.cpp` (0 dispatch, pure scalar fallback)
3. `src/backends/software/simd/highway_conversion_kernels.cpp` (12 HWY_EXPORT + 2 `#if HWY_TARGET == HWY_AVX2`)
4. `src/backends/software/simd/highway_dof_kernels.cpp` (4 HWY_EXPORT)
5. `src/backends/software/simd/highway_rasterize_kernels.cpp` (2 HWY_EXPORT)
6. `src/backends/software/simd/highway_separable_kernels.cpp` (18 HWY_EXPORT)
7. `include/chronon3d/backends/software/utils/effects/per_pixel_dof.hpp` (uso `using namespace hwy::HWY_NAMESPACE` + chiamata inline LoadU/StoreU su framebuffer pre-HWY_AVX2 dispatch)
8. `tests/bench/dof_benchmark.cpp` (uso Highway in benchmark harness)

Direct `#include <hwy/...>` users (no namespace alias):
9. `src/render_graph/nodes/sampling_utils.hpp` (4: `#include <hwy/highway.h>` + `namespace hn = hwy::HWY_NAMESPACE`)
10. `src/render_graph/pipeline/composition.cpp` (2 sites `using namespace hwy::HWY_NAMESPACE` + LoadU/StoreU su VerticalPass)

Direct preprocessor `#if defined(__AVX2__)` users (bypass Highway):
11. `src/backends/software/rasterizers/path/pip.hpp` (1 site)
12. `src/backends/software/rasterizers/path/pip.cpp` (2 sites)
13. `src/backends/software/utils/effects/effect_color.cpp` (1 site)
14. `src/backends/software/utils/blend2d_bridge_transforms_fb.cpp` (2 sites)
15. `src/backends/software/utils/blend2d_bridge_core.cpp` (2 sites)

**Totale: 15 siti** (≥ 8 utenti confermati).

## Migration order (blast-radius minimizzato)

- **Step 1**: `highway_color_kernels.cpp` (smallest, 0 dispatch) + `highway_rasterize_kernels.cpp` (2 dispatch). Introducono il pattern `m_simd_kernels::color_ops` + `m_simd_kernels::raster_ops`. Smoke-test: i 2 file compilano + idempotenza OK.
- **Step 2**: `highway_conversion_kernels.cpp` (12 dispatch + 2 AVX2 specialization). Migrare a `m_simd_kernels::conversion_ops` con specializzazione AVX2 esplicita nel vtable.
- **Step 3**: `highway_blend_kernels.cpp` (12 dispatch + 4 AVX2 specialization — il caso d'uso più caldo). Sostituire 5 `composite_*_premul_dispatch` con chiamate dirette a `m_simd_kernels::blend_ops.{normal, add, multiply, screen, overlay}`.
- **Step 4**: `highway_dof_kernels.cpp` (4 dispatch) + `highway_separable_kernels.cpp` (18 dispatch — bigger aggregation). Migrare a `m_simd_kernels::dof_h_gather` + `m_simd_kernels::dof_v_gather` + `m_simd_kernels::separable_*`.
- **Step 5 (cross-cutting)**: `include/.../per_pixel_dof.hpp` (uso inline di Highway) + `sampling_utils.hpp` + `composition.cpp` (2 siti inline) + `tests/bench/dof_benchmark.cpp` (test-only). Dopo Step 1-4, sostituire `using namespace hwy::HWY_NAMESPACE` con accesso via `pb->engine.runtime->m_simd_kernels.<op>`.
- **Step 6 (preprocessor guard sites)**: `pip.{hpp,cpp}` + `effect_color.cpp` + `blend2d_bridge_{core,transforms_fb}.cpp` (5 siti). Sostituire `#if defined(__AVX2__) && !defined(CHRONON3D_FORCE_SCALAR_BLEND)` con branch su `m_simd_kernels.<op>` (compile-time ISA detection del resolver + runtime branching al call-site).

## Criteri di accettazione
- [ ] ADR-025 merged in `docs/adr/`.
- [ ] `include/chronon3d/simd/cpu_capabilities.hpp` + `dispatch.hpp` esistono (ABI-stable, NO global statics).
- [ ] 15 siti Highway migrati; `rg 'HWY_DYNAMIC_DISPATCH' src/ include/` → **0 matches** (machine-verified).
- [ ] `rg 'using namespace hwy::HWY_NAMESPACE' src/ include/` → **0 matches** (machine-verified).
- [ ] `rg '#if defined\(__AVX2__\)' src/` → **0 matches nei siti Highway** (5 siti preprocessor guard sostituiti; AVX2 detection ora via `detect_cpu_capabilities()`).
- [ ] Test `test_simd_resolver_idempotent` registered + cicla 5 CpuIsa; AND-mask su `detect_cpu_capabilities()`.
- [ ] 11/11 baseline suite verde su working build host post-migration.
- [ ] `CHRONON3D_FORCE_CPU_ISA=Scalar|AVX2|AVX512|SSE42|NEON` env var honoured dal `detect_cpu_capabilities()` (per test + repro deterministico).

## Constraints
- **Cat-3 zero-new-SDK-API**: solo 2 nuovi header pubblici `cpu_capabilities.hpp` + `dispatch.hpp` (ABI-stable, non template-bound, no inline-heavy). NO nuove funzioni membro su `RenderRuntime` oltre `m_simd_kernels` accessor.
- **AGENTS.md "no singleton senza ADR"**: il `m_simd_kernels` è membro d'istanza, NON globale (cache locale ABI-stable per ISO C++ standard).
- **NO new `add_executable`/`target_*` CMake targets**: il test `test_simd_resolver_idempotent` riusa un esistente `chronon3d_*_tests` target per Cat-3 anti-dup.
- **Subject envelope**: `chore(simd): SIMD resolver canonical (8 site unification)` ≤ 72 char.
- **CHANGELOG envelope**: 1 entry prepended (canonical pattern + ticket-link).

## Out of scope (this chore = ticket opening + ADR draft ONLY)
- Implementation chore (Step 1-6 migrations) DEFERRED a `TICKET-SIMD-REGISTRY-IMPLEMENTATION` (separate chore).
- Implementation macchina-verifica → deferred a working build host per `TICKET-BUILD-ROT-CASCADE-CAMERA` + `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent.

## Forward-points
- (a) **`TICKET-ADR-025-SIMD-RESOLVER`** — preprocessore ADR con decision rationale + rejected alternatives: (alt-r) global singleton cache `static PixelKernelSet s_global_kernels` (BOCIED per AGENTS.md "no new singleton"); (alt-r) `std::function` vtable (BOCIED per overhead zero-overhead-richiesto); (alt-r) interceptor compile-time tag dispatch senza runtime detection (BOCIED perdita di dynamic dispatch). Design vincente: function-pointer vtable risolta a construction-time di `RenderRuntime` (cached in member), ABI-stable, zero-overhead at call-site, no global state.
- (b) **`TICKET-SIMD-REGISTRY-IMPLEMENTATION`** — implementazione: 2 nuovi header + 15 site migration (Step 1-6 ordered) + 1 test `test_simd_resolver_idempotent`. Subject target: `chore(simd): SIMD resolver canonical (15 site unification)`. Estimated 6 atomic sub-chore commits per blast-radius minimizzato.
- (c) **`TICKET-SIMD-REGISTRY-MACHINE-VERIFY`** — 11/11 baseline suite verde + `ctest -R simulate_resolver` post-impl. Working build host.
- (d) **`TICKET-SIMD-FORCE-CPU-ISA`** — env var `CHRONON3D_FORCE_CPU_ISA=Scalar|AVX2|AVX512|SSE42|NEON` per test deterministico + repro consistency check.
- (e) **`TICKET-SIMD-REGISTRY-3DOC-CAT5-ALIGN`** — Cat-5 3-doc deferred closure (CHANGELOG + FOLLOWUP questa ticket + currenT_status deferrable per AGENTS.md §Honesty).

## Cross-link
- `include/chronon3d/simd/kernels.hpp` (header PixelKernels attualmente esistente — il primo passo è AUDIT per capire se può essere esteso o sostituito).
- `AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza ADR"` — rule hard-line che QA questo ticket.
- `AGENTS.md §regole "Cat-3 minimal-surface"` — vincola scope a 2 header + 15 edit (no new semantic).
- `AGENTS.md §regole "NO #include <msdfgen>, <libtess2>, <unicode[/...]>"` (Rule 5 Gate 5) — nuovo codice NON introduce queste dipendenze divietate.
- `AGENTS.md §honest-limitation` — macchina-verifica DEFERRED to working build host per TICKET-BUILD-ROT-CASCADE-CAMERA + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV precedent.
- I 15 siti Highway (machine-verified):
  - `src/backends/software/simd/highway_{blend,color,conversion,dof,rasterize,separable}_kernels.cpp` (6 file .cpp)
  - `include/chronon3d/backends/software/utils/effects/per_pixel_dof.hpp` (uso inline)
  - `tests/bench/dof_benchmark.cpp` (benchmark harness only)
  - `src/render_graph/nodes/sampling_utils.hpp` (uso inline `namespace hn = hwy::HWY_NAMESPACE`)
  - `src/render_graph/pipeline/composition.cpp` (2 siti `using namespace hwy::HWY_NAMESPACE`)
  - `src/backends/software/rasterizers/path/pip.{hpp,cpp}` (3 siti `#if defined(__AVX2__)`)
  - `src/backends/software/utils/effects/effect_color.cpp` (1 sito `#if defined(__AVX2__)`)
  - `src/backends/software/utils/blend2d_bridge_{core,transforms_fb}.cpp` (4 siti `#if defined(__AVX2__)`)

## Idempotency / re-runnable
- Il ticket opening NON è runnable (è una dichiarazione di scope, no side effects).
- L'implementazione forward-pointed sarà riapplicabile `git revert` + `git cherry-pick` per re-run idempotency preservata (lineare history).
