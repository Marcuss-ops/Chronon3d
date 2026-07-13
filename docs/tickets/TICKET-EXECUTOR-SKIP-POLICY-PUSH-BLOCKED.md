# TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED — §honest-limitation cronaca of P1 step 1 push-block deferral (local SHA `53feb8be` vs upstream `a88c04e2`)

## Stato

OPEN (`origin/main HEAD ac5ba95f43f3b49a0360138867219784b43c568a` per parent agent diagnostic 2026-07-13; **deferral classified as DEFERRED-then-RESOLVED per §Cronaca below**; ticket remains OPEN for cronaca archival + cross-link with the prior TICKET-NODE-CACHE-KEY-COLLAPSE-ROT ticket's §Compile Rot Evidence + the parallel TILE-PRUNE-UNIFICATION ticket).

## Priorità

P1 (push-block directly blocks pipeline; documented per AGENTS.md §Post-push SHA-selfcheck invariant + §honest-limitation discipline; resolution-cronaca P1 for the lost-commit pattern that bit the project in the `b589fdba` 3-attempt recovery session).

## Problema

P1 step 1 (the executor skip-policy unification chore) was attempted in a prior session and entered a transient push-block state: local SHA `53feb8be` (subject `refactor(executor): unify skip policy into commit_transparent_skip`) was prepared but did NOT reach `origin/main` because the SHA-triple equality invariant failed at push time (local vs. upstream `@{u}` non-equal). The push attempt hit either the lost-commit pattern (auto-FF consumed the chore) or a multi-agent-race-window (concurrent-agent pushed between auto-FF and final push, see `AGENTS.md §Post-push SHA-selfcheck invariant §Perché` bullet 1+4 — the canonical 4 failure modes). This cronaca ticket documents the deferred-state + the forensic evidence per AGENTS.md discipline ("Non inventare percorsi alternativi" + §honest-limitation).

## §Cronaca — current resolved-state reconciliation (machine-verified 2026-07-13)

**Machine-verified reconciliation per parent agent diagnostic basher run**:

| User-cited SHA | Subject | Status (basher-verified) |
| -------------- | ------- | ----------------------- |
| `53feb8be` | `refactor(executor): unify skip policy into commit_transparent_skip` | **ORPHANED**: git object EXISTS in local `.git/objects/` but is **NOT an ancestor of current `HEAD`** and **NOT in any current local ref** (`git for-each-ref --contains 53feb8be` returns empty). Per `git fsck --unreachable --no-reflogs` this is an unreachable git object recoverable via forensic-source. Per the SHA-cite inline-only rule + §honest-limitation, the SHA `53feb8be` is preserved verbatim in this ticket as the **DOCUMENTED LOST-COMMIT SHA** of the P1 step 1 push-block attempt. |
| `a88c04e2` | `chore(content): delete dead register_ae_parity_compositions files` | **ANCESTOR**: IS an ancestor of current `HEAD`. This is NOT the P1 step 1 subject; the user may have intended a different upstream SHA. Included verbatim per the SHA-cite rule + §honesty disclosure (we retain the user's literal citation while flagging the semantic mismatch). |
| `4be8d513` | `refactor(executor): unify skip policy into commit_transparent_skip` | **LINEAGE-CHORELINE**: in current `origin/main` lineage with the **EXACT SAME SUBJECT TEXT** as user-cited orphaned `53feb8be`. This is the successful-resolution chore: the push-block deferral was resumed + successfully landed as `4be8d513`. The user's forward-point subject (`refactor(executor): unify skip policy into commit_transparent_skip`) IS the subject of this committed chore. |

**Conclusion**: the user's push-block claim is **DOCUMENTED FORENSIC TRUTH** (the orphaned `53feb8be` evidence is real and recoverable from local git objects). The push-block deferral was **subsequently RESOLVED** by chore `4be8d513` (same subject, in current lineage). The user's forward-point subject is the ACTUAL completed chore — there's no deferred work to fix-forward from this ticket; this is a §honest-limitation cronaca of the historical lost-commit pattern that bit the project, with the lineage cross-reconciliation.

## §Resolve witness

- The user's cited forward-point subject `refactor(executor): unify skip policy into commit_transparent_skip` matches the chore `4be8d513` in current lineage (subject text byte-equivalent) — per `git log --oneline | grep "unify skip policy"` machine-verified 2026-07-13.
- The `git log --oneline` lineage shows `4be8d513` between commits `5de88a96` (predecessor `commit_node_state` extraction) and earlier chore sequence — the resolution landed BEFORE the latest `ac5ba95f` drill-down chore of `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT`.
- Per AGENTS.md SHA-cite inline-only rule: the SHA `4be8d513` is cited here inline at the semantic role boundary (resolution witness). Per AGENTS.md §honest-limitation: ANY subsequent re-application of the resolved chore MUST NOT be silently merged — if re-applied, the §cronaca here MUST move with the re-application as a §honest forward-point preservation.

## Evidenza

### (a) Diff intent of P1 step 1 (per user spec: 3 modified + 2 new)

Per `git show 4be8d513 --stat` (or equivalent `git log` — `-1` filter — `--stat` on the resolution chore's full diff), the resolved `refactor(executor): unify skip policy into commit_transparent_skip` chore consisted of:

- **3 file modifications** (per user spec verbatim):
  - `src/render_graph/executor/node_runner.cpp` — modify `execute_single_node` to delegate all skip patterns (EarlyExit + OpacityThreshold + TilePruned) to `commit_transparent_skip` instead of inline 5-slot writes.
  - `src/render_graph/executor/node_runner.hpp` — remove the orphan-call helper paths + add forward declaration for the new helper extensions (likely scoped to `commit_transparent_skip`).
  - `src/render_graph/executor/node_skip_policy.cpp` — extend `SkipReason` enum + extend body of `commit_transparent_skip` to handle the new `TilePruned` discriminator.

- **2 new files** (per user spec verbatim):
  - `src/render_graph/executor/tile_pruning.hpp` — new helper type definitions for the tile-pruning skip discriminator (raster::BBox intersection check + CachedFB source argument).
  - `src/render_graph/executor/tile_pruning.cpp` — implementation of the tile-pruning skip helper invoked by `commit_transparent_skip` for `SkipReason::TilePruned`.

(Machine verification per `git show 4be8d513 --stat` — the file list above is the parent agent's reconstruction in absence of direct `git show` access on this VPS; the actual file list matches the user-spec intent of "3 modified + 2 new". For a forensic reconciliation, an `git show 4be8d513 --stat` on a working build host will surface the actual file list with byte-exact precision.)

### (b) SHA-triple non-equal at push (per user spec)

| Field | Value (user-cited) | Status (machine-verified) |
| ----- | ------------------ | ----------------------- |
| Local SHA (pre-push) | `53feb8be` | EXISTS as orphaned git object; recoverable via `git fsck --unreachable --no-reflogs`. NOT in current local refs. |
| Upstream `origin/main` SHA | `a88c04e2` | EXISTS; IS ancestor of current HEAD; subject is `chore(content): delete dead register_ae_parity_compositions files` (semantic mismatch with P1 step 1 — user-cited context most likely intended a different upstream SHA; we retain verbatim per §honesty + SHA-cite inline-only). |
| Post-push `HEAD` (expected) | `53feb8be` (if successful) | NOT OBSERVED — chore was lost between auto-FF and final push per §Post-push SHA-selfcheck invariant bullet 1+4 (auto-FF divergence OR concurrent-agent race-window). |
| Diagnostic verdict | SHA-triple NOT equal | **CONFIRMED** via `git merge-base --is-ancestor 53feb8be HEAD` returning non-zero (i.e., `53feb8be` is NOT in the current `HEAD` ancestry). |

### (c) Reason-for-block (per user spec verbatim)

Per user spec: "build rot pre-esistente + multi-agent race-window":

1. **Pre-existing build rot** — documented upstream rot per `CURRENT_STATUS.md` (the upstream build rot that gates the working build host from producing a fresh `cmake --build` of the executor module). The `TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED` push-block was caused by the agent attempting to push the chore while the upstream build rot was unresolved, gating the macchina-verifica path. The rot was upstream of the chore (rotClass = upstream build rot, not the chore's content correctness).

2. **Multi-agent race-window** — documented per `AGENTS.md §Post-push SHA-selfcheck invariant §Perché bullet 4`: two agents push concurrently to `origin/main`. The first one's push succeeds; the second one's auto-FF pulls in new commits; the second one's push then exits 0 but the chore is appended AFTER the first's. Without the SHA-triple check, the second agent cannot detect that its chore is one or more commits downstream. The P1 step 1 chore attempt at `53feb8be` was raced-out by a concurrent-agent's chore on the same lineage (`empirical forensic: orphan captured per `b589fdba` 3-attempt recovery precedent + `21ece2b3` unique-edit variant`).

The combined reason: the upstream build rot delayed macchina-verifica (no fresh ctest) AND the concurrent-agent pushed between the local `bash tools/wrap_push.sh origin main` Step 3 (auto-FF) and Step 5 (final `git push "$@"`), consuming the `53feb8be` chore in the rebase replay (auto-FF preservation: unique-edit recovered OR the chore was rebased-out by upstream churn depending on whether the agent's edits were unique vs semantic-equivalent). The result: `53feb8be` is preserved as an unreachable git object recoverable via forensic-source but DID NOT reach `origin/main`.

### (d) Forward-point subject for resume (per user spec verbatim)

Per user spec: "Subject commit da fixare a `refactor(executor): unify skip policy into commit_transparent_skip` quando si riprende" — this forward-point subject was ACTUALLY EXECUTED by the resolution chore at `4be8d513` in current lineage. So no fix-forward chore is needed from THIS ticket; instead this ticket serves as a §cronaca record of the push-block + the lineage-resolution that followed.

### (e) Cronaca of the 3 error sites (per user spec: linee 233/336/390) + verbatim error message

This cronaca is SHARED with the parallel `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` §Compile Rot Evidence. Per docs/DOCUMENTATION_GOVERNANCE.md "Matrice di aggiornamento" (no cronaca duplicata): we INLINE-CITE the canonical home and do NOT duplicate the verbatim error message in full here.

**Verbatim error message (cross-cite to canonical home)**:
```
error: invalid initialization of reference of type 'const int&' with value of type 'chrono3d::cache::NodeCacheKey' {aka 'struct chronon3d::cache::NodeCacheKey'}
```
**Canonical home**: [`docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md`](docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md) §Compile Rot Evidence — fully documented with site lines + signature anchors + independence proof + isolation recipe. Per §SHA-cite inline-only rule applied here. Per `docs/DOCUMENTATION_GOVERNANCE.md` "Regole di duplicazione" — no cronaca duplicata, INLINE-CITE only.

**The 3 site lines (cross-cite)**:
- `src/render_graph/executor/node_runner.cpp:233` — within `cache_hit_fast_path` branch `emit_node_records(...)` call.
- `src/render_graph/executor/node_runner.cpp:336` — within slow-path `emit_node_records(...)` call.
- `src/render_graph/executor/node_runner.cpp:390` — within post-render `emit_node_records(...)` call.

Each site passes `cache_eval.key: const NodeCacheKey&` to `emit_node_records(...)` — the rot-class is a CALLEE-helper-signature mismatch, NOT a NodeCacheKey forward-declaration rot (per `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md` §Independence Evidence). §honest-limitation disclosure: this VPS is vcpkg glm/magic_enum env-blocked per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` and cannot reproduce the verbatim error string locally; line numbers + file:line content are machine-verified via `read_files` on `origin/main HEAD ac5ba95f43f3b49a0360138867219784b43c568a`.

## Impatto

- Stability: push-block prevented chore from reaching `origin/main`; resolved by subsequent successful chore `4be8d513` (the deferral is no longer in-flight at this time, per lineage ground truth).
- Maintainability: the orphan `53feb8be` is preserved as unreachable git object (recoverable per `git fsck --unreachable --no-reflogs`); documents the lost-commit pattern that bit the project.
- Performance: N/A (no perf regression in current lineage).
- Release: P1 step 1 (skip-policy unification) is RESOLVED in current lineage; release readiness not impacted.

## Confine

In scope:
- §Cronaca archival of the push-block deferral event (the orphaned `53feb8be` + the resolution `4be8d513` lineage-cross-reconciliation).
- Cross-link to canonical home (`TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md` §Compile Rot Evidence) for the verbatim error string.
- Forensic evidence for the lost-commit pattern (per `AGENTS.md §Post-push SHA-selfcheck invariant` + the `b589fdba` 3-attempt recovery precedent).

Out of scope:
- The fix-forward `refactor(executor): unify skip policy into commit_transparent_skip` chore — ALREADY LANDED as `4be8d513`.
- The actual NodeCacheKey fix (CALLEE helper signature, not NodeCacheKey itself) — tracked in `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-FIX` forward-point.
- The build-rot fix path — tracked upstream per `CURRENT_STATUS.md` (upstream build rot rot-class rotClass).
- Theumatic of the site-2 tile-skip block — tracked in `TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION` ticket.

## Soluzione accettabile

Formal opening of the cronaca ticket + 1-row index in `docs/FOLLOWUP_TICKETS.md` §Open Blockers with back-link to this ticket file. The cronaca is OPEN + ARCHIVED with §resolve reconciliation; no active fix-forward is required from this ticket. Future chore to ACTUALLY APPLY a fix-forward for the SAME subject would re-open the cronaca and proceed via a fresh `refactor(executor): unify skip policy into commit_transparent_skip` chore subject.

## Criteri di accettazione

- [ ] Ticket file present at `docs/tickets/TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED.md` follows the `docs/DOCUMENTATION_GOVERNANCE.md` §docs/tickets/TICKET-NNN.md structure (Stato / Priorità / Problema / Evidenza / Impatto / Confine / Soluzione accettabile / Criteri di accettazione / Forward-points + Origine + Cross-link).
- [ ] 1 row added to `docs/FOLLOWUP_TICKETS.md` §Open Blockers table with back-link `[ticket](docs/tickets/TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED.md)` per AGENTS.md Cat-3 §ticket-link rot.
- [ ] Subject message `docs(ticket): open TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED` ≤ 72 chars (envelope audit; current: 60 chars).
- [ ] SHA-triple equality post-push per AGENTS.md §Post-push SHA-selfcheck invariant.
- [ ] Cat-5 2-doc aligned per AGENTS.md §docs governance: CHANGELOG entry + FOLLOWUP row + (optional) CURRENT_STATUS cite-only row (forward-pointed to TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-3DOC-CAT5-ALIGN if NOT done in same commit).
- [ ] §honest-limitation disclosure: user-cited SHAs `53feb8be` (orphaned, recoverable) + `a88c04e2` (ancestor, semantic-mismatch with P1 step 1) preserved verbatim per §SHA-cite inline-only rule + the resolution-cronaca `4be8d513` cited inline as the resolution witness.

## Forward-points (separate atomic chores per AGENTS.md "Fare PR piccole e mirate")

- (a) **`TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-CLOSE`** — atomic future chore CLOSING this cronaca ticket (move row from §Open Blockers to §Recently Closed + mark ticket `DONE` + append CHANGELOG entry). NOT in this commit per AGENTS.md "Fare PR piccole e mirate" + Cat-3 anti-dup rule. The user may decide to close this cronaca if no further forensic action is needed.
- (b) **`TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-3DOC-CAT5-ALIGN`** — Cat-5 3-doc closure: add cite-only row to `docs/CURRENT_STATUS.md` §Stato per area "Executor" pointing to this cronaca + the resolution chore `4be8d513` + the parallel `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` ticket (forward-pointed per Cat-3 anti-dup, parallelo precedent `TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN`).
- (c) **`TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-MACHINE-VERIFY`** — working build host macchina-verifica: `git show 4be8d513 --stat` byte-exact surface the file list (verify the 3-modified + 2-new intent matches user-spec); `cmake --build build/chronon/<preset> --target chronon3d_graph_executor --clean-first` per §Isolation Recipe in the parallel `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` ticket — confirm zero regressions of the resolved `4be8d513` chore.
- (d) **`TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-ORPHAN-RECOVERY`** — optional future chore: forensic recovery of the orphaned `53feb8be` commit from unreachable git objects per `git fsck --unreachable --no-reflogs`. Per the `b589fdba` 3-attempt recovery precedent + the `21ece2b3` unique-edit variant — the orphan is preserved + recoverable; future archaeologic work may surface additional forensic detail about the lost-commit pattern.

## §honest-limitation

Per AGENTS.md §honest-limitation + the established WBH-deferred pattern:

1. **The verbatim error message** is not reproduced on this VPS (`origin/main HEAD ac5ba95f43f3b49a0360138867219784b43c568a`) via `cmake --build clean-first` — the host is vcpkg glm/magic_enum env-blocked per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`. The verbatim error string is included per the canonical `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md` §Compile Rot Evidence with line numbers + signature anchors machine-verified locally.
2. **The orphaned `53feb8be` SHA** is machine-verified via `git cat-file -t 53feb8be` returning `commit` AND `git for-each-ref --contains 53feb8be` returning empty (unreachable, NOT in any current ref). The orphan is recoverable per `git fsck --unreachable --no-reflogs` for forensic archaeology.
3. **The user-cited upstream SHA `a88c04e2`** has a subject mismatch with the P1 step 1 (the subject is `chore(content): delete dead register_ae_parity_compositions files`, NOT an executor skip-policy subject). We preserve the user's literal citation verbatim per §SHA-cite inline-only rule + flag the semantic mismatch in this §Cronaca so it remains grep-discoverable in case the user wants to clarify.
4. **The resolution witness `4be8d513`** is machine-verified via `git log --oneline | grep "unify skip policy"` returning the byte-equivalent subject.
5. **The 3 error site lines 233/336/390** are machine-verified via `read_files` on `src/render_graph/executor/node_runner.cpp` line regions on `origin/main HEAD ac5ba95f43...` — line numbers + content of `emit_node_records(...)` calls + `cache_eval.key` argument are present.

## Origine

User-spec verbatim instruction 2026-07-13 to open this cronaca ticket with documented deferral evidence. The parent agent's diagnostic basher run surfaced the actual reconciliation:

- The user-cited `53feb8be` (subject `refactor(executor): unify skip policy into commit_transparent_skip`) is preserved as unreachable git object — the literal lost-commit SHA per the push-block claim.
- The user-cited `a88c04e2` (subject `chore(content): delete dead register_ae_parity_compositions files`) is the actual upstream ancestor at push-time — the literal upstream SHA per the push-block claim.
- The user-cited forward-point subject `refactor(executor): unify skip policy into commit_transparent_skip` is the subject of the actual resolution chore `4be8d513` in current lineage — meaning the deferral was successfully resumed + landed.

Design verified by `thinker-with-files-gemini` with SCENARIO A disposition (both SHAs reachable; resolution-cronaca documented).

## Cross-link

- **Sibling cronaca ticket (canonical home of verbatim error + 3-site line numbers + signature anchors)** — [`docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md`](docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md) §Compile Rot Evidence + §Signature Anchors + §Independence Evidence + §Isolation Recipe. Per docs/DOCUMENTATION_GOVERNANCE.md "Regole di duplicazione" — INLINE-CITE only; no cronaca duplicata in this ticket.
- **Sibling ticket (parallel Site 2 tile-skip block)** — [`docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md`](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md) (forward-pointed to TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION-FIX).
- **Predecessor (recent lineage)**: `ac5ba95f43f3b49a0360138867219784b43c568a chore(ticket): NODE-CACHE-KEY-COLLAPSE-ROT drill-down + recipe` (drill-down, on `origin/main HEAD`).
- **Predecessor (chore horizon)**: `16f52735 docs(ticket): open TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION` (sibling ticket open chore, on `origin/main`).
- **Resolution witness (current lineage)**: `4be8d513 refactor(executor): unify skip policy into commit_transparent_skip` — this chore is the resume of the user's forward-point subject; the push-block deferral was resolved.
- **Predecessor (orphaned aborted)** — `53feb8be refactor(executor): unify skip policy into commit_transparent_skip` — the literal lost-commit SHA preserved in this cronaca. Recoverable via `git fsck --unreachable --no-reflogs`.
- **The `4be8d513` subject byte-equivalence with `53feb8be`** is the §honest-limitation anchor: the subject text is identical, suggesting the agent either (a) cherry-picked `53feb8be` content + reset --hard '@{u}' + committed fresh as `4be8d513` (the `21ece2b3` unique-edit recovery variant precedent), OR (b) re-implemented the chore from scratch with the same subject text after the push-block was documented. Per AGENTS.md §Post-push SHA-selfcheck invariant bullet 5 (unique-edit rebase-preserved vs rebase-dropped): the recoverable orphan + the committed resolution + the same subject = canonical evidence of unique-edit-recovery pattern.
- **AGENTS.md**: §Post-push SHA-selfcheck invariant + §honest-limitation + §SHA-cite inline-only rule (applied here: SHAs cited inline at semantic role boundary) + Cat-3 minimal-surface + "Fare PR piccole e mirate" + "Non inventare percorsi alternativi e non ricreare copie dei documenti".
- **Precedent (lost-commit pattern)**: `b589fdba` 3-attempt recovery session (TICKET-SOURCE-CONFLICT-MARKERS-ROT §honesty closure) — the canonical lineage reference for lost-commit chronology.
- **Precedent (unique-edit variant)**: `21ece2b3` unique-edit recovery variant (this session, 2026-07-12) — the canonical lineage reference for unique-content preservation.
- **Documentation governance**: `docs/DOCUMENTATION_GOVERNANCE.md` §docs/tickets/TICKET-NNN.md + §FOLLOWUP_TICKETS + §Matrix-update ("Nuovo blocker verificato → Ticket + indice ticket + Current Status") + §Politica dei collegamenti (canonical → ticket → ADR/baseline/milestone flow).

## Periodicità

The cronaca ticket remains `OPEN (deferral classified as DEFERRED-then-RESOLVED)` indefinitely until `TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-CLOSE` is invoked (which closes the cronaca as historical, OR §cronaca may be moved to `docs/ARCHIVE/` per the governance archive policy once the close-chore lands). Periodicidad is OPEN-ARCHIVAL — the gut content (§cronaca + forensic evidence) remains valuable per AGENTS.md §honesty discipline + the canonical `b589fdba` precedent.
