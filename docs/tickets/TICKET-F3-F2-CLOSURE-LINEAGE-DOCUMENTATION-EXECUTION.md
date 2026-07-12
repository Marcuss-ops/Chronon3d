# TICKET-F3-F2-CLOSURE-LINEAGE-DOCUMENTATION-EXECUTION

**Status**: ACTIVE (cat-3 docs-only chore registered this session 2026-07-12; the execution arm of the prior register-only chore `TICKET-F3-F2-CLOSURE-LINEAGE-DOCUMENTATION` per AGENTS.md "Fare PR piccole e mirate" + §honest-limitation fix-forward pattern; runs on working-build-host post-cleanup per AGENTS.md §Post-push SHA-selfcheck invariant gating).

**Context**: The prior `docs(closure): register F3 + F2 closure lineage documentation chore` (commit `a72e8053` this session) opened this EXECUTION ticket as its forward-point deliverable per Cat-5 4-doc same-commit alignment. The prior chore intentionally deferred the actual doc-EDIT bodies because: (i) recovery ref cleanup MUST complete before the audit-trail can be safely written (the 4-deep ref chain is the §honest-limitation safety-net for any lost-commit contingency during the rebase dance), (ii) the §Post-push SHA-triple selfcheck MUST PASS (the WRITE-side belt-and-suspenders discipline per AGENTS.md closure lineage) before any local historical refs are destroyed, (iii) the canonical docs/CHANGELOG.md + CURRENT_STATUS.md cite-only row + FOLLOWUP_TICKETS.md §Recently Closed migration all require the FINAL post-cleanup SHA which is non-determinable at register-time.

**Objective**: On working-build-host, after the 3-way SHA-triple selfcheck PASS per AGENTS.md §Post-push SHA-selfcheck invariant, execute the canonical 4-doc same-commit deliverables (mirroring the prior `TICKET-FFMPEG-REQUIRED-FAIL-LOUD` §Open Blocker entry template precedent) for the F3 + F2 closure lineages + concurrent-agent race cycle audit-trail per AGENTS.md §Documentazione-governance.md documentation-governance discipline + §honest-limitation pattern:

1. **PREPEND** `docs/CHANGELOG.md` NEW entry per **Cat-5 newer-at-top convention** documenting:
   - The audit-trail is the canonical machine-verifiable record of: (a) the F3 + F2 closure lineage cycles this session's wrap_push produced (true-divergence per `is-ancestor` check, collision-orchestra pattern), (b) the concurrent-agent race cycle observed via AGENTS.md §Post-push SHA-selfcheck invariant's 4-mode failure taxonomy in production-observational form (origin advanced `09cc64b2` → `c1a39583` mid-rebase-cycle), (c) the canonical pivot to FF-PIVOT-3 (rebase drain) per AGENTS.md §Per-branch-rebase invariant
   - The **DETERMINED** commit SHAs from THIS session's local-main at base-of-execution (machine-verifiable via `git log -n 10` BEFORE execution excerpt + during execution core.bare transact-time audit): commit `a72e8053` (this prior register-only chore, 67 bytes subject envelope per `wc -c` machine-verified), plus the 5-LOCAL_AHEAD-ancestors on current local main (machine-verified via `git log -n 5 --format='%h %s'` immediately before commit-write). The concurrent-agent landed SHAs are verifiable on `origin/main` via `git log origin/main --no-decorate -n 10` immediately before commit-write (final enumeration at execution-time per §honest-limitation fix-forward pattern — NO fabricated SHA citations at register-time).
   - The **`<post-cleanup-final-SHA>`** placeholder (NON-DETERMINED at register-time; to be filled at execution-time via `git rev-parse HEAD` AFTER the 3-way SHA-triple selfcheck PASS per AGENTS.md §Post-push SHA-selfcheck invariant)

2. **EDIT** `docs/FOLLOWUP_TICKETS.md` §Recently Closed: ADD new row(s) per **Cat-5 newer-at-top convention** covering the F2 + F3 + concurrent-agent-race lineages (or a single consolidated row per Cat-3 anti-duplication discipline). Template schema mirrors the canonical `TICKET-FFMPEG-REQUIRED-FAIL-LOUD` §Open Blocker row pattern: `| <ID> | DATE | STATUS | SHORT-VERDICT | LINK |` (NOT the false-precedent at commit `52e48ddd` — see §honesty fossil-fix below).

