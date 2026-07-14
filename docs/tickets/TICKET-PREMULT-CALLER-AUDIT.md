# TICKET-PREMULT-CALLER-AUDIT — Production-side Premult invariant caller audit in src/backends/software/*

## Stato

P2 OPEN (2026-07-14, opened from TICKET-SIMD-PRECISION-DRIFT §Forward-points per code-reviewer-minimax-m3 MINOR #3)

## Priorità

P2 (production-side invariant consistency; not currently P0 blocking because no known silent rot surfacing, but a forward-prophylactic sweep to prevent identical rot pattern from re-emerging in production callers)

## Problema

The SIMD test fix in commit `b16ad302` is test-side only. It fixed the test fixture under the Premult invariant semantic, but DID NOT verify that production callers of `chronon3d::simd::detail::scalar_blend` (and its future SIMD counterparts via `cs::resolve_pixel_kernels(caps)` ABI) actually pass pre-premultiplied `src.rgb` matching the contract documented at `include/chronon3d/simd/detail/scalar_kernels.hpp::scalar_blend` doc-comment.

If a production caller in `src/backends/software/composite_*` or any other production callers passes RAW RGB (non-premultiplied) src.rgb before invoking `scalar_blend`, the formula silently produces wrong output (degenerate to `dst = src_raw + dst_raw * (1 - sa)` — close-to-but-not-Porter-Duff-portable). Specifically:
- For sa = 0: function darkens the dst by adding raw RGB (NOT identity, NOT Porter-Duff).
- For sa ∈ (0, 1): function applies non-Premult arithmetic on RGB which deviates from the canonical SRC_OVER computational graph.
- For sa = 1: function correctly replaces dst with src (since raw * 1 = Premult when sa=1, no observable rot at this alpha extreme).

This is a CLASS-2 rot pattern (silent latency/tautology: behavior appears correct on boundary alpha values + looks plausible inside the rgb space) — different from CLASS-1 build rot (parse-fail cascade) that the prior chore `988e6c26` addressed.

## Evidenza

- `include/chronon3d/simd/detail/scalar_kernels.hpp::scalar_blend` — the function + its doc-comment PREMULT contract.
- `include/chronon3d/simd/pixel_kernels.hpp::kKernelEpsilon` — the ABI epsilon SSoT (1 ULP float32 per ADR-025 §Decision 3).
- `include/chronon3d/simd/kernel_resolver.hpp` — the dispatch entry point that callers use to obtain correct per-ISA kernel set.
- `code-reviewer-minimax-m3` verdict on commit `b16ad302` — MINOR #3: "Production-side caller audit missing. If `src/backends/software/composite_*` callers pass raw RGB without pre-premultiplied src, the formula silently produces wrong output."
- Candidate caller paths (forward-point to grep):
  - `src/backends/software/composite_*` — composite_normal_premul, composite_screen_premul, etc. naming pattern.
  - `src/scene/builders/glue_apply_*` — TextFrame + GlyphSelector + Layer apply pipelines.
  - `src/render_graph/nodes/composite_normal_*` — render-graph node implementations.
  - `src/render_graph/nodes/effect_stack_node.*` — composite pipeline nodes.

## Impatto

- ABI surface: unchanged (audit-only churn; no production signature mutation).
- Build: no impact expected.
- Observed user-visible rot: NONE in production yet (no reported issue from pipelines). But a forward-prophylactic sweep is warranted given the AGENTS.md "non segnare verde una suite che restituisce failure" + "non cambiare un gate per nascondere un errore" rules — silent rot is exactly the failure mode these rules exist to prevent.
- Future-gate-portability: the simd_parity_blend_tests SIMD tests verify the formula correctness but cannot verify the CALLER's premultiplication responsibility. If the codebase's invocations are wrong, the tests still pass via internal-self-consistency.

## Confine

IN SCOPE:
- ONLY `src/backends/software/` + `src/render_graph/nodes/` + `src/scene/builders/` — the 3 candidate caller paths identified above.
- Static analysis via `rg -l scalar_blend src/` + `rg -l composite_normal_premul src/` (whichever surfaces the broader caller pattern).
- Per-caller audit: does the caller pre-premult src.rgb BEFORE invoking the kernel? If not, the caller requires a fix-up commit (or explicit "Premult invariant is enforced at the kernel-API boundary" assumption documentation if the API is already canonical Premult-aware).

OUT OF SCOPE:
- The kernel implementation itself (`include/chronon3d/simd/detail/scalar_kernels.hpp`) — already correct per its doc-comment Premult contract.
- Test-side audit — covered by SIBLING ticket TICKET-PREMULT-TEST-SWEEP.
- New `src/backends/software/` files (no API surface expansion; only audit + fix-up existing callers).

## Soluzione accettabile

For each production caller (`rg -l scalar_blend src/backends/ + src/render_graph/nodes/`), perform a 3-step audit:
1. **Identify the caller** via `rg scalar_blend <file>` and read the surrounding context.
2. **Verify premult semantic**: trace the src.rgb through any upstream transforms — color convert, layer blend, glyph pre-multiply — to determine if src.rgb reaches the kernel pre-premultiplied. If pre-premultiplied somewhere upstream (likely), document the pre-condition in a `// PREMULT CONTRACT:` comment block adjacent to the call site. If NOT pre-premultiplied anywhere upstream (a real rot-class), fix it by inserting a per-pixel premult loop before the call OR mark it as a forward-pointed production rot.
3. **Add a regression test** for each fix-up'd caller (optional but recommended): test that the upstream path correctly produces pre-premultiplied src.rgb before reaching the kernel. Place the test in the corresponding test directory per existing test_hygiene.

For callers verified ALREADY pre-premultiplied (likely many): add ONLY the contract documentation block, no code change. This is a doc-only cat-3 minimal-surface commit.

For callers needing fix-up: a separate cat-3 chore per fix-up; forward-point here + open sibling tickets.

## Criteri di accettazione

- [ ] All production callers in `src/backends/software/` + `src/render_graph/nodes/` audited via `rg -l scalar_blend` + manual verification.
- [ ] Every audited caller has EITHER:
  - A `// PREMULT CONTRACT:` doc-block adjacent to the call site (if already pre-premultiplied upstream), OR
  - An INLINE pre-premult loop before the kernel call (if not yet pre-premultiplied upstream).
- [ ] No caller passes RAW RGB to the kernel without either premultiplication or an explicit "this is intentionally non-Premult because X" comment.
- [ ] All callers' existing test coverage (if any) passes post-audit disclosure OR fix-up.
- [ ] macchina-verifica: ctest -R <backends|render_graph|scene_builders> all pass; 0 ABI epsilon regressions; 0 ABI surface changes.
- [ ] Cross-references: TICKET-SIMD-PRECISION-DRIFT (parent ticket) + TICKET-PREMULT-TEST-SWEEP (sibling forward-point — test-side canonicalization).

## Forward-points

- **PREMULT-CONTRACT-DOCBLOCK** (P3 OPEN): for each audited caller verified-already-premultiplied, ensure `// PREMULT CONTRACT:` doc-blocks are present. This is a uniform documentation pass.
- **PREMULT-FAIL-LOUD-MISMATCH** (P2 OPEN): add a debug-build assertion in `include/chronon3d/simd/detail/scalar_kernels.hpp::scalar_blend` that detects "src.rgb_premul > src.a" violations (a clear non-Premult invariant violation) and emits a WARN log so production-side misuse is loud rather than silent. (Cat-3 add: 1 inline assertion + 1 WARN log; no SDK surface mutation beyond a debug-only side-effect.)
- **PREMULT-UPSTREAM-INTERFACE** (P3 OPEN): investigate whether ALL `src/backends/software/composite_*` callers SHOULD accept an explicit `bool assume_premultiplied = true` parameter so the contract is enforced at the API boundary rather than the caller having to remember. ADR-driven.

## Cronaca

- 2026-07-14: forward-point surfaced from `b16ad302` code-reviewer-minimax-m3 verdict MINOR #3. TICKET-SIMD-PRECISION-DRIFT §Forward-points appended after that commit. Ticket OPENED as standalone cat-5 canonical ticket per AGENTS.md §Docs canonical update discipline + Cat-3 anti-dup patterns.
- 2026-07-14: scope verified — 3 candidate caller paths identified (composite_*, glue_apply_*, render_graph_nodes/composite*). The com grep will surface exact caller inventory.

## Cross-references (SHA cites inline per AGENTS.md §SHA cite pattern)

- Commit `b16ad302` (fix(simd): Premult alpha=0 fixture + SweepN + macchina-verifica PASS) — parent commit that surfaced this audit as a forward-point.
- Commit `988e6c26` (docs(followup): macchina-verifica PARTIAL + TICKET-SIMD-PRECISION-DRIFT) — the original 5-file chore that opened TICKET-SIMD-PRECISION-DRIFT.
- TICKET-SIMD-PRECISION-DRIFT (P2 OPEN; this ticket's parent via §Forward-points).
- TICKET-PREMULT-TEST-SWEEP (P2 OPEN, sibling ticket; this ticket's parallel forward-point — test-side historical sweep).
- ADR-025 (SIMD registry canonical SSoT) — the architecture frame for the contract.
- AGENTS.md §regole "Mantenere baseline verde" — the §honesty clause that surfaced the §Forward-points discipline.
- AGENTS.md §regole "Non segnare verde una suite che restituisce failure" — the silent-rot pattern this ticket exists to prevent.
- AGENTS.md "SHA cite pattern (inline-only rule)" — the SHA cite discipline applied in this ticket's §Cronaca.
- `include/chronon3d/simd/detail/scalar_kernels.hpp::scalar_blend` — the function + doc-comment Premult contract.
- `include/chronon3d/simd/pixel_kernels.hpp::kKernelEpsilon` — ABI epsilon SSoT.
- `tools/check_architecture_boundaries.sh` (Gate 5 Check 11) — the deny-everywhere pattern for `#include <msdfgen>/<libtess2>/<unicode[/...]>` (different concern but mentioned for context on architectural boundaries).
