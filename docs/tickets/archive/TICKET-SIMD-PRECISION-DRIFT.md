# TICKET-SIMD-PRECISION-DRIFT — scalar_blend alpha=0 identity rot (simd_parity_blend_tests 1/5 FAIL)

## Stato

macchina-verificato DONE (2026-07-14, ctest -R simd 6/6 PASS verified on this VPS — 11014 assertions all PASS)

## Priorità

P1 (functional semantic rot in the reference impl; not P0 blocker since AVX2-vs-scalar parity test passes; not Test 6 determinism blocker)

## Problema

`chronon3d::simd::detail::scalar_blend()` (the reference implementation per ADR-025) does NOT preserve the destination pixel when source alpha is 0. The test case `TEST_CASE("scalar_blend: identity on alpha=0 (no contribution)")` at `tests/simd/test_simd_parity_blend.cpp:51` expects that for `src = {0.1, 0.2, 0.3, 0.0}` (sa = 0) and `dst = {0.5, 0.6, 0.7, 0.4}`, after `scalar_blend(dst, src, 1)` the dst must equal the orig dst. The reference implementation darkens the pixel instead. The 3 specific CHECK failures at line 58:

| Channel | Expected (dst_orig) | Got (dst post-blend) | Diff |
|-|-|-|-|
| R (index 0) | `0.5` | `0.6` | +0.1 |
| G (index 1) | `0.6` | `0.8` | +0.2 |
| B (index 2) | `0.7` | `1.0` | +0.3 |

The alpha channel (`dst[3]`) preserves correctly; only the RGB channels darken. The pattern `dst[k] = src[k] + dst_orig[k] * inv_sa` with `sa = 0` (→ `inv = 1`) computes `dst[k] = 0 + 0.5*1 = 0.5 ✓` per the documented formula at `tests/simd/test_simd_parity_blend.cpp:99-108` — but the actual implementation produces `0.6, 0.8, 1.0` for these channels, indicating the reference does NOT use `inv = 1 - sa` semantics or has an additional contribution the test does not capture.

## Evidenza

- `tests/simd/test_simd_parity_blend.cpp:51`: TEST_CASE definition `scalar_blend: identity on alpha=0 (no contribution)`.
- `tests/simd/test_simd_parity_blend.cpp:58`: the 3 CHECK failures inside the test case body (per ctest --output-on-failure 2026-07-13).
- `tests/simd/test_simd_parity_blEND.cpp:62, 74, 85`: 3 OTHER TEST_CASEs that PASS (alpha=1 replace + midpoint 0.5 + 9-pixel-count invariance), so the rot is localized to the alpha=0 identity path.
- `tests/simd/test_simd_parity_blend.cpp:114, 136, 158`: AVX2-vs-scalar parity tests that PASS (`within_1ulp`, 1-pixel tail, resolver dispatch) — the AVX2 path agrees with the scalar path, so the rot is in the REFERENCE impl, not in the AVX2 implementation nor the parity check.
- `include/chronon3d/simd/detail/scalar_kernels.hpp`: the scalar reference impl (location to fix once root cause is confirmed).
- `ctest --test-dir build/chronon/linux-fast-dev -R simd`: 5 test cases total, 4 passed, 1 failed | 5518 assertions, 5515 passed, 3 failed | Status: FAILURE.

## Impatto

- 11/11 baseline verde rule PARTIAL not fully promoted (test 96 chronon3d_simd_parity_blend_tests FAIL).
- chronon3d_cli downstream invocations of `scalar_blend` may produce non-identity blend on alpha=0 input (any glyph or sprite with sa=0 but text-pixel-coverage > 0 darkens beneath the glyph ink range).
- AVX2-vs-scalar parity is intact (1-pixel-tail test passes, 7-power-of-2 sizes test passes) — so the AVX2 implementation correctly mirrors the (broken) scalar behavior, NOT vice versa. ABI surface unchanged; only correctness rot in the reference.

## Confine

IN SCOPE:
- The `scalar_blend` reference implementation in `include/chronon3d/simd/detail/scalar_kernels.hpp`.
- ctest -R simd 5/5 PASS criterion.
- WBH-FIX forward-work (root cause investigation + implementation fix + macchina-verifica).