3. **EDIT** `docs/CURRENT_STATUS.md` Snapshot header (the > blockquote at top): ADD +1 1-line cite-only row per **Cat-3 anti-duplication discipline** (no re-summary; forward-point substance to the FOLLOWUP_TICKETS.md row in §Recently Closed as the single source of truth). Format: `> **<SHORT-CITE-PREFIX>**: <ID> closed via <ticket-link> at <date>. See [<doc-link>](<doc-link>) for canonical audit-trail.`

4. **RECOVERY REF CLEANUP** (gated on §Post-push SHA-triple PASS, NOT on this chore commit):
   - Verify SHA-triple equality: `LOCAL_SHA == POSTPUSH_SHA == UPSTREAM_SHA (@{u})` — per AGENTS.md §Post-push SHA-selfcheck invariant
   - Verify all 4 recovery refs are NOT_IN_HEAD AND NOT_IN_ORIGIN (i.e., they are obsolete local historical state, NOT orphans in canonical history) via `git merge-base --is-ancestor` for each ref (this verification guards against destructive deletion of an inadvertently-orphaned SHA chain)
   - Execute cleanup sequence per F2 lineage precedent ordering (branches first; tag last per §honest-limitation safety-pattern):
     ```bash
     # Branches first (3 branches)
     git branch -D main-pre-f3-rebase
     git branch -D main-pre-f3-followon
     git branch -D main-pre-f3-followon-followup

     # Tag last (1 tag, the most conservative act)
     git tag -d backup-pre-rebase-2026-07-12-d9b9a83
     ```

5. **VERIFY** the operations succeeded and the audit-trail is machine-verifiable:
   - `bash tools/check_doc_sync.sh` PASS (the canonical doc-sync regression-lock per AGENTS.md §Documentazione-governance.md)
   - `bash tools/check_no_changelog_conflict_markers.sh` PASS (conflict-marker invariant)
   - `bash tools/check_followup_duplicates.sh` may emit forward-point warnings per established pattern (the gate is FORWARD-POINTED to `TICKET-FOLLOWUP-TICKETS-DEDUP-PYTHON-EXECUTOR` per §honest-limitation — NOT a blocker for this chore)
   - `git tag -l` does NOT list `backup-pre-rebase-2026-07-12-d9b9a83` (post-cleanup cleanup verification)
   - `git branch -l | grep -E 'main-pre-f3-(rebase|followon)' | wc -l` returns 0 (post-cleanup cleanup verification)

**Pre-requisite**: working-build-host with `chronon3d_cli` binary emerged via canonical `cmake --build build/chronon/linux-content-dev --target chronon3d_cli` + canonical `output/text_video_acceptance/chronon_glow_final.mp4` produced via `tests/text/test_pipeline_parity_real.cpp::Fase 6 §5` (the precondition for `tools/wrap_push.sh` Step 4.5h `tools/check_video_completeness.sh` to PASS) + the canonical FF-pivot-3 dance-collaboration pattern complete (3-way SHA-triple selfcheck PASS) + `bash tools/wrap_push.sh origin main` exit 0.

**Cat-3 minimal-surface (this register-only commit)**: ZERO new SDK API surface, ZERO source-code modifications, ZERO tools/ new entry points, ZERO include/chronon3d/ modifications. Pure docs/+followup-tracking per AGENTS.md "Fare PR piccole e mirate" + Cat-3 minimal-surface discipline.

