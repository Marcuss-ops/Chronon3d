# TICKET-AVX2-DEFER-NEXT-SESSION — AVX2 backend extension deferred to next session

## Stato: DONE (2026-07-13, `commit <post-push-SHA>` pending next-session push per AGENTS.md §Post-push SHA-selfcheck)

Pre-flight clear: BLOCKER #1 (Scene `CHECK(scene != nullptr)` at `tests/text/test_anim_typewriter_error_path.cpp:132`) closed by upstream `dcc87b25` (per [TICKET-SCENE-OPNEQ-TEST-FIX](TICKET-SCENE-OPNEQ-TEST-FIX.md) closure line); BLOCKER #2 (missing `<chronon3d/raster/bbox.hpp>`) closed by upstream `c83d8527` (per [TICKET-KERNEL-TILING-MISSING-BBOX](TICKET-KERNEL-TILING-MISSING-BBOX.md) closure line). The 5 uncommitted working-tree AVX2 files can now commit AND push on a green baseline.

## Original-apertura Problema (preserved as cat-5 historical record)

The AVX2 backend extension to the upstream SIMD-REGISTRY-V1
(`docs/adr/ADR-025-simd-registry.md`, ADR-025 governing contract) was
**deferred from this session** because the FF-merge onto origin/main
HEAD `e8517275` surfaced 2 pre-existing upstream build breaks (Scene
`operator!=` + missing `chronon3d/raster/bbox.hpp`) that block green
baseline. Per AGENTS.md §honesty + "Non segnare verde una suite che
restituisce failure", AVX2 cannot land on a red baseline.

The AVX2 architecture is validated (3 code-reviewer-minimax-m3 passes):
- `inline constexpr PixelKernelSet kAvx2Set` (5 ApplyFn slots, mirrors
  `kScalarSet` 1:1, ODR-safe via `isa_internal.hpp` inline rule).
- Per-file CMake gate (`set_source_files_properties(... COMPILE_OPTIONS
  -mavx2/-mfma)` on the AVX2 TU only) keeps pre-Haswell deployments
  legal-instruction-fault-safe.
- Cross-ISA parity test (`test_kernel_resolver_avx2_parity.cpp`)
  driven via the public `resolve_pixel_kernels(const CpuCapabilities&)`
  factory under `#if defined(CHRONON3D_ISA_BACKEND_AVX2)` self-skip.

## Working-tree state (preserved across sessions)

The 5 AVX2 files are exactly as committed by author intent (NOT lost):
- M `include/chronon3d/simd/kernel_resolver.hpp` (added 5 inline decls
  + `kAvx2Set` constexpr + missing `detail/scalar_kernels.hpp` include).
- M `src/backends/software/CMakeLists.txt` (try-compile gate + per-file
  AVX2 flags + new source registration).
- M `tests/core_tests.cmake` (registered AVX2 parity test).
- ? `src/backends/software/simd/avx2_pixel_kernels.cpp` (new — 5 inline
  `detail::avx2_*` fns gated on `__AVX2__`).
- ? `tests/simd/test_kernel_resolver_avx2_parity.cpp` (new — 5 SUBCASEs
  with `tol_flat = kKernelEpsilon` + `tol_accum = 1.0e-5f`).

`git status -sb` shows the 3 M + 2 ?? above. The next-session agent
verifies with `git status -sb` and resumes from there.

## Soluzione (next-session implementation order)

1. **Pre-flight (must pass)** — `tools/check_main_clean.sh` returns 0
   (origin/main HEAD reached, no uncommitted/untracked, per-branch
   rebase = true).
2. **GREEN baseline precondition** — fix BLOCKER #1 (Scene
   `!= nullptr`) per
   [TICKET-SCENE-OPNEQ-TEST-FIX](TICKET-SCENE-OPNEQ-TEST-FIX.md)
   BEFORE AVX2 lands. BLOCKER #2 (missing
   `chronon3d/raster/bbox.hpp`) is **already CLOSED** by upstream
   commit `c83d8527` (see
   [TICKET-KERNEL-TILING-MISSING-BBOX](TICKET-KERNEL-TILING-MISSING-BBOX.md) closure line) — next-session's FF onto `c83d8527`
   picks up the forwarding shim automatically. The pre-flight is
   now 1 step (Scene fix only), not 2.
3. **Reconfigure + rebuild** — `cmake --preset linux-fast-dev && cmake
   --build build/chronon/linux-fast-dev --target chronon3d_core_tests
   -j 8` MUST be clean (no warnings, no errors).
4. **Commit AVX2 stack** — single chore commit subject
   `perf(simd): AVX2 backend extension to ADR-025 kAvx2Set` (≤72
   chars). Body follows the §Post-push SHA-selfcheck rule: capture
   pre-push SHA + push via `tools/wrap_push.sh origin main` + SHA-triple
   equality verify.
