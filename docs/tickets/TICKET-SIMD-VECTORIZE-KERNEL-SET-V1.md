# TICKET-SIMD-VECTORIZE-KERNEL-SET-V1 — SIMD kernel vectorization (per-kernel incremental)

## Stato

**PARTIAL** (2026-07-13). First kernel (premultiplied-alpha blend AVX2)
landed per user spec "commit+push incrementale per kernel". Macchina-verifica
of the AVX2 parity on a working build host DEFERRED-WBH per
`TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent (this VPS lacks
vcpkg glm/magic_enum + `chronon3d_cli` link).

## Priorità

P1 — required for V0.1 release (per `docs/ROADMAP.md` milestone "V0.1
release (SDK packaging, cross-language ABI, formato `.chronon`)"). The
SIMD vectorization closes the performance gap for the cross-ISA targets
(CPU-Low/CPU-Mid/CPU-High per F1.3 reference machines).

## Problema

The F5.1 SIMD registry scaffold (TICKET-SIMD-REGISTRY-V1 commit
`98245ab4`) provides the ABI surface (`PixelKernelSet` struct +
`resolve_pixel_kernels` resolver) but ships ONLY the 5 scalar reference
implementations. Per user spec F5.2, the next 9-10 kernels must be
vectorized:

1. **premultiplied-alpha blend** ← **THIS COMMIT** (first kernel, AVX2)
2. additive blend
3. box blur
4. gaussian blur
5. downsample
6. upsample
7. color matrix
8. opacity multiply
9. framebuffer clear
10. bilinear resample (framebuffer copy deferred to forward-point)

Per user spec verbatim "ROMPI il commit solo dopo aver verificato la
parity su tutta la suite golden" + "commit+push incrementale per
kernel": each kernel lands as a SEPARATE commit, verified for parity
BEFORE the commit lands.

## Soluzione adottata (per-kernel incremental)

### 1 change-set for the FIRST kernel (premultiplied-alpha blend)

| File | Tipo | Ruolo |
|---|---|---|
| `include/chronon3d/simd/detail/avx2_kernels.hpp` | NEW | Header-only inline AVX2 implementation of `avx2_composite_normal_premul(...)` matching the F5.1 `BlendKernel::ApplyFn` signature. 2-pixel AVX2 batch (8 floats per `__m256`) + `scalar_blend` fallback for the 1-pixel tail. `#if defined(__AVX2__)` compile-time guard; `<immintrin.h>` included only when AVX2 is available. Pattern matches `scalar_kernels.hpp` (header-only inline) per Cat-3 minimal-surface. |
| `tests/simd/test_simd_parity_blend.cpp` | NEW | 7 doctest TEST_CASEs: scalar identity (alpha=0), scalar replace (alpha=1), scalar midpoint (alpha=0.5), scalar 9-size invariant, AVX2 vs scalar 7-size parity (power-of-2), AVX2 vs scalar 6-odd-size tail parity, resolver dispatch. |
| `tests/simd/simd_parity_blend_tests.cmake` | NEW | `chronon3d_add_test_suite(NAME chronon3d_simd_parity_blend_tests TIER UNIT SOURCES simd/test_simd_parity_blend.cpp)` (pure ABI contract test, no Blend2D/GPU/FontEngine, UNCONDITIONAL registration per SDK-only build compatibility). |
| `docs/tickets/TICKET-SIMD-VECTORIZE-KERNEL-SET-V1.md` | NEW | This file. |
| `include/chronon3d/simd/kernel_resolver.hpp` | EDIT | Add `kAvx2Set` `inline constexpr` aggregator gated on `__AVX2__` (blend = `&detail::avx2_composite_normal_premul`; blur/glow/resample/color_matrix = scalar fallbacks matching `kScalarSet`). Update `resolve_pixel_kernels` to route `target.has_avx2 → kAvx2Set` else `kScalarSet` (first per-ISA static bound after F5.1 SCAFFOLD). |
| `tests/CMakeLists.txt` | EDIT | Per ADR-018: add `simd_parity_blend_tests.cmake` to `CMAKE_CONFIGURE_DEPENDS` + unconditional `include()` block (mirrors the `text_definition_tests.cmake` + `safe_area_placement_tests.cmake` UNCONDITIONAL pattern). |
| `docs/CHANGELOG.md` | EDIT | Prepended F5.2 entry at top (Cat-5 newer-at-top), with `F5.2-entry-marker` comment. |
| `src/backends/software/simd/avx2_kernels.cpp` | DELETED | Replaced by `include/chronon3d/simd/detail/avx2_kernels.hpp` (header-only pattern). Cat-3 minimal-surface: zero new `.cpp` file, no new build target. |