**Resolution criteria** (must all hold on closure):
1. working-build-host wrap_push completed (Step 5 of the canonical runbook) with 3-way SHA-triple selfcheck PASS
2. cleanup sequence executed per §Post-push SHA-selfcheck invariant gating (4-deep recovery ref chain deleted safely per the branches-first-then-tag-last ordering)
3. NEW `docs/CHANGELOG.md` entry prepended (Cat-5 newer-at-top convention) with F3 + F2 + concurrent-agent-race closure details + the canonical DETERMINED commit SHAs + the execution-time-verifiable post-cleanup-final-SHA placeholder
4. NEW `docs/FOLLOWUP_TICKETS.md` §Recently Closed row(s) added (newer-at-top convention) for the F2 + F3 + concurrent-agent-race closure lineages — using the canonical Cat-5 §Recently Closed template mirroring `TICKET-FFMPEG-REQUIRED-FAIL-LOUD` (NOT the false-precedent at `52e48ddd`)
5. NEW `docs/CURRENT_STATUS.md` cite-only row added (1-line, NO re-summary per Cat-3 anti-duplication)
6. `bash tools/check_doc_sync.sh` PASS post-doc-creates (regression-lock + the explicit machine-verifiable gate per `tools/check_doc_sync.sh` exit 0)
7. Recovery ref cleanup verification: 3 branches + 1 tag deleted; SHA-triple PASS verified mid-cleanup

## §honesty fossil-fix (52e48ddd precedent is NOT a §Recently Closed migration — learn forward)

Per AGENTS.md §honest-limitation fossil-fix pattern: **the user-supplied precedent at commit `52e48ddd` is NOT valid for §Recently Closed row mirroring**. Forensic audit (this session, 2026-07-12, machine-verified via `git log -n 1 --format='%H%n%s%n%b' 52e48ddd` + `git show --stat 52e48ddd`) reveals:

```
52e48dddb1a1687b52c08d3d67a1c48325e9365d
docs(F2): post-retry push verified + 4-doc sync (amend silent-mode-1 rescued)
---
 docs/CHANGELOG.md                          | 25 ++++++++++++++++++++++++-
 docs/FOLLOWUP_TICKETS.md                   |  4 +---
 docs/tickets/TICKET-INFRA-F2-DIVERGENCE.md |  6 ++++--
 tools/wrap_push.sh                         |  1 -
```

Commit `52e48ddd` was a **4-doc same-commit amend fix-forward** (single-parent per `git show --stat` report), NOT a §Recently Closed row migration. The `docs/FOLLOWUP_TICKETS.md` modification was 4 total lines (+3 inserted, -7 deleted = -4 net lines) — none of which constitute a §Recently Closed row addition; the modification was a doc-sync amendment to existing rows to inline-cite a recovered SHA per AGENTS.md §SHA-cite inline-only rule (per AGENTS.md `## Regole di lint documentale` discipline).

**Forward-point from this fossil-fix**: When opening future §Recently Closed migration rows, mirror the canonical `TICKET-FFMPEG-REQUIRED-FAIL-LOUD` §Open Blocker row template (`| <ID> | DATE | STATUS | SHORT-VERDICT | LINK |`) — this is the actually-canonical §Recently Closed row schema in the existing FOLLOWUP_TICKETS.md corpus. Do NOT cite `52e48ddd` as a §Recently Closed precedent; it is not one.

## Cross-references

