# ADR-010 — Boundary gate semantic extension: regression-resistant guards for canonical copy patterns + dead accessor renames

| Field | Value |
|---|---|
| **Status** | Accepted (F3.1 commit pattern, validated by `tools/check_architecture_boundaries_selftest.sh` Case 8/9/10) |
| **Date** | 2026-06-23 |
| **Deciders** | `codex/agent3` author of branch `chore/boundary-gate-semantic-checks`, code-reviewer-minimax-m3 |
| **Tags** | `boundary-gate`, `gates`, `semantic-checks`, `regression-resistance` |
| **Related** | [ADR-008 — `RenderEngine::renderer()` returns pointer](./ADR-008-render-engine-renderer-ptr-return.md), [TICKET-011]((...) — render-runtime ownership), F0.2 PR (`p0/fix-software-renderer-move-ctor` branch on which `m_renderer->settings()` was renamed to `m_renderer->render_settings()` in 3 callsites), F2.4 prior work (`p1/render-pipeline-single-identity` branch on which `runtime::RenderPipeline` was deleted as part of ADR-009 single-identity elimination of the cross-class `m_renderer` sidecar). |

## Context

The boundary gate `tools/check_architecture_boundaries.sh` enforces 11 activity-orthogonal checks: retired header paths (checks 1–3), retired symbol names (4–7), include typo (8), memory-allowlist (9), SoftwareRenderer subprocess (10), deny-everywhere deps (11).  After F0.2 (`m_renderer->settings()` → `m_renderer->render_settings()` rename, still in-flight on `main`), F2.4 (`runtime::RenderPipeline` deletion, on `p1/render-pipeline-single-identity`), and the `chronon3d::runtime::RenderEngine::Impl` construction rewires of TICKET-011 / F0.2, the gate framework needs **semantic regression-resistant checks** — not just activity-orthogonal ones — for three specific patterns:

1. **`m_runtime = nullptr` / `m_registry = nullptr` in (move-)constructor bodies** outside the canonical `other.m_X` source.  Canonical pattern is `m_runtime(other.m_runtime)` (init-list) or `m_runtime = other.m_runtime` (move-assign body).  Bare `m_runtime = nullptr` in a constructor body silently zeroes the pointer (and loses any pre-existing state) — a reintroduction hazard after F2.4 side-fixes in `RenderEngine::Impl` and the planned F2.3 `SoftwareRenderer` ctor rewires (TICKET-042).

2. **`m_renderer->settings()` literal token**.  F0.2 renamed this accessor to `m_renderer->render_settings()` everywhere except 3 sites in `src/runtime/render_engine.cpp:71, 90, 202` that escaped the search-and-replace wave (because they sit inside `RenderEngine::Impl` ctor bodies — see the pre-flight grep result).  The 3 remaining callsites produce undefined behaviour because the canonical accessor no longer exists; the gate must enforce zero matches post-merge.

3. **`RenderPipeline::m_renderer` outside `src/runtime/render_pipeline.cpp`**.  After F2.4, `runtime::RenderPipeline` is deleted (the sidecar `m_renderer` arg was an explicit violation of single-identity ownership per [ADR-009](ADR-009-render-pipeline-single-identity.md) on branch `p1/render-pipeline-single-identity`).  The gate must guard against reintroduction of any `RenderPipeline`-shaped facade that stores `m_renderer` outside its own TU, by failing any future restoration on the file `src/runtime/render_pipeline.cpp` which post-F2.4 doesn't exist (vacuous exclude) and any other TU which would mean the sidecar is back.