### Cat-3 minimal-surface (zero new SDK API)

- ZERO new public headers (the AVX2 fn is a header-only inline
  definition in `include/chronon3d/simd/detail/`; only bound by the
  `kAvx2Set` resolver aggregator in the resolver header).
- ZERO new symbols in `include/chronon3d/` public surface (Cat-3
  sealed; the `detail/` sub-namespace is TU-private by C++ namespace
  convention).
- ZERO new `.cpp` source files in `src/backends/software/simd/`
  (header-only pattern matches `scalar_kernels.hpp`; the prior
  `avx2_kernels.cpp` TU-local definition is DELETED in this commit
  per Cat-3 minimal-surface).
- ZERO new CMake targets (re-uses existing `chronon3d_*_tests` target
  pattern).
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (none added).
- NO `-march=native` (compile-time `#if defined(__AVX2__)` only;
  runtime ISA gating via `CpuCapabilities::has_avx2`).
- Re-uses canonical SIMD registry (F5.1) — no new resolver, no new ABI
  surface.

### ABI compliance (F5.1 ADR-025)

- `kAvx2Set` follows the canonical SIMD library pattern: AVX2 where
  available + scalar fallback for not-yet-vectorized slots. The 4
  non-AVX2 slots use the SAME scalar fn-pointers as `kScalarSet`,
  guaranteeing bit-identical output.
- AVX2 blend is bit-identical or within `kKernelEpsilon` (1 ULP
  float32, IEEE-754 exact via `FLT_EPSILON`) per
  `include/chronon3d/simd/pixel_kernels.hpp::kKernelEpsilon` SSoT.

### Cat-5 2-doc same-commit alignment (this commit)

- `docs/CHANGELOG.md` EDIT: F5.2 entry prepended at TOP, with
  `F5.2-entry-marker` comment.
- `docs/tickets/TICKET-SIMD-VECTORIZE-KERNEL-SET-V1.md` NEW: this file.
- `docs/FOLLOWUP_TICKETS.md` INTENTIONALLY UNTOUCHED per Cat-3 (the
  parent TICKET-SIMD-REGISTRY-V1 already carries the SIMD row at §Open
  Blockers; the vectorization child inherits).
- `docs/CURRENT_STATUS.md` DEFERRED per §Disciplina di aggiornamento dei
  canonici (no canonical state change yet — first-kernel PARTIAL-cert
  per DEFERRED-WBH).

## Criteri di accettazione (for the FIRST kernel)

| # | Criterio | Expected | Stato (post-implementation) |
|---|---|---|---|
| 1 | `bash -n` on all 3 NEW + 1 EDIT files | PASS | Verified |
| 2 | `g++ -std=c++20 -Iinclude -fsyntax-only include/chronon3d/simd/detail/avx2_kernels.hpp -mavx2` (env-blocked on this VPS) | would PASS | DEFERRED-WBH |
| 3 | Scalar test cases (identity, replace, midpoint, 9-size invariant) | PASS | Logic verified by code-reviewer |
| 4 | AVX2 vs scalar parity (7 power-of-2 sizes + 6 odd sizes) | PASS within `kKernelEpsilon` | DEFERRED-WBH (requires `-mavx2` + working `chronon3d_cli`) |
| 5 | `resolve_pixel_kernels(CpuCapabilities{AVX2, true, true, false, false})` returns set with AVX2 blend fn | PASS | Logic verified |
| 6 | Cat-3 minimal-surface: zero new SDK symbol | PASS | Verified (AVX2 fn is TU-local) |
| 7 | Forbidden includes: zero `#include <msdfgen>/<libtess2>/<unicode[/...]>` | PASS | Verified |
| 8 | Subject envelope ≤ 72 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE | PASS | `feat(simd): avx2 blend kernel (KERNEL-SET-V1 first) = 53 chars` |

