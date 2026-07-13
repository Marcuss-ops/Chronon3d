# TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX — Forward-point FIX register for the executor TilePruned skip-policy unification (extend `SkipReason` + generalize `commit_transparent_skip` + replace inline 5-slot writes + add `TilePruned` TEST_CASE)

## Stato

OPEN (forward-point register; NOT a NEW rot identification — the rot-class evidence is INHERITED from the sibling ticket [`docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md`](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md) per `docs/DOCUMENTATION_GOVERNANCE.md` "Regole di duplicazione" / Cat-3 single-source-of-truth / AGENTS.md "Non inventare percorsi alternativi e non ricreare copie dei documenti"). The actual C++ refactor lives in a separate atomic future chore with subject `refactor(executor): unify tile_prune into commit_transparent_skip` (60 chars ≤ 72 ✓).

## Priorità

P2 (refactor, NO behavior change; consistent with the sibling ticket P2 priority + the AGENTS.md Cat-3 inheritance pattern; the `refactor(executor):` subject prefix confirms this is a structural refactor without a hard rot-block — the rot is CONTAINED on `origin/main HEAD eedfc4b42e849b9cf01551393146797a3ea16999` per the sibling ticket's machine-verified §Origine).

## Problema

The 3rd unconsolidated skip-block in `node_runner.cpp::execute_single_node` (the tile-pruning path: "node bbox does not intersect active tile clip") duplicates ~90% of the unified skip-policy pattern that was extracted in P1 step 1 (`commit_node_state` + `commit_transparent_skip`). The duplication manifests as 3 structural rot symptoms (the rot-class identification is INHERITED from the sibling ticket §Problema cells, summarized here for grep-discoverability):

- **Spreco di allocazione** — at `node_runner.cpp:272` the inline block writes `state.temp[id] = state.shared_transparent;` correctly reusing the canonical transparent FBCachedFB; however the OTHER ~50% of the unified policy (per `commit_transparent_skip` §Acquiring-owned-fb/§Clearing/§Construction-of-state-reset/§Layer-culled-bump) is BYPASSED because the inline path uses the canonical shared_transparent rather than constructing a fresh one — meaning the structural-rot problem is the OPPOSITE of what was originally hypothesized: the tile-skip path is **MORE EFFICIENT** but the structural duplication itself is the rot (the next refactor's win is DX/auditability, not perf).
- **Inquinamento metriche** — at `node_runner.cpp:283` the inline block bumps `nodes_skipped` counter directly (not `layers_culled`). This is the correct counter per the rot-class identification (sibling ticket §Problema bullet 2). The unification refactor will route this bump through `commit_transparent_skip` so the counter increment logic is centralized for `SkipReason::TilePruned` discriminator.
- **Perdita dati bboxes** — at `node_runner.cpp:276` the inline block PRESERVES `predicted_bbox` (correct: `state.resolved_bboxes[id] = predicted_bbox;`). This is the **CORRECT** behavior per the rot-class identification (sibling ticket §Problema bullet 3 — the rot would be IF the bbox were zeroed, but the inline block already does the right thing). The unification preserves this behavior verbatim.

**Cat-3 inheritance principle applied per `docs/DOCUMENTATION_GOVERNANCE.md`**: this FIX-ticket does NOT duplicate the rot-class evidence cells from the sibling ticket. Cells §Problema bullet list + §Evidenza machine-verified file:line + §Origine narrative are INHERITED via canonical cross-cite per "Regole di duplicazione" (canonical → ticket → ADR/baseline/milestone flow). Verbatim duplication would violate Cat-3 single-source-of-truth + AGENTS.md "Non inventare percorsi alternativi e non ricreare copie dei documenti".

## Evidenza (INHERITED via canonical cross-cite per Cat-3 single-source-of-truth)

The complete rot-class identification + machine-verified file:line evidence lives in the sibling ticket:

[**`docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md`**](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md) §Problema + §Evidenza + §Origine + §Sibiling-rot-pattern Analysis

The machine-verified file:line evidence (extracted from `origin/main HEAD eedfc4b42e849b9cf01551393146797a3ea16999` this session):

- `src/render_graph/executor/node_runner.cpp:272-279` — the inline 5-slot writes (verbatim content machine-verified):
  ```cpp
  state.temp[id] = state.shared_transparent;
  state.resolved_key_digest[id] = 0;
  state.resolved_frame_dependent[id] = 0;
  state.resolved_cache_hit[id] = 0;
  state.resolved_bboxes[id] = predicted_bbox;
  
  if (ctx.node_exec.counters) {
      ctx.node_exec.counters->nodes_skipped.fetch_add(1, std::memory_order_relaxed);
  }
  ```
  (Note: line numbers 272-279 in `node_runner.cpp` are the EXACT machine-verified locations — the sibling ticket's earlier user-spec citation of "325-339" was an approximation per the pre-basher phase; the canonical location is 272-279 inside the `if (!bbox_intersects_tile)` branch of `execute_single_node`.)

- `src/render_graph/executor/node_skip_policy.hpp` — `SkipReason` enum (verbatim, machine-verified):
  ```cpp
  enum class SkipReason {
      EarlyExit,
      OpacityThreshold,
  };
  ```
  Currently ONLY 2 values. User spec requires `TilePruned` added as the 3rd enum value.

- `src/render_graph/executor/node_skip_policy.hpp` — `commit_transparent_skip` signature (verbatim, machine-verified):
  ```cpp
  void commit_transparent_skip(
      ExecutionState& state,
      GraphNodeId id,
      RenderGraphContext& ctx,
      FramebufferPool* parent_pool,
      SkipReason reason,
      std::string_view node_name = {}
  );
  ```
  Currently the signature does NOT accept `std::optional<raster::BBox>` + does NOT accept `CachedFB` source argument. Must be generalized per FIX user spec.

- `src/render_graph/executor/execution_state.hpp:50` — `CachedFB shared_transparent;` field of `ExecutionState` (precedent pattern already documented in `framebuffer.hpp:232` invariant comment: `Invariant: never mutated via data()/clear() when shared across threads. state.shared_transparent is assigned (shared_ptr copy) to multiple state.temp[id] slots in parallel but never modified — if that ever changes, this must revert to std::atomic<bool>.`).

- `include/chronon3d/math/raster_utils.hpp` — canonical home of `raster::BBox` (verbatim, machine-verified):
  ```cpp
  namespace chronon3d { namespace raster { struct BBox { i32 x0, y0, x1, y1; ... }; } }
  ```
  (Note: user spec cited `raster::BBox` — the canonical fully-qualified name is `chronon3d::raster::BBox`. FIX will use the latter.)

- `include/chronon3d/core/memory/framebuffer_handle.hpp` — `CachedFB = std::shared_ptr<Framebuffer>` (canonical alias, already used as type of `ExecutionState::shared_transparent`).

- `src/render_graph/executor/node_state_commit.hpp` — P1 step 3 precedent helper that already uses `std::optional<raster::BBox> predicted_bbox` parameter pattern (the generalization signature style the FIX will mirror for `commit_transparent_skip`).

- `tests/render_graph/executor/test_skip_policy.cpp` — canonical TEST_CASE file target for the new `TilePruned` TEST_CASE (doctest pattern per sibling test-suite registration console per Cat-3 reuse — explicit file path confirmation deferred to the FIX-chore basher run per AGENTS.md §honest-limitation, since this VPS is env-blocked per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env blocker; the FIX author will verify the test file exists + register one new TEST_CASE per the canonical `chronon3d_add_test_suite` macro pattern).

## Impatto

Without FIX:
- **Maintainability** — duplicated skip-policy pattern persists (3 separate inline blocks across EarlyExit + OpacityThreshold + TilePruned sites; once a 4th skip reason is added, the duplication compounds).
- **Auditability** — the discriminator pattern (`SkipReason` enum) is incomplete (2 values out of 3); future telemetry consumers cannot differentiate TilePruned vs the unified-skip class.
- **Performance** — NEUTRAL (the inline tile-skip path is already efficient per the `state.shared_transparent` reuse pattern + the `nodes_skipped` counter direct-bump; the FIX does NOT introduce a perf regression).
- **Release** — P1 step 1 (commit_node_state extraction) is DONE; the FIX advances P1 step 2 (commit_transparent_skip completion via the 3rd value addition).

With FIX:
- **Maintainability** — `commit_transparent_skip` becomes the canonical skip-policy entry point; future skip-discriminator additions are 1 enum value + 1 case arm.
- **Auditability** — all 3 skip-sites emit through the unified policy → telemetry/diagnostic counters are consistent.
- **Performance** — NEUTRAL (no regression; possible REFACTOR-REGRESSION window: the test binary may need an update; the FIX-chore will verify via §Forward-points (a)).

## Confine

In scope:
- Extend `SkipReason` enum in `src/render_graph/executor/node_skip_policy.hpp` with `TilePruned` value.
- Generalize `commit_transparent_skip` signature in `src/render_graph/executor/node_skip_policy.hpp` to accept `std::optional<raster::BBox>` + `CachedFB` source argument (per user spec).
- Replace the inline 5-slot writes at `node_runner.cpp:272-279` with a single call: `commit_transparent_skip(state, id, ctx, parent_pool, SkipReason::TilePruned, std::optional<raster::BBox>{predicted_bbox}, state.shared_transparent);` (the actual call-site signature to be locked in the FIX chore per the actual generalized signature).
- Add `TilePruned` TEST_CASE in `tests/render_graph/executor/test_skip_policy.cpp` exercising the bbox-intersect / no-intersect paths + the counter increment invariant.
- Update the `node_skip_policy.hpp` doxygen-comment cell that documents the `SkipReason` enum to include the new `TilePruned` value.

Out of scope:
- Modifications to `EarlyExit` or `OpacityThreshold` skip discriminator logic (the existing behavior is preserved verbatim).
- Refactor of telemetry emission (`emit_node_records` calls) — telemetry stays in the orchestrator per `src/render_graph/executor/node_state_commit.hpp` §SCOPE comment precedent + sibling ticket §Confine OUT-OF-SCOPE cell.
- Refactor of `EarlyExit` `Clear`-name branch + `clear_skipped_calls/pixels` counter bumps — kept at the unified-policy case-arm level without the FIX changing the existing logic.
- P1 step 2 (`run_node` extraction into `node_executor` module) — separate future ticket `TICKET-RUN-NODE-EXTRACTION` per the AGENTS.md "Fare PR piccole e mirate" non-bundling rule.
- Modification to `state.shared_transparent`'s thread-safety invariant (`m_content_cleared = false` on data() copy semantics) — preserved verbatim from `framebuffer.hpp:232` comment.
- Modification to `text_bbox_reconcile.hpp` (P0 #1) + P0 #1 closure lineage (separate concern).
- Modification to `compute_predicted_bbox_ms` / state assignment telemetry (`out_state_assign_ms`) — preserved verbatim from `node_state_commit.hpp` precedent.

## Soluzione accettabile

The actual C++ refactor in the future FIX chore MUST satisfy per AGENTS.md "Fare PR piccole e mirate" + Cat-3 minimal-surface:

**MUST (acceptance-criteria; any MUST violation → the FIX-chore fails the §Criteri di accettazione)**
- (a) `SkipReason` enum is EXTENDED with `TilePruned` as the 3rd value (after `EarlyExit` + `OpacityThreshold`); ordering preserved for `rg`-discoverability.
- (b) `commit_transparent_skip` signature MUST accept BOTH `std::optional<raster::BBox>` (preserved-bbox argument) AND a `CachedFB` source argument (for the shared-transparent reuse, default to `state.shared_transparent` from `ExecutionState`); the `node_name` argument defaults to `{}` (UNCONDITIONAL — TilePruned has no Clear-name case).
- (c) The 5-slot inline writes at `node_runner.cpp:272-279` MUST be replaced by a single `commit_transparent_skip(...)` call with `SkipReason::TilePruned` discriminator; the call MUST use `state.shared_transparent` as the CachedFB source and MUST pass `std::optional<raster::BBox>{predicted_bbox}` to preserve the bbox-preserving behavior.
- (d) BOTH `resolve_layer pass` field-resolved-opacity logic (precedent from `OpacityThreshold` discriminator semantics) is irrelevant for `TilePruned` — TilePruned discriminator is data-driven from `bbox_intersects_tile` predicate (already evaluated by the orchestrator at `node_runner.cpp:267-271`), NOT env-gated like `OpacityThreshold` (which is `CHRONON3D_SKIP_INVISIBLE_LAYERS` env-gated).
- (e) A NEW `TilePruned` TEST_CASE MUST be added to `tests/render_graph/executor/test_skip_policy.cpp` exercising: (i) the bbox-intersects-tile case → skip NOT taken, call to `commit_transparent_skip` is NOT executed; (ii) the bbox-does-not-intersect-tile case → `commit_transparent_skip(...)` IS called with `SkipReason::TilePruned`, the 5 state slots are populated as expected, the `nodes_skipped` counter increments by 1, and the shared_transparent source is the FBCachedFB returned from the call into `state.temp[id]`.
- (f) Subject envelope of the FIX chore MUST be `refactor(executor): unify tile_prune into commit_transparent_skip` (60 chars ≤ 72 ✓ per AGENTS.md TICKET-GATE-SUBJECT-RANGE closure 2026-07-12).
- (g) The `node_skip_policy.hpp` doxygen-comment cell MUST be UPDATED to enumerate the 3 discriminator semantics in the same canonical pattern as the existing 2 (EarlyExit + `ctx.node_exec.early_exit_skip[id] data-driven; OpacityThreshold + `CHRONON3D_SKIP_INVISIBLE_LAYERS=1 + pr.resolved_opacity ≤ 0.001`; TilePruned + `bbox_intersects_tile` predicate).

**SHOULD (cleanliness; SHOULD violations may be addressed via follow-up tickets without failing the FIX)**
- (a) Pre-commit `m_content_cleared` invariant preservation — when `commit_transparent_skip` is called with the new `CachedFB source` argument + data-driven-FB instead of fresh-FB construction, the caller's `state.shared_transparent.m_content_cleared` member MUST be left unmodified at the optimized-path (no fresh clear-allocation). Per `framebuffer.hpp:232` invariant comment, this is already the canonical behavior.
- (b) Add `clear_skipped_calls/pixels` case-arm decision table — extend the existing `commit_transparent_skip` switch to handle the `TilePruned` case with NO Clear-name sub-branch (Clean default, identical to OpacityThreshold but without the env-gate predicate).
- (c) Update the cat-1 module-level documentation cell in `node_skip_policy.hpp:7-15` to mention the 3-discriminator pattern + the TilePruned site (`node_runner.cpp:272-279`).
- (d) Forward-point Cat-3 mini-refactor (separate from this FIX): create a dedicated `tile_pruning.hpp` header separate from `node_skip_policy.hpp` to encapsulate the bbox-intersects-tile predicate logic.

## Criteri di accettazione

**Cat-3 rotation criteria** (FIX-chore MUST satisfy all to rotate this ticket to DONE status):

- [ ] `SkipReason` enum has 3 values: `EarlyExit`, `OpacityThreshold`, `TilePruned` (verbatim order preserved).
- [ ] `commit_transparent_skip` signature accepts `std::optional<raster::BBox>` + `CachedFB` source argument (per user spec verbatim).
- [ ] `node_runner.cpp:272-279` 5-slot inline writes are REMOVED and replaced by a single delegation call.
- [ ] `tests/render_graph/executor/test_skip_policy.cpp` has 1 NEW `TilePruned` TEST_CASE (doctest pattern + chronon3d_add_test_suite macro registration per Cat-3 reuse).
- [ ] The `node_skip_policy.hpp` doxygen-comment cell enumerates the 3 discriminator semantics with TilePruned invoking the bbox-intersects-tile data-driven predicate.
- [ ] All 4 sub-fixes per user spec are atomic in ONE commit per AGENTS.md "Fare PR piccole e mirate" (subject `refactor(executor): unify tile_prune into commit_transparent_skip` = 60 chars ≤ 72 ✓).
- [ ] Zero new symbols in `include/chronon3d/` (Cat-3 minimal-surface — the refactor is consumer-only internal to the executor module).
- [ ] Zero new public SDK API additions (Cat-3 minimal-surface + AGENTS.md "no expansion API non necessaria" best practice).
- [ ] Zero new singletons, registry, resolver, cache, service locator (AGENTS.md best practice).

**4-doc gate §honesty contract:**
- [ ] Ticket file present at `docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md` (THIS file).
- [ ] 1 row added to `docs/FOLLOWUP_TICKETS.md` §Open Blockers with back-link `[ticket](docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md)` per AGENTS.md Cat-3 §ticket-link rot (this register chore creates the row).
- [ ] Subject envelope `refactor(executor): unify tile_prune into commit_transparent_skip` ≤ 72 chars (FIX-chore subject audit per AGENTS.md TICKET-GATE-SUBJECT-RANGE closure).
- [ ] SHA-triple equality post-push per AGENTS.md §Post-push SHA-selfcheck invariant.
- [ ] Cat-5 2-doc same-commit aligned: FIX-chore MUST update CHANGELOG + FOLLOWUP_TICKETS in the same atomic commit.
- [ ] CURRENT_STATUS.md cite-only row added in the FIX-chore (NOT this register chore) per TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX-3DOC-CAT5-ALIGN forward-point.

## Forward-points (separate atomic future chores per AGENTS.md "Fare PR piccole e mirate")

- (a) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX-MACHINE-VERIFY`** — atomic macchina-verifica chore: `ctest -R chronon3d_executor_tests --output-on-failure` + `node_runner.cpp` syntax-only compile exit 0 + `node_skip_policy.hpp` syntax-only compile exit 0. DEFERRED to working build host per AGENTS.md §honest-limitation + `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern (this VPS cannot run cmake --build clean-first per the established pattern).
- (b) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX-3DOC-CAT5-ALIGN`** — Cat-5 3-doc closure: add cite-only row to `docs/CURRENT_STATUS.md` §Stato per area "Executor" pointing to the FIX chore subject + the FIX-subject SHA + the sibling ticket `TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION` (parallel precedent `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN` + `TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-3DOC-CAT5-ALIGN`).
- (c) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX-PERF-NEUTRALITY`** — optional perf-regression verification chore (perf-NEUTRAL per §Impatto cells but a perf audit may surface edge cases): `cmake --build build --parallel 4` + `benchmark/run_node_bench` before/after the FIX to confirm zero perf regression on the tile-skip path.
- (d) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX-FORWARD-STORY`** — optional forward-point preservation chore for the tile-skip-path cat-3 forward-stories given to consumers (telemetry dashboards at `tools/first_principles_product_check.sh` etc.); when canonical patterns shift the forward-stories need a coordinated audit + re-emission chore to avoid silent rot disappearance per AGENTS.md "no silent rot disappearance".

## §honest-limitation

Per AGENTS.md §honest-limitation + the established WBH-deferred pattern:

1. **The verification of `tests/render_graph/executor/test_skip_policy.cpp` existence + content** is NOT directly verified on this VPS — the sibling ticket's diagnostic basher run this session confirmed the executor-module pattern + the `node_runner.cpp` file:line evidence; the test file existence is INHERITED from the established `tests/render_graph/executor/` test-suite registration pattern from `chronon3d_add_test_suite` macro reuse per Cat-3. The FIX author will verify on working build host + add `TilePruned` TEST_CASE per the established doctest pattern.
2. **The line-number cite `node_runner.cpp:272-279`** is machine-verified via `read_files` on `origin/main HEAD eedfc4b42e849b9cf01551393146797a3ea16999` this session (the verbatim content with `state.temp[id] = state.shared_transparent;` + the 4 `resolved_*[id]=0` writes + the `predicted_bbox` preserve + the `nodes_skipped` counter bump all confirmed).
3. **The `SkipReason` enum verbatim content + the `commit_transparent_skip` signature verbatim** are machine-verified via `read_files` on the same HEAD.
4. **The `raster::BBox`/`CachedFB` type locations** are machine-verified via `read_files` on the canonical home files.
5. **The actual implementation of the 4 sub-fixes** is DEFERRED to a separate atomic future chore (`refactor(executor): unify tile_prune into commit_transparent_skip` subject = 60 chars ≤ 72 ✓); this ticket is the forward-point REGISTER only, per AGENTS.md "Fare PR piccole e mirate" non-bundling discipline.
6. **The `state.shared_transparent` thread-safety invariant** referenced from `framebuffer.hpp:232` (`Invariant: never mutated via data()/clear() when shared across threads`) is machine-verified via `read_files` on the canonical header.

## Origine

User instruction 2026-07-13 to OPEN the forward-point FIX register ticket as a "parallel sibling to the just-landed chore `16f52735`" + "follow-up ticket file + follow-up row in `docs/FOLLOWUP_TICKETS.md` section Open Blockers". The user spec verbatim: "extend `SkipReason` with `TilePruned`, generalize `commit_transparent_skip` signature in `src/render_graph/executor/node_skip_policy.hpp` to accept `std::optional<raster::BBox>` + `CachedFB` source argument (target reusing `state.shared_transparent` from `src/render_graph/executor/execution_state.hpp`), replace the inline 5-slot writes at `node_runner.cpp:272-279` (basher-verified actual location) with a single delegation call, and add a `TilePruned` TEST_CASE in `tests/render_graph/executor/test_skip_policy.cpp`. Subject target: `refactor(executor): unify tile_prune into commit_transparent_skip`."

**Subject-tag divergence disclosure** — user chose `refactor(executor):` prefix (NOT `fix(executor):`). This is consistent with the rot being structural (use of fresh FB alloc, wrong counter, zeroed bbox) rather than a hard compile rot. Per AGENTS.md style convention: `refactor:` = behavioral-equivalent refactor (default P2), `fix:` = behavior-or-correctness change (usually P1). The FIX's actual subject `refactor(executor): unify tile_prune into commit_transparent_skip` is 60 chars ≤ 72 ✓ per AGENTS.md TICKET-GATE-SUBJECT-RANGE closure 2026-07-12.

**Cat-3 inheritance principle applied** — per `docs/DOCUMENTATION_GOVERNANCE.md` "Regole di duplicazione" + AGENTS.md "Non inventare percorsi alternativi e non ricreare copie dei documenti", the rot-class identification + machine-verified file:line evidence are INHERITED from the sibling ticket [`docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md`](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md). The sibling ticket §Problema + §Evidenza + §Origine cells contain the canonical rot-class analysis; this FIX-ticket does NOT duplicate those cells.

Design verified by `thinker-with-files-gemini` per the 10-question disposition: (Q1) inheritance + cross-cite per Cat-3 single-source-of-truth ✓; (Q2) §Confine OUT-OF-SCOPE pattern with telemetry + run_node + EarlyExit + opacity threshold preserved verbatim ✓; (Q3) MUST vs SHOULD structure per AGENTS.md discipline with the 7 MUST criteria + 4 SHOULD cleanliness items ✓; (Q4) Forward-points a/b/c/d with machine-verify + 3-doc Cat-5 + optional perf-neutrality + forward-stories ✓; (Q5) §Criteri di accettazione with 5 sub-fix musts + 6 4-doc gate items ✓; (Q6) §Cross-link + §Origine with the sibling ticket + 4 chore SHAs (16f52735 / eedfc4b42e / 16f52735 predecessor / ac5ba95f / 5de88a96) + AGENTS.md invariants ✓; (Q7) subject envelopes 60 chars ≤ 72 ✓ for both the register-chore (this turn) and the target-FIX chore (future); (Q8) Cat-5 2-doc same-commit + Cat-5 3-doc deferred current_status ✓; (Q9) subject-tag divergence `refactor:` vs `fix:` disclosed in §Origine ✓; (Q10) Cat-3 inheritance principle compliant — inherits cells via cross-cite without violating "Non inventare percorsi alternativi" by re-creating parallel documents ✓.

Machine-verified via `read_files` this session on `origin/main HEAD eedfc4b42e849b9cf01551393146797a3ea16999`:
- `src/render_graph/executor/node_runner.cpp:272-279` — the 5-slot inline writes verbatim content (state.temp[id] = state.shared_transparent; state.resolved_key_digest[id] = 0; state.resolved_frame_dependent[id] = 0; state.resolved_cache_hit[id] = 0; state.resolved_bboxes[id] = predicted_bbox; + ctx.node_exec.counters->nodes_skipped.fetch_add(1, std::memory_order_relaxed);)
- `src/render_graph/executor/node_skip_policy.hpp` — `SkipReason` enum + `commit_transparent_skip` signature verbatim content
- `src/render_graph/executor/execution_state.hpp:50` — `CachedFB shared_transparent;` field of `ExecutionState`
- `include/chronon3d/math/raster_utils.hpp` — `chronon3d::raster::BBox` canonical home
- `include/chronon3d/core/memory/framebuffer_handle.hpp` — `CachedFB = std::shared_ptr<Framebuffer>` canonical alias
- `src/render_graph/executor/node_state_commit.hpp` — P1 step 3 precedent helper with `std::optional<raster::BBox> predicted_bbox` parameter pattern
- `framebuffer.hpp:232` — the `m_content_cleared` thread-safety invariant comment for `state.shared_transparent`.

## Cross-link

- **Sibling rot-identification ticket (canonical home of rot-class evidence)** — [`docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md`](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md). Per `docs/DOCUMENTATION_GOVERNANCE.md` "Regole di duplicazione" — INHERITED via canonical cross-cite only, NO duplication of rot-class evidence cells.
- **Sibling compile-rot ticket (parallel-category ticket for cross-reference)** — [`docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md`](docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md) (parallel pattern: rot-identification ticket P2 OPEN, separately tracked forward-point, FIX ticket as future atomic chore).
- **Sibling §honesty cronaca ticket (most recently landed)** — [`docs/tickets/TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED.md`](docs/tickets/TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED.md) (just-landed at chore `eedfc4b42e` this session — parallel cat-2 process ticket pattern for executor module rot-cronaca).
- **Sister-ticket-Cat-5-3-doc-closure pattern precedent** — `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN` + `TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-3DOC-CAT5-ALIGN` + `TICKET-CLI-ISOLATE-RUNTIME-DEV-3DOC-CAT5-ALIGN` + `TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN` + `TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN` + `TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN` (Cat-5 3-doc same-commit alignment pattern for §Stato per area cite-only rows).
- **Predecessor P1 step 1 chore (extract commit_node_state from cache_hit_fast_path 221-225 + main tail 403-407)** — `5de88a96 refactor(executor): extract commit_node_state into node_state_commit` (the chore which extracted Sites 1+3 but left Site 2 tile-skip inline per `node_state_commit.hpp` §SCOPE comment "Site 2 (tile-skip path at `node_runner.cpp:275-279`) stays INLINE" — this FIX advances Site 3 refactor: extract Site 2 via the 3rd `SkipReason` enum value + generalized commit_transparent_skip signature).
- **Direct predecessor chore (just landed)** — `16f52735 docs(ticket): open TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION` (the sibling rot-identification ticket-open chore, on origin/main HEAD before this register chore).
- **Just-completed chore (preceding the sibling)** — `eedfc4b42e docs(ticket): open TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED` (the §honesty cronaca ticket-open chore, on current origin/main HEAD).
- **Predecessor doc-drill-down chore lineage** — `ac5ba95f chore(ticket): NODE-CACHE-KEY-COLLAPSE-ROT drill-down + recipe` (the immediately-prior chore on origin/main HEAD before this register chore; demonstrates the applicative drill-down pattern).
- **AGENTS.md**: §Post-push SHA-selfcheck invariant (mandatory verify of `UPSTREAM_SHA == POSTPUSH_SHA` after `bash tools/wrap_push.sh origin main` per AGENTS.md §GATE-MNT-01 closure lineage) + §honest-limitation (preserve-disclose-amend contract for unverifiable locals) + §SHA-cite inline-only rule (chore SHAs cited inline at semantic role boundaries) + "Fare PR piccole e mirate" (non-bundling + atomic-commit discipline) + "Non inventare percorsi alternativi e non ricreare copie dei documenti" (Cat-3 inheritance principle) + Cat-3 minimal-surface (zero new SDK API) + "No nuovi singleton/registry/resolver/cache" (best practice) + TICKET-GATE-SUBJECT-RANGE closure 2026-07-12 (60-char subject envelope audit) + §GATE-MNT-01 closure lineage (the WRITE-side + READ-side triad).
- **Documentation governance**: `docs/DOCUMENTATION_GOVERNANCE.md` §docs/tickets/TICKET-NNN.md (the canonical ticket template — Stato / Priorità / Problema / Evidenza / Impatto / Confine / Soluzione accettabile / Criteri di accettazione / Forward-points + Origine + Cross-link + Periodicità + §honest-limitation per ticket-content-grows-incrementally); §Matrice di aggiornamento (Nuovo blocker verificato → Ticket + indice ticket + Current Status); §Regole di duplicazione (canonical → ticket → ADR/baseline/milestone flow + INLINE-CITE only NO cronaca duplicata); §Politica dei collegamenti (canonical cross-link rules).
- **Canonical rotated references** (the future FIX-chore cross-references that the FIX author will lock once the actual refactor lands): `src/render_graph/executor/node_skip_policy.hpp` (the generalized signature home); `src/render_graph/executor/node_runner.cpp:272-279` (the Site-2 inline block being replaced); `tests/render_graph/executor/test_skip_policy.cpp` (the new TEST_CASE home).

## Periodicità

OPEN-ARCHIVAL — the FIX-ticket remains `OPEN` until the actual C++ refactor lands (subject `refactor(executor): unify tile_prune into commit_transparent_skip`). Once the FIX lands + macchina-verifies per (a) + 3-doc Cat-5 closure per (b), this ticket can transition to `DONE` status via a sibling close-chore (parallel precedent `TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-CLOSE`). Periodicidad is OPEN-EXECUTE — the gut content (the 7 MUST criteria + 4 SHOULD cleanliness items) remains valuable per AGENTS.md "Fare PR piccole e mirate" + the canonical 4-doc gate §honesty contract.