The pre-flight on `main@6e0c7413` (this branch's fork point) confirmed:

- **Check 1** finds 4 hits in `src/backends/software/software_renderer.cpp` at lines 152, 153, 172, 173.  Lines 172–173 are canonical `other.m_runtime = nullptr; other.m_registry = nullptr;` inside a move-assign body — filtered by `grep -Ev 'other\.m_(runtime|registry)'`.  Lines 152–153 are bare `m_runtime = nullptr; m_registry = nullptr;` inside the `~SoftwareRenderer()` destructor body — legitimate teardown but would still match the bare pattern; documented ADR-010 exception, gate allows with a comment + a narrowly-scoped `grep -Ev 'src/backends/software/software_renderer\.cpp:15[0-9]:'` filter.
- **Check 2** finds 4 hits: 1 in `include/chronon3d/runtime/render_runtime.hpp:61` (a `//` comment, stripped by the existing `filter_symbol_in_code_only` helper), and 3 in `src/runtime/render_engine.cpp:71, 90, 202` (real F0.2 rot).  The 3 real callsites are FIXED in the same gate-update commit by mapping `m_renderer->settings()` → `m_renderer->render_settings()`.
- **Check 3** finds 0 hits on `main@6e0c7413` (RenderPipeline already deleted on the fast-forward).  Gate PASS by default; future regression-resistance provided by the bare pattern.

## Decision

1. **Extend the existing gate script** `tools/check_architecture_boundaries.sh` with three new semantic checks numbered `[12/14]`, `[13/14]`, `[14/14]`.  Renumber existing checks 1–11 from `[N/11]` to `[N/14]` for labelling consistency.  No gates are removed, no gates are merged — the linear structure stays intact (per the bug-mitigation comment at the top of the gate script).

2. **Each new check follows the established gate template** (per check 6's pattern): grep `SCRIPT_PATHS` (`include/ src/ tests/ apps/`) → filter via `filter_symbol_in_code_only` for comment-stripping where applicable → `grep -v` for allowlisted sites → `FAILED=1` on residual non-empty hits, `echo PASS` on empty.  Exit 1 on any single failure.

3. **Check 12**: nail the canonical copy-init pattern.
   ```bash
   hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
       -E '\bm_runtime\s*=\s*nullptr\b|\bm_registry\s*=\s*nullptr\b' \
       $SCRIPT_PATHS 2>/dev/null \
       | grep -Ev 'other\.m_(runtime|registry)\b' \
       | grep -Ev 'src/backends/software/software_renderer\.cpp:15[0-9]:' \
       || true)
   ```
   The first `grep -Ev` strips canonical move-assign bodies (`other.m_* = nullptr` is correct pattern in `operator= &&` bodies per TICKET-044 cadence).  The second `grep -Ev` strips the documented destructor-body exception in `software_renderer.cpp` (`~SoftwareRenderer` teardown nulls the owned pointer).

4. **Check 13**: nail the F0.2 rot.
   ```bash
   hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
       -E '\bm_renderer\s*->\s*settings\s*\(' \
       $SCRIPT_PATHS 2>/dev/null \
       | filter_symbol_in_code_only '\bm_renderer\s*->\s*settings\s*\(' \
       || true)
   ```
   Uses the existing `filter_symbol_in_code_only` helper to strip `//` comments referencing the dead pattern.  Pre-merge, this gate is GREEN only after the 3-line `render_engine.cpp` fix (per ADR Decision 5).

5. **Check 14**: nail the cross-class sidecar leak.
   ```bash
   hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
       -E '\bRenderPipeline::m_renderer\b' \
       $SCRIPT_PATHS 2>/dev/null \
       | grep -Ev 'src/runtime/render_pipeline\.cpp:' \
       || true)
   ```
   Currently 0 hits on main.  Filter is vacuous post-F2.4 but if F2.4 is rebased or reverted, the file location remains the canonical anchor — any other TU reading `RenderPipeline::m_renderer` is a sidecar violation of ADR-009.

6. **Synchronously fix `src/runtime/render_engine.cpp`** in the gate-update commit:
   - Line 71: `m_renderer->settings(),` → `m_renderer->render_settings(),`
   - Line 90: `m_renderer->settings(),` → `m_renderer->render_settings(),`
   - Line 202: `return m_impl->m_renderer->settings();` → `return m_impl->m_renderer->render_settings();`
   All 3 callsites sit inside `RenderEngine::Impl` ctor bodies + the public `RenderEngine::settings()` accessor — the OUTER names stay unchanged, only the inner rendering-side accessor is renamed.

7. **Extend the self-test** `tools/check_architecture_boundaries_selftest.sh` with 3 new cases (8, 9, 10) — one per new gate.  Each case builds a FRESH `mktemp` tree (per the existing pattern in cases 1–7), injects the forbidden token, runs the gate via `BOUNDARY_CHECK_ROOT="$TMP"`, asserts exit code is `1`.  Update the summary line to print e.g. `=== check_architecture_boundaries selftest PASSED (10 assertions) ===` after the 3 new cases pass.

## Consequences

### Positive

* **Regression-resistance for F0.2 + F2.4 + future RenderPipeline-like sidecar leaks.**  Any future PR that re-introduces any of the 3 patterns is blocked at CI by the gate.
* **Single source of truth for the canonical patterns.**  Future contributors don't need to discover by code archaeology that `m_runtime = nullptr` in a ctor body is wrong — they read the gate's doc block.
* **Gate counter renumbering is consistent** — `[1/14]`..`[14/14]` is easier to maintain than `[1/11]` + `[12/14]` mixed.
* **Self-test parity**: 3 new cases mirror the existing 7-case self-test philosophy (synthetic tree + forbidden token injection + exit-1 assertion).

### Negative

* **Destructor-body false-positive risk** for Check 12.  The `~SoftwareRenderer` teardown at lines 152–153 of `src/backends/software/software_renderer.cpp` matches the bare `m_runtime = nullptr` pattern — a brittle hardcoded `grep -Ev '15[0-9]:'` filter excludes them.  If a future contributor adds another destructor site with the same pattern (e.g. `~Runtime` teardown), the gate will flag it as advisory; reviewer + ADR update must add to the filter.  The cost is "human review at PR time" vs "the alternative is parser-level ctor-only scoping which is unsupported by ripgrep".
* **Tight coupling between gate and 3-line fix in `render_engine.cpp`**.  The gate update commit MUST also include the 3-line `render_engine.cpp` rename, otherwise post-merge the gate is RED.  Documented in `Decision 6`.
* **Header comment update required**: `tools/check_architecture_boundaries.sh` top-of-file says "WP-0 (PR 0.2 / 0.5 close-out) — Architecture boundary grep checks (9 total)".  After this ADR the count is 14; the header comment must be updated in lockstep.  Documented in `Decision 1`.
* **Self-test output line `=== check_architecture_boundaries selftest PASSED ($PASS assertions) ===`** — the `$PASS` counter auto-increments on the new cases 8/9/10.  If the script is fed a stale `wc -l` count in a comment block, manual maintenance is required.

### Neutral

* **No external consumer impact.**  All three changes are internal tooling + one render-engine internal accessor rename.  No downstream API surface changes.
* **`AGENTS.md` regole di lavoro** (CE 2026-06-21) already call out "non cambiare un gate per nascondere un errore" — this change keeps the gate a hard fail and adds new checks, doesn't relax any.
* **`docs/stabilization-plan/04-cmake-module-registry.md`** does NOT need updating — the gate script is referenced by `.github/workflows/gates.yml` Gate 5 (existing) and runs unchanged in CI.

## Migration path

Not applicable.  Internal tooling (gate script + self-test) + one internal accessor rename (`m_renderer->settings()` → `m_renderer->render_settings()`) inside `src/runtime/render_engine.cpp` only.  No external consumer ABI/API churn; no exported symbol changes.

## References

* `tools/check_architecture_boundaries.sh` — the script extended by this ADR; gates renumbered `[1/14]`..`[14/14]`.
* `tools/check_architecture_boundaries_selftest.sh` — extended with cases 8, 9, 10.
* `src/runtime/render_engine.cpp` lines 71, 90, 202 — the 3 fixed callsites.
* `src/backends/software/software_renderer.cpp` lines 152, 153, 172, 173 — the 4 sites scanned by Check 12; only lines 172, 173 are filtered via `other.` exclusion; lines 152, 153 are filtered via the documented destructor exception.
* [ADR-008 — `RenderEngine::renderer()` returns pointer](./ADR-008-render-engine-renderer-ptr-return.md).
* [TICKET-011]((...) — render-runtime ownership, the construction-sequence invariant that constrains `m_runtime`/`m_registry`/`m_renderer` lifetime).
* F0.2 PR (`p0/fix-software-renderer-move-ctor`) — the original `settings()` → `render_settings()` rename.  Re-applied here to the 3 callsites that escaped the original wave.
* F2.4 prior work (`p1/render-pipeline-single-identity`) — the `runtime::RenderPipeline` deletion that ADR-010 Check 14 regression-guards.
* Branch: `chore/boundary-gate-semantic-checks` @ `main@6e0c7413` (fast-forwarded to `6e0c7413` from `14dbc415`).
* CI: `.github/workflows/gates.yml` Gate 5 — runs `tools/check_architecture_boundaries.sh` and `tools/check_software_renderer_boundary.sh` on every push.  Adds 3 new failure modes (12, 13, 14) without modifying the integration surface.