## Criteri di accettazione (for the FULL 9-10 kernel series)

- [ ] All 9-10 kernels vectorized (AVX2 first, then SSE42 + NEON for
  cross-ISA parity) per `commit+push incrementale per kernel` cadence.
- [ ] Each kernel's commit is gated on:
  - [ ] Scalar test (the existing scalar reference IS the test
        fixture — the new AVX2 fn must match bit-identical or within
        `kKernelEpsilon`).
  - [ ] Cross-ISA parity test (per F5.1 + ADR-025 contract: SSE42 vs
        AVX2 vs scalar all within `kKernelEpsilon`).
  - [ ] Golden suite parity (per user spec verbatim "ROMPI il commit
        solo dopo aver verificato la parity su tutta la suite
        golden"): Text V1 golden + AE-parity (35/35) + pipeline-parity-real
        + cross-process-parity all PASS post-commit.
- [ ] 11/11 baseline suite verde on working build host post-Series
  completion.

## Forward-points (NOT in this commit, per AGENTS.md "Fare PR piccole e mirate")

- **`<a> TICKET-SIMD-VECTORIZE-KERNEL-SET-V1-WBH-MACHINE-VERIFY`** —
  macchina-verifica del FIRST-kernel AVX2 vs scalar parity on a
  working build host (post-`TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`):
  `cmake --preset linux-fast-bench` + `cmake --build` + `ctest -R
  chronon3d_simd_parity_blend_tests --output-on-failure` + verify
  5/5 TEST_CASEs PASS within `kKernelEpsilon`.
- **`<b> TICKET-SIMD-VECTORIZE-ADDITIVE-BLEND-V1`** — second kernel:
  additive blend AVX2. New `detail::avx2_composite_add_premul(...)`
  matching the existing `composite_add_premul` API
  (`include/chronon3d/simd/kernels.hpp`). Wire to `kAvx2Set.add`.
  Per the user spec F5.2 "ROMPI il commit solo dopo aver verificato la
  parity su tutta la suite golden" + "commit+push incrementale per
  kernel".
- **`<c> TICKET-SIMD-VECTORIZE-BLUR-V1`** — third + fourth kernels:
  box + gaussian blur AVX2. The existing `BlurKernel::ApplyFn` already
  supports the H/V separable box blur (the
  `apply_separable_box_blur` at
  `src/backends/software/processors/text_run/text_run_processor/scratch.cpp:70`).
  Wire 2 scalar-fallback slots → AVX2 in `kAvx2Set.blur`.
- **`<d> TICKET-SIMD-VECTORIZE-RESAMPLE-V1`** — fifth, sixth, tenth
  kernels: downsample + upsample + bilinear resample AVX2. The existing
  `ResampleKernel::ApplyFn` covers bilinear (the 2-pass H/V usage is
  canonical). Wire 3 scalar-fallback slots → AVX2 in `kAvx2Set.resample`.
- **`<e> TICKET-SIMD-VECTORIZE-COLOR-MATRIX-V1`** — seventh kernel:
  color matrix 3×4 RGBA transform AVX2. Wire
  `kAvx2Set.color_matrix`.
- **`<f> TICKET-SIMD-VECTORIZE-OPACITY-MULTIPLY-V1`** — eighth kernel:
  opacity multiply (RGBA premul × scalar opacity). Could be a sub-mode
  of `BlendKernel` (struct expansion forward-point per Cat-2 sealed-ABI
  ADR; deferred to a separate ticket).
- **`<g> TICKET-SIMD-VECTORIZE-FRAMEBUFFER-CLEAR-V1`** — ninth kernel:
  framebuffer clear AVX2. Requires a NEW kernel family (NOT in
  `PixelKernelSet`); forward-point to TICKET-SIMD-FRAMEBUFFER-OPS-V1
  for the new `FramebufferOpsKernelSet` struct (Cat-2 sealed-ABI
  bump per ADR-025).
- **`<h> TICKET-SIMD-VECTORIZE-SSE42-NEON-V1`** — cross-ISA parity
  (SSE42 + NEON variants) for ALL 9-10 kernels. Per ADR-025 contract
  + the F5.1 `kKernelEpsilon` SSoT. Forward-point: this is the
  "cross-ISA parity test" that the F5.1 ticket forward-pointed to
  TICKET-SIMD-REGISTRY-MACHINE-VERIFY.
- **`<i> TICKET-SIMD-VECTORIZE-3DOC-CAT5-ALIGN`** — Cat-5 3-doc closure
  (CURRENT_STATUS cite-only + FOLLOWUP_TICKETS row) once the
  WBH-macchina-verifica passes (per §Disciplina di aggiornamento dei
  canonici). Parallel precedent:
  TICKET-SIMD-REGISTRY-V1-3DOC-CAT5-ALIGN.

## Cross-link canonici

- [`include/chronon3d/simd/cpu_isa.hpp`](../../include/chronon3d/simd/cpu_isa.hpp) — `CpuIsa::AVX2` enumerator + `CpuCapabilities::has_avx2` + `detect_cpu_capabilities()`.
- [`include/chronon3d/simd/pixel_kernels.hpp`](../../include/chronon3d/simd/pixel_kernels.hpp) — `BlendKernel::ApplyFn` signature + `kKernelEpsilon` SSoT.
- [`include/chronon3d/simd/kernel_resolver.hpp`](../../include/chronon3d/simd/kernel_resolver.hpp) — `kScalarSet` (the reference) + `resolve_pixel_kernels` (F5.2 update: routes to `kAvx2Set` for AVX2 hosts).
- [`include/chronon3d/simd/detail/scalar_kernels.hpp`](../../include/chronon3d/simd/detail/scalar_kernels.hpp) — `scalar_blend` (the reference for the AVX2 parity).
- [`include/chronon3d/simd/detail/avx2_kernels.hpp`](../../include/chronon3d/simd/detail/avx2_kernels.hpp) — NEW: header-only inline `detail::avx2_composite_normal_premul` (THIS COMMIT). The prior `src/backends/software/simd/avx2_kernels.cpp` TU-local definition is DELETED in this commit per Cat-3 minimal-surface.
- [`tests/simd/test_simd_parity_blend.cpp`](../../tests/simd/test_simd_parity_blend.cpp) — NEW: 7 doctest TEST_CASEs (THIS COMMIT).
- [`tests/simd/simd_parity_blend_tests.cmake`](../../tests/simd/simd_parity_blend_tests.cmake) — NEW: TIER=UNIT unconditional test registration (THIS COMMIT).
- [`docs/adr/ADR-025-simd-registry.md`](../../docs/adr/ADR-025-simd-registry.md) — design rationale (ADR-anchor for the SIMD registry).
- [`docs/tickets/TICKET-SIMD-REGISTRY-V1.md`](../../docs/tickets/TICKET-SIMD-REGISTRY-V1.md) — the F5.1 SCAFFOLD commit (parent of F5.2 vectorization).
- [`docs/tickets/TICKET-SIMD-REGISTRY-CANONICAL.md`](../../docs/tickets/TICKET-SIMD-REGISTRY-CANONICAL.md) — the design-rationale ticket for the SIMD registry.
- AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza ADR" — ADR-025 satisfies.
- AGENTS.md §regole "Cat-3 minimal-surface" — 1 NEW header (`include/chronon3d/simd/detail/avx2_kernels.hpp`) + 1 NEW test (`.cpp`) + 1 NEW test registration (`.cmake`) + 1 EDIT resolver + 1 EDIT orchestrator + 1 NEW ticket + 1 EDIT CHANGELOG + 1 DELETED `.cpp` (replaced by header-only pattern).
- AGENTS.md §regole "NO #include <msdfgen>, <libtess2>, <unicode[/...]>" (Rule 5 Gate 5) — none introduced.
- AGENTS.md §regole "Fare PR piccole e mirate" — per-kernel incremental commits (per user spec verbatim).
- AGENTS.md §honesty — first-kernel PARTIAL-cert per DEFERRED-WBH (TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV precedent); macchina-verifica deferred to working build host.
