# TICKET-NODE-CACHE-KEY-COLLAPSE-ROT — Isolate NodeCacheKey collapse rotation in executor module

## Stato

OPEN (rot CONTAINED, no active surge on `origin/main HEAD 5de88a96`)

## Priorità

P2

## Problema

`NodeCacheKey` is the canonical cache-key type used by the executor pipeline. The rotation class may include incomplete-type / forward-declaration patterns around `NodeCacheKey` consumers. Rot is CONTAINED — no active surge from this rotation class on `origin/main HEAD 5de88a96` — but the rot-class is present in the executor module scope and warrants formal tracking. The P1 step 2 successor chore (`refactor(executor): extract run_node into node_executor module`) will include `NodeCacheKey` transitively, so isolating this rot now prevents re-emerge during the refactor.

## Evidenza

Concrete file:line evidence on `origin/main HEAD 5de88a96` (verified via grep + diagnostic 2026-07-13):

- Type definition (rot root): `include/chronon3d/cache/node_cache.hpp` (canonical `NodeCacheKey` class)
- Active usage sites (5+ files in the rendering pipeline):
  - `src/render_graph/pipeline/refresh/source.cpp:24,79`
  - `src/render_graph/nodes/TextRunNode.cpp:82,328`
  - `src/render_graph/nodes/source_node.cpp:183`
  - `src/render_graph/nodes/multi_source_node.cpp:21,119`
  - `src/render_graph/executor/telemetry_emitter.hpp:18,35`
- Diagnostic method: `grep -rn "NodeCacheKey" src/render_graph/ include/` returns the 5+ active consumers; `g++ -std=c++23 -fsyntax-only -c src/render_graph/executor/node_runner.cpp` returns exit 0 (rot-class is structural rather than compile-time sensitive — no immediate compiler error).

Per the prior `5de88a96 refactor(executor): extract commit_node_state into node_state_commit` chore's HARNESS-COMPLETE verification, the executor module syntax-only compile was exit 0 with no `NodeCacheKey` / `FramebufferPool` warnings — rot is CONTAINED.

## Impatto

- Stability: rot is contained — no active build break. Risk increases if P1 step 2 refactor reintroduces incomplete-type patterns on `NodeCacheKey` transitively.
- Maintainability: the executor module becomes harder to reason about if forward-declarations rot further (the P1 step 2 chore introduces `node_executor.{hpp,cpp}` which transitively pulls in `NodeCacheKey`).
- Performance: N/A (no perf regression observed on `origin/main`).
- Release: deferrably-impacted — P1 step 2 lands on top of this rot; releasing without a binding forward-point risks future incomplete-type rot re-emergence during subsequent extractor chores.

## Confine

In scope:
- `NodeCacheKey` rotation patterns within the executor module scope (`src/render_graph/executor/*.cpp`).
- Forward-declaration / include-guard rot patterns targeting `NodeCacheKey` consumers.
- Pre-condition tracking for P1 step 2 (extract `run_node`) refactor — the next chore commit MUST NOT re-introduce the rot-class.

