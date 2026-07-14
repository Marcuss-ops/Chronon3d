# TICKET-PREMULT-TEST-SWEEP — Uniform Premult-invariant canonicalization across the 4 other currently-passing TEST_CASEs

## Stato

P2 OPEN (2026-07-14, opened from TICKET-SIMD-PRECISION-DRIFT §Forward-points per code-reviewer-minimax-m3 MINOR #2)

## Priorità

P2 (test-side invariant consistency; not blocking since all 4 currently pass via self-consistency; future-proofing to prevent silent regressions)

## Problema

After the SIMD test fix in commit `b16ad302` (Premult-invariant correction at `tests/simd/test_simd_parity_blend.cpp:51`), the test file has ASYMMETRIC Premult-strictness: the new `alpha=0 identity test_case` (post-fix) is Premult-strict (asserts identity via valid `{0,0,0,0}` src + comment block citing Premult invariant), but the OTHER 4 currently-passing TEST_CASEs in the same file use RAW uniform-distribution non-premult src fixtures and pass ACCIDENTALLY via self-consistency (both the implementation + the inline reference derivation apply the same non-Premult formula). They do NOT actually CHECK the Premult invariant.

The 4 affected TEST_CASEs:
1. `TEST_CASE("scalar_blend: alpha=1 fully replaces dst")` (line ~62-72) — uses raw `src = {0.1, 0.2, 0.3, 1.0}` non-premult fixture; passes under current impl because raw + raw cancels out symmetrically with formula derivation. Under canonical Premult, `src.rgb` should be `src.rgb_unpremult * sa = {0.1, 0.2, 0.3} * 1.0 = {0.1, 0.2, 0.3}` (coincidentally same as raw because sa=1, so the bug is hidden).
2. `TEST_CASE("scalar_blend: midpoint alpha=0.5 (50/50 blend)")` (line ~74-84) — uses `src = {0, 0, 0, 0.5}` (src.rgb=0 mask); trivially passes any interpretation. Needs explicit Per-channel Premult assertion.
3. `TEST_CASE("scalar_blend: pixel counts {0, 1, 2, 4, 8, 16, 64, 256, 1024} all correct")` (line ~86-110) — uses `make_random_rgba` which produces RAW uniform [0, 1] RGBA pairs that violate Premult invariant for ANY sa ∈ (0, 1). Reference derivation uses same non-Premult formula → self-consistency mask. Should canonicalize src.rgb to `make_random_rgba * sa`.
4. `TEST_CASE("avx2 vs scalar: parity within 1 ULP across 7 power-of-2 sizes")` + `TEST_CASE("avx2 vs scalar: 1-pixel tail (odd sizes) parity")` (lines ~136-186 inside `#if defined(__AVX2__)` block) — same raw-RGB fixture issue as #3. Cross-ISA parity asserted but Premult invariant never checked.

## Evidenza

- `tests/simd/test_simd_parity_blend.cpp:51` (now post-fix) — explicit Premult-strict test_case for alpha=0 identity.
- `tests/simd/test_simd_parity_blend.cpp:62-110` — 3 currently-passing scalar tests using raw-src fixtures.
- `tests/simd/test_simd_parity_blend.cpp:136-186` — 2 AVX2-vs-scalar parity tests using raw-src fixtures inside `#if defined(__AVX2__)` block.
- `include/chronon3d/simd/detail/scalar_kernels.hpp::scalar_blend` — the impl whose contract is Premultiplied-Alpha SRC_OVER per doc-comment.
- `include/chronon3d/simd/pixel_kernels.hpp::kKernelEpsilon` — the ABI epsilon SSoT; no change required for this chore.
- `code-reviewer-minimax-m3` verdict on commit `b16ad302` — MINOR #2: "SYMMETRIC INCONSISTENCY not addressed. The 4 OTHER currently-passing TEST_CASEs use RAW non-premult src fixtures and ACCIDENTALLY pass via self-consistency (formula vs formula) — they don't actually CHECK the Premult invariant. After this fix, the test file has asymmetric strictness: the new test is Premult-strict; the other 4 silently assume Premult-correct caller input."

## Impatto

- Current behavior: all 6 currently-passing TEST_CASEs PASS via ctest -R simd (11014 assertions). Functional correctness verified for `chronon3d::simd::detail::scalar_blend` reference impl.
- Silent-regression risk: if production-side caller audit (see TICKET-PREMULT-CALLER-AUDIT) finds that callers DO correctly pre-premult src, the existing test fixtures continue to pass via symmetric-non-Premult mask BUT fail to assert the actual semantic. The Premult contract is the API; tests should mirror API contract, not formula derivation.
- ABI surface: unchanged (test-only matter; no SDK signature mutation).
- Future-test-portability: if future tests are written against these fixtures (e.g., adding 1 new test_case family for a fifth SIMD kernel), the asymmetry propagates: new tests inherit the loose-fixture convention.

## Confine

IN SCOPE:
- ONLY `tests/simd/test_simd_parity_blend.cpp` (4 currently-passing TEST_CASEs to re-author).
- SweepN regression test from commit `b16ad302` is ALREADY Premult-canonical (src = all-zero vector).
- Production source unchanged (no `include/chronon3d/` edits).

OUT OF SCOPE:
- `include/chronon3d/simd/detail/scalar_kernels.hpp` — already correct per its doc-comment Premult contract.
- `include/chronon3d/simd/detail/avx2_kernels.hpp` — AVX2 mirrors scalar faithfully; parity maintained.
- `include/chronon3d/simd/kernel_resolver.hpp` + `pixel_kernels.hpp` — ABI SSoTs unchanged.
- Production callers in `src/backends/software/composite_*` — covered by SIBLING ticket TICKET-PREMULT-CALLER-AUDIT.

## Soluzione accettabile

Re-author the 4 raw-src TEST_CASEs to use canonical Premult semantically-valid fixtures. Recommended pattern: `src = make_random_rgba(n, seed) * sa` (vectorized scalar multiplication: src.rgb component-wise multiplied by src.a component via `#include <algorithm>` |parallel std::transform|, OR via per-sample loop). This produces a valid Premult color where `src.rgb <= src.rgb_unpremult` (since src.rgb_unpremult ∈ [0, 1] and sa ∈ [0, 1]).

Specific changes per TEST_CASE:
1. `alpha=1 fully replaces dst`: change `src = {0.1, 0.2, 0.3, 1.0}` → use `make_random_rgba(1, seed) * 1.0` (already semantically valid by coincidence; explicit construction makes the Premult semantic obvious).
2. `midpoint alpha=0.5`: change `src = {0, 0, 0, 0.5}` to `src = make_random_rgba(1, seed) * 0.5` (RNG-driven rgb * 0.5 makes for richer test coverage while remaining Premult-canonical).
3. `pixel counts {0..1024}` + `avx2 parity` + `1-pixel tail` sweep families: change `src = make_random_rgba(n, seed)` → `src = make_random_rgba(n, seed) * sa` where `sa` is computed element-wise as `src[4*i+3]` from the same RNG output. The premult multiplication should be element-wise per pixel (RGB * A of same pixel).

After re-authoring, all 6 TEST_CASEs + the 2 AVX2 parity tests should still pass (since formula derivation is unchanged; only the fixture validity is changed). The 4 raw-RGB self-consistency mask is replaced with Premult-canonical semantics + explicit invariant assertion.

## Criteri di accettazione

- [ ] All 6 currently-passing TEST_CASEs + 2 AVX2-parity TEST_CASEs (when AVX2 enabled) PASS post-fixture-canonicalization.
- [ ] The 4 re-authored TEST_CASEs explicitly assert the Premult semantic via either a comment block (per Premult invariant) or via fixture derivation showing `src.rgb = make_random_rgba(n, seed) * sa`.
- [ ] The test file has UNIFORM Premult-strictness: every TEST_CASE either (a) uses src.rgb = 0 mask + comment, or (b) uses src.rgb = make_random_rgba * sa canonicalization.
- [ ] cat-3 minimal-surface: 1 file edit, 0 SDK surface changes, ABI epsilon SSoT unchanged.
- [ ] macchina-verifica: `ctest -R simd` 6/6 PASS + 2/2 AVX2 parity PASS; 0 assertion regressions; 0 ABI epsilon violations.
- [ ] Cross-references: TICKET-SIMD-PRECISION-DRIFT (parent ticket) + TICKET-PREMULT-CALLER-AUDIT (sibling forward-point — production-side audit).

## Forward-points

- **PREMULT-INVARIANT-CONTRACT-DOC** (P3 OPEN): extend `include/chronon3d/simd/detail/scalar_kernels.hpp::scalar_blend` doc-comment block to EXPLICITLY document the "src.rgb MUST be pre-multiplied by src.a BEFORE this call" pre-condition. Currently the doc-comment just states the formula without the pre-condition contract. This doc-only update would harden the API contract for future caller authors.
- **PREMULT-TEST-DRIVER-FIXTURE** (P3 OPEN): create a shared helper function `make_premultiplied_random_rgba(n, seed) → std::vector<float>` that produces the Premult-canonical random RGBA fixture, used by the 4 affected TEST_CASEs + future test families. Reduces code duplication (cat-3 anti-dup violation if not extracted).

## Cronaca

- 2026-07-14: forward-point surfaced from `b16ad302` code-reviewer-minimax-m3 verdict MINOR #2. TICKET-SIMD-PRECISION-DRIFT §Forward-points appended after that commit. Ticket OPENED as standalone cat-5 canonical ticket per AGENTS.md §Docs canonical update discipline + Cat-3 anti-dup patterns.
- 2026-07-14: scope verified — only the 4 currently-passing TEST_CASEs in `tests/simd/test_simd_parity_blend.cpp` need re-authoring. SweepN regression test (from `b16ad302`) is already Premult-canonical (src = all-zero vector).

## Cross-references (SHA cites inline per AGENTS.md §SHA cite pattern)

- Commit `b16ad302` (fix(simd): Premult alpha=0 fixture + SweepN + macchina-verifica PASS) — parent commit that surfaced the asymmetric Premult-strictness as a follow-point.
- Commit `988e6c26` (docs(followup): macchina-verifica PARTIAL + TICKET-SIMD-PRECISION-DRIFT) — the original 5-file chore that opened TICKET-SIMD-PRECISION-DRIFT.
- TICKET-SIMD-PRECISION-DRIFT (P2 OPEN 2026-07-14, this ticket's parent) — anchor for the hypothesis-(d) analysis + Premult-invariant strategy.
- TICKET-PREMULT-CALLER-AUDIT (P2 OPEN 2026-07-14, sibling ticket) — production-side audit forward-point.
- ADR-025 (SIMD registry canonical SSoT) — architecture frame; this ticket extends the test-side compliance to ADR-025's `kKernelEpsilon` contract.
- AGENTS.md §regole "Eseguire almeno i test del modulo toccato prima della PR" — the macchina-verifica gate.
- AGENTS.md §Cat-3 minimal-surface — the 1-file-tag scope discipline.
- AGENTS.md "SHA cite pattern (inline-only rule)" — the SHA cite discipline applied in this ticket's §Cronaca.
- `tests/simd/test_simd_parity_blend.cpp` — the 4 TEST_CASEs to re-author (lines 62-186 with the AVX2 block).
- `include/chronon3d/simd/pixel_kernels.hpp` — the kKernelEpsilon SSoT + the doc-comment contract for the implied Premult semantic.