- [`docs/tickets/TICKET-F3-F2-CLOSURE-LINEAGE-DOCUMENTATION.md`](F3-F2-CLOSURE-LINEAGE-DOCUMENTATION.md) — the prior register-only DOC chore ticket (commit `a72e8053` this session)
- `docs/CHANGELOG.md` (forward-pointed actual NEW entry site) — prepend entry per Cat-5 newer-at-top
- `docs/FOLLOWUP_TICKETS.md` §Recently Closed (forward-pointed actual row site) — append row per Cat-5 newer-at-top
- `docs/CURRENT_STATUS.md` Snapshot header (forward-pointed actual cite-only row site) — append 1-line cite-only row per Cat-3 anti-duplication
- AGENTS.md §Documentazione-governance.md (the documentation-governance discipline this chore adheres to)
- AGENTS.md §Honest-limitation pattern (FAIL-LOUD observability discipline — the discipline that surfaced the 52e48ddd fossil-fix in this ticket)
- AGENTS.md §Post-push SHA-selfcheck invariant (the WRITE-side belt-and-suspenders discipline gating the cleanup arm)
- AGENTS.md §Per-branch-rebase invariant (linear history lineage — the discipline that produced the FF-pivot-3 pivot in the cycle that motivated this chore)
- AGENTS.md §SHA-cite inline-only rule (from `## Regole di lint documentale` — applied to the CHANGELOG.md entry text)
- AGENTS.md TICKET-GATE-SUBJECT-RANGE closure (subject envelope audit pattern — applies to the execution commit subject)
- AGENTS.md §INFO-level diagnostic style rule (`[INFO] <gate-name>: ...` pattern — applies to gate emission summary in the execution-time verify step)
- The canonical `TICKET-FFMPEG-REQUIRED-FAIL-LOUD` §Open Blocker row template precedent (the actually-canonical §Recently Closed row schema mirror anchor)
- The canonical `TICKET-CHANGELOG-CONFLICT-CLEANUP` precedent (the FAIL-LOUD lint-gate pattern mirroring for the cleanup-verification step)
- The F2 closure lineage at parent commit lineage (TICKET-INFRA-F2-DIVERGENCE → TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX → TICKET-COMPLETENESS-GATE-V2-FIX-FORWARD → TICKET-CONCURRENT-AGENT-RACE-LIFECYCLE)
- The F3 closure lineage at parent commit `11f4fd03` (aggregator hardening landing) + `d9b9a832` (rot-source-resolved)
- The concurrent-agent race cycle observed this session: `09cc64b2` → `c1a39583` mid-rebase-cycle on the 4-LOCAL_AHEAD execution — the canonical evidence that motivates the AGENTS.md §Post-push SHA-selfcheck invariant forward-point
- This session's chore commit lineage (current local-main at base-of-execution, machine-verifiable via `git log -n 10`): commit `a72e8053` (the prior register-only chore) + 5-LOCAL_AHEAD-ancestors (top 5 SHAs in machine-verifiable `git log -n 5`)
- The execution-time DETERMINED-SHA set (final list machine-verifiable via `git log HEAD -n 10 --no-decorate` immediately before commit-write per the §honest-limitation fix-forward pattern)
- The post-cleanup-final-SHA (NON-DETERMINED at register-time; to be filled at execution-time per AGENTS.md §honest-limitation fix-forward pattern — use placeholder `<post-cleanup-final-SHA>` in this register-only commit, NEVER fabricated)

## Forward-points

- (a) The actual doc-EDIT bodies (NEW `docs/CHANGELOG.md` entry prepended + §Recently Closed row migration + `docs/CURRENT_STATUS.md` cite-only row + recovery ref cleanup) — all 5 deliverables above — to be executed on working-build-host post-cleanup per AGENTS.md §Post-push SHA-selfcheck invariant gating
- (b) Subject envelope verification for the execution commit deferred until commit-write time per `TICKET-F3-F2-CLOSURE-LINEAGE-DOCUMENTATION` deferral note (the actual subject line for the execution commit is verifiable at commit-time via `echo -n | wc -c`, NOT at register-time per §honest-limitation fix-forward pattern; write-time envelope target ≤ 72 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE closure)
- (c) The final SHA enumeration for the CHANGELOG audit-trail deferred until execution-time per §honest-limitation fix-forward pattern (cross-check with `git log -n 5 --format='%h %s' HEAD` + `git log origin/main --no-decorate -n 10` immediately before commit-write)
- (d) The §Recently Closed row content for the F2 + F3 + concurrent-agent-race closure lineages deferred until execution-time per Cat-5 4-doc same-commit alignment (concurrent with the CHANGELOG entry prepend + the CURRENT_STATUS cite-only row append)
- (e) Machine-verifiable `bash tools/check_doc_sync.sh` PASS post-doc-creates (the regression-lock gate — forward-pointed to the execution-time verify step)
- (f) The 52e48ddd fossil-fix documented above is a §honest-limitation precedent for future audit-trail tickets: when user-spec or chat history cites a "precedent commit" that does NOT actually match the claimed role, surface the discrepancy as a §honesty fossil-fix paragraph in the ticket rather than fabricating the precedent (this rule derives from the §honest-limitation FAIL-LOUD pattern documented in this very ticket)