Out of scope:
- The P1 step 2 chore itself (the `run_node` extraction is the separate `chore B`, NOT this ticket's concern).
- ROT A: 6-fork `chronon3d::chronon3d::` double-namespace rotation (already resolved or stale-documented; not the user-named rotation).
- ROT B: `content/text_helpers` ~300-error rotation (out of scope per user-selected "Isolate rot first" path).

## Soluzione accettabile

Formal opening of the rotation ticket + 1-row index in `docs/FOLLOWUP_TICKETS.md` with back-link to the ticket file. The rotation class is identified, scoped, and tracked. Future chore to actually address the rotation (`TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-FIX`) lands in its own atomic commit per AGENTS.md "Fare PR piccole e mirate". No source-code changes needed for opening the ticket itself.

## Criteri di accettazione

- [ ] Ticket file present at `docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md` follows the `docs/DOCUMENTATION_GOVERNANCE.md` §docs/tickets/TICKET-NNN.md structure (Stato / Priorità / Problema / Evidenza / Impatto / Confine / Soluzione accettabile / Criteri di accettazione / forward-points / Origine / Cross-link).
- [ ] 1 row added to `docs/FOLLOWUP_TICKETS.md` §Open Blockers table with back-link `[ticket](docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md)` per AGENTS.md Cat-3 §ticket-link rot.
- [ ] Subject message `docs(ticket): open TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` ≤ 72 chars (envelope audit).
- [ ] SHA-triple equality post-push per AGENTS.md §Post-push SHA-selfcheck invariant.
- [ ] Cat-5 2-doc aligned per AGENTS.md §docs governance: CHANGELOG entry + FOLLOWUP row + (optional) CURRENT_STATUS cite-only row (forward-pointed to TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN if NOT done in same commit).

## Forward-points (separate atomic chores per AGENTS.md "Fare PR piccole e mirate")

- (a) **`TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-FIX`** — atomic future chore applying the actual rotation fix (full `NodeCacheKey` type inclusion in executor module consumers, OR forward-declaration guard pattern). NOT in this commit per AGENTS.md "Fare PR piccole e mirate" + Cat-3 anti-dup rule.
- (b) **`TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-MACHINE-VERIFY`** — working build host macchina-verifica: `cmake --build build/chronon/<preset>` + `ctest -R chronon3d_executor_tests --output-on-failure` confirms zero `NodeCacheKey` incomplete-type errors post-fix.
- (c) **`TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN`** — Cat-5 3-doc closure: add cite-only row to `docs/CURRENT_STATUS.md` §Stato per area "Executor" if NOT done in the same commit (deferred per Cat-3 anti-dup, parallel to existing `TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN` precedent).

## §honest-limitation

Per AGENTS.md §honest-limitation + the established WBH-deferred pattern, the actual rotation fix is DEFERRED to working build host macchina-verifica. The ticket OPENS the rotation formally so the next P1 step 2 chore can land on top with the rotation explicitly tracked. Verdict: OPEN/ON-TRACK but defer-fix-WBH per the §honest-limitation discipline.

## Origine

User ask_user clarification 2026-07-13 chose "Isolate rot first" path. Design verified by `thinker-with-files-gemini`:

- Why this rot and not ROT A/B? Pick ROT C (executor `NodeCacheKey` rotation rot) rather than the upstream double-namespace (a) or text_helpers (b) because:
  - Literal slug match: `NODE-CACHE-KEY-COLLAPSE-ROT` directly names `NodeCacheKey` via file:line evidence.
  - Executor-domain coupling: chore B is the executor refactor — direct rot ↔ refactor coupling, no domain mismatch.
  - Precedent: the prior `5de88a96 refactor(executor): extract commit_node_state` left the rotation CONTAINED on origin/main — this ticket formalizes that containment in the canonical ticket-home format.

## Cross-link

- **Predecessor chore**: `5de88a96 refactor(executor): extract commit_node_state into node_state_commit` (lands `node_state_commit.{hpp,cpp}` extracting 5 state-slot writes from `execute_single_node` on `origin/main HEAD 5de88a96`; the `run_node()` function remains inline in `node_runner.cpp`).
- **Successor chore (forward-point)**: P1 step 2 = `refactor(executor): extract run_node into node_executor module` (lands `node_executor.{hpp,cpp}` extracting `run_node()` from `node_runner.cpp`; pulls in `NodeCacheKey` paths transitively through `node_runner.cpp`).
- **Documentation governance**: `docs/DOCUMENTATION_GOVERNANCE.md` §docs/tickets/TICKET-NNN.md + §FOLLOWUP_TICKETS + §Matrix-update ("Nuovo blocker verificato → Ticket + indice ticket + Current Status").
- **AGENTS.md**: §Post-push SHA-selfcheck invariant + Cat-3 minimal-surface + §honest-limitation + "Fare PR piccole e mirate" + SHA-cite inline-only rule.

## Periodicità

The ticket should remain OPEN with status `OPEN (rot CONTAINED)` until `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-FIX` lands + closes it via the standard Done flow per AGENTS.md (move row from §Open Blockers to §Recently Closed + mark ticket `DONE` + append CHANGELOG entry + cite the working-build-host SHA baseline).