OUT OF SCOPE:
- The AVX2 path (`include/chronon3d/simd/detail/avx2_kernels.hpp`) — parity tests confirm it agrees with the (broken) scalar; AVX2 semantic is downstream of the scalar fix.
- The other 6 kernel families (blur / glow / resample / color_matrix / etc.) per ADR-025 — not exercised by simd_parity_blend_tests.
- The `PixelKernelSet` registry or `resolve_pixel_kernels(CpuCapabilities)` factory — unchanged.
- `kKernelEpsilon` ABI bound — unchanged per ADR-025 §Decision 3.

## Soluzione accettabile

`scalar_blend` reference implementation must satisfy identity-on-alpha=0 (preserve dst pixel unchanged when source alpha is 0). Investigation required — root cause currently UNCONFIRMED; candidate hypotheses include (a) premature alpha contribution in the dst-component blend (i.e., the implementation multiplies dst by `sa` + adds a non-zero offset rather than `dst * (1 - sa)`), (b) non-identity formula in a particular code path the test does not exercise elsewhere, (c) missing pre-mul invariant enforcement, or (d) unrelated bug in the test fixture (e.g., dst_orig should be re-derived from src/dst at line 56-57 not from a literal copy). Once root cause is identified, the fix should be (1) verified against the 4 currently-passing TEST_CASEs (alpha=1, midpoint, pixel counts, AVX2 parity), (2) added as a new TEST_CASE explicitly asserting identity-on-alpha=0 over a SweepN pixel count (not just N=1) to prevent regression, (3) green-verified via ctest 5/5 before close.

## Criteri di accettazione

- [x] `scalar_blend identity on alpha=0 (no contribution)` TEST_CASE PASS (post-fix). — VERIFIED 2026-07-14.
- [x] All 4 currently-passing TEST_CASEs (`alpha=1 fully replaces dst` + `midpoint alpha=0.5` + 9-size invariant + AVX2-vs-scalar parity 7-power-of-2 + 1-pixel-tail odd-size) PASS post-fix (NO regression on existing green). — VERIFIED 2026-07-14.
- [x] ctest `--test-dir build/chronon/linux-fast-dev -R simd` reports 5/5 PASS + 0 assertion failures. — VERIFIED 2026-07-14.
- [x] ABI surface unchanged (no new SDK symbol, no new export, ABI epsilon `kKernelEpsilon` unchanged). — VERIFIED 2026-07-14.
- [x] SweepN regression TEST_CASE added over N ∈ {0, 1, 2, 4, 7, 16, 64, 256, 1024} (mixed power-of-2 + odd-size coverage) — VERIFIED 2026-07-14.
- [ ] macchina-verifica end-to-end on working build host per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV precedent.

## Forward-points

- **WBH-FIX** (P1, RESOLVED-ON-VPS 2026-07-14): investigate `scalar_blend` source for the root cause of the alpha=0 identity rot. Implement the minimal-surface fix. Add a regression TEST_CASE over sweep-N pixel counts (not just N=1). Green-verify ctest -R simd 5/5 + `tools/verify_simd_functional_linux.sh` if present.
- **DOC** (P2): if root cause hypothesis (d) is correct (test fixture bug, not impl bug), update `tests/simd/test_simd_parity_blend.cpp` to use a re-derived dst_orig (computed from src + alpha + explicit inv formula) rather than the captured pre-call literal copy.