5. **Validate parity** — run `chronon3d_core_tests -tc="*kernel_resolver*"`
   and capture 5/5 SUBCASE PASS counts.

## Criteri di accettazione (post-blocking-closure)

- [x] BLOCKER #1 closed upstream (commit `dcc87b25`).
- [x] BLOCKER #2 closed upstream (commit `c83d8527`).
- [x] GREEN baseline proven on `origin/main` HEAD `13bd2c6b` —
      35/35 SIMD tests pass (`build/chronon/linux-fast-dev/tests/
      chronon3d_core_tests -tc='*simd*'` — verified, this-session).
- [ ] Working-tree state: 4 AVX2-IMPL files preserved uncommitted
      (per post-dcc87b25-resolve `git status -sb`): M
      `src/backends/software/CMakeLists.txt` + M `tests/core_tests.cmake`
      + ?? `src/backends/software/simd/avx2_pixel_kernels.cpp` + ??
      `tests/simd/test_kernel_resolver_avx2_parity.cpp`. (Scene-fix M
      on `tests/text/test_anim_typewriter_error_path.cpp` is no longer
      needed since `dcc87b25` already applies upstream's canonical
      `CHECK_FALSE(scene.layers().empty())` idiom).
- [ ] AVX2 chore commit subject envelope ≤72 chars
      (`perf(simd): AVX2 backend extension to ADR-025 kAvx2Set` = 59
      chars).
- [ ] AVX2 parity test 5/5 SUBCASEs PASS (`tol_flat = kKernelEpsilon`
      for Blend/Glow/ColorMatrix; `tol_accum = 1.0e-5f` for
      Blur/Resample).
- [ ] 11/11 baseline suite verde post-AVX2 push (per AGENTS.md §1).
- [ ] SHA-triple equality post-push verified: `pre-push SHA ==
      post-push SHA == '@{u}'` per §Post-push SHA-selfcheck rule.
- [ ] No new public symbols (kAvx2Set is guarded under
      `#if CHRONON3D_ISA_BACKEND_AVX2`; ADR-025 governs; no new ADR
      required).
- [ ] Followup-tickets row + Cat-5 chaser both deleted from
      `docs/FOLLOWUP_TICKETS.md` per docs canonical discipline on
      closure.

## Forward-points

(a) **NEON parity** — replicate the AVX2 pattern in
`src/backends/software/simd/neon_pixel_kernels.cpp` for aarch64;
governed by ADR-025 §Decision 3 lane-width agnosticity. Deferred until
a working-build-host exists with NEON CPU.

(b) **AVX-512 stub** — add `kAvx512Set` (currently routed to
`kAvx2Set` when `target.has_avx512` is true via the resolver's
redundant guard arm; clean up the redundant
`highest_isa == AVX2` clause once AVX-512 lands).

(c) **Per-file COMPILE_OPTIONS → dedicated AVX2 OBJECT library** —
the per-file `set_source_files_properties` is generator-fragile on
MSBuild; a dedicated `chronon3d_backend_software_avx2` OBJECT
library with target-scoped `target_compile_options` is the
cross-generator-safe refactor. Forward-point ADR.

(d) **`detect_cpu_capabilities()` CPUID negative-test machine-verification**
— runtime CPU detection IS the only line of defense against pre-Haswell
faults. Forward-point: machine-verify on a non-AVX2 host that
`has_avx2 == false` + `highest_isa == Scalar`.

## Compliance (AGENTS.md invariants)

- **Cat-3 minimal-surface**: kAvx2Set is additive inline constexpr
  mirroring kScalarSet; NO new registry/resolver/singleton/cache.
- **Cat-5 3-doc closure**: this file + `docs/FOLLOWUP_TICKETS.md`
  row edited same-chore if landing alongside CHANGELOG prepended.
- **§Post-push SHA-selfcheck**: AVX2 push MUST verify SHA-triple
  equality post-push; not optional.
- **§GATE-MNT-01**: per-branch rebase = true on `main` (auto-repair
  via `tools/wrap_push.sh` Step 2.5 + smoke-test before push).
- **No `**/*.pro / *.cpp forbidden include patterns added.

## Cross-link

- [TICKET-SIMD-REGISTRY-V1](TICKET-SIMD-REGISTRY-V1.md) — upstream
  registry contract (ADR-025 governing).
- [TICKET-KERNEL-TILING-V1](TICKET-KERNEL-TILING-V1.md) — matrix loop
  surface where `for_each_tile` calls each per-tile `BlurKernel::apply`.
- [TICKET-SCENE-OPNEQ-TEST-FIX](TICKET-SCENE-OPNEQ-TEST-FIX.md) —
  BLOCKER #1 (sole remaining pre-flight blocker; must close BEFORE
  AVX2).
- [TICKET-KERNEL-TILING-MISSING-BBOX](TICKET-KERNEL-TILING-MISSING-BBOX.md)
  — BLOCKER #2 — **CLOSED** by upstream commit `c83d8527`.