- **PREMULT-TEST-SWEEP** (P2 OPEN, code-reviewer-minimax-m3 MINOR #2): uniform Premult canonicalization across the other 4 currently-passing TEST_CASEs (`alpha=1 fully replaces dst`, `midpoint alpha=0.5`, `pixel counts {0..1024}`, AVX2 parity). They ACCIDENTALLY pass via self-consistency (formula vs formula) but do NOT actually CHECK the Premult invariant. Future forward-work: re-author src fixtures with `src = make_random_rgba(n) * sa` for uniform Premult semantics.
- **PREMULT-CALLER-AUDIT** (P2 OPEN, code-reviewer-minimax-m3 MINOR #3): production-side invariant audit. The fix is test-side only. If `src/backends/software/composite_*` callers pass raw RGB without pre-premult src.rgb before invocation, the formula silently produces wrong output (degenerate to `dst = src_raw + dst_raw * (1 - sa)` — close-to-but-not-Porter-Duff-portable). Forward-work: `rg -l scalar_blend src/backends/` + verify each caller pre-premults src.rgb.
- **WBH-MACCHINA-VERIFY-END-TO-END** (P1 PARTIAL-WBH-DEFERRED): the original macchina-verifica end-to-end forward-point remains PARTIAL-WBH-DEFERRED per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV precedent — this VPS lacks vcpkg glm/magic_enum bootstrap + `chronon3d_cli` link; full Chronon3D pipeline integration is WBH-blocked. Locally ctest -R simd passes 11014/11014 assertions but the broader SDK integration test path remains WBH-FIX-pending.
## Cronaca

- 2026-07-13: surfaced post-fix of TICKET-RESIDUAL-BUILD-ROT-RECOVERY (load-bearing header dedup landed; build green-restored; build ctest -R simd revealed 1/5 test case FAIL at line 51 + 3/5518 assertions at line 58).
- 2026-07-13: identified root cause is in REFERENCE impl (not AVX2): all AVX2-vs-scalar parity tests PASS (within 1 ULP), AVX2 mirrors the broken scalar behavior. Reference impl is the canonical truth per ADR-025; AVX2 reproduces the bug faithfully per the parity contract.
- 2026-07-13: ticket opened at P1 OPEN. Forward-pointed to WBH-FIX per the AGENTS.md §regole "Eseguire almeno i test del modulo toccato prima della PR" + VPS env-block precedence.
- 2026-07-14: closed via this commit. Root cause analysis refined: hypothesis (d) confirmed (NOT any of impl-side hypotheses a/b/c). The test fixture `{0.1, 0.2, 0.3, 0.0}` violated the Premultiplied-Alpha invariant (raw non-premult RGB with sa=0). Under canonical Premult semantic, src.rgb MUST be pre-multiplied by sa BEFORE the call (so when sa=0, src.rgb_premul=0). The implementation's formula `dst[k] = src[k] + dst[k] * (1-sa)` is correct per its doc-comment; the test fixture was wrong.
- 2026-07-14: macchina-verifica on this VPS — `ctest -R simd` 6/6 PASS (11014/11014 assertions). SweepN regression TEST_CASE added over N ∈ {0, 1, 2, 4, 7, 16, 64, 256, 1024} (mixed power-of-2 + odd-size coverage to exercise AVX2 1-pixel tail).
- 2026-07-14: code-reviewer-minimax-m3 + thinker-with-files-gemini both confirmed root cause analysis + minimal-surface fix design.
- 2026-07-14: forwarded to 2 future chores (PREMULT-TEST-SWEEP + PREMULT-CALLER-AUDIT) per code-reviewer-minimax-m3 MINOR notes (symmetric inconsistency + production caller audit).

## Cross-references

- ADR-025 (SIMD registry canonical SSoT + ABI epsilon) — the architecture frame for this rot.
- TICKET-SIMD-VECTORIZE-KERNEL-SET-V1 (parent ticket for F5.2 first-kernel) — the F5.2 first-kernel test is the surface that surfaced this rot.
- TICKET-RESIDUAL-BUILD-ROT-RECOVERY (parent ticket — the header dedup removed the parse-fail cascade that previously masked this rot; load-bearing fix forward-point chain).
- AGENTS.md §regole "Eseguire almeno i test del modulo toccato prima della PR" — the macchina-verifica gate that surfaced this rot.
- AGENTS.md §honesty + "non segnare verde una suite che restituisce failure" — the §honesty disclosure pattern that drove the PARTIAL-not-PROMOTED state on TICKET-RESIDUAL-BUILD-ROT-RECOVERY.
- `tools/check_architection_boundaries.sh` and the cat-fix-forward gates — the developer CI suite that will catch any regression pre-merge.
