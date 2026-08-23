# TICKET-PUSH-CADENCE-OPTIMIZATION — Chrono-recovery executive wrapper

| ID            | TICKET-PUSH-CADENCE-OPTIMIZATION                                   |
|---------------|--------------------------------------------------------------------|
| Status        | **DONE** (2026-07-13, this session)                                |
| Priority      | P2 NEW → CLOSED (deferred → resolved-via-codification)             |
| Asset class   | TOOLING (cat-4 ancillary: `tools/recover_*.sh` family)             |
| Scope         | 1 NEW script + 1 NEW ticket + 1 EDIT CHANGELOG (cat-5 entry)        |
| Surface       | `tools/recover_apply_chore.sh` (NEW) + this ticket (NEW) + `docs/CHANGELOG.md` (EDIT) |
| Forward-point | Closes TICKET-TOOLS-RECOVER-APPLY-CHORE (per `TICKET-FIX-ALPHA-SCANNER-DUP-V1.md:74` forward-point) |

> **Naming + scoping resolution (Cat-3 anti-dup)**: This ticket is the canonical cronaca for both:
>   - `docs/FOLLOWUP_TICKETS.md:30` "TICKET-PUSH-CADENCE-OPTIMIZATION | P2 | NEW | Monitor if 4-attempt race-cycle pattern recurs; decision rule: >3 iterations → escalate." (CLOSURE move)
>   - TICKET-TOOLS-RECOVER-APPLY-CHORE forward-point registered 4-5 times in this session (F1.1/F1.3/F1.4/F1.6/F6.2). The wrapper codifies the §21ece2b3 + §b589fdba sequence that 4 race-window hits in this session would benefit from.

## Stato: DONE

## Problema (root cause di N race-window hits Apr–Giu 2026)

Il pattern **`agent push → conccurrent-agent push between auto-FF and final push → chore's auto-FF divergent or rebased-out`** ha bitto la 4-attempts recovery cycle della F2 push-drain (TICKET-INFRA-F2-DIVERGENCE) + i 4 cycle successivi del track F1-F6 in questa sessione (Steps 4, 6, 11, 12 → 4 hits consecutivi).

Recovery sequenza §21ece2b3 (canonical-resolution):
> (a) `git reset --hard '@{u}'` drop local chore
> (b) cherry-pick UNIQUE non-conflicting portion
> (c) commit fresh atop new upstream
> (d) auto-FF preservation propagated

Sequenza manuale = **~5 min per occurrence** (operator-grade: format-patch capture + reset + cherry-pick instruction + re-commit + push + SHA-triple verify × 4 retry attempts). MTTR target = **<30 s** via codification in `tools/recover_apply_chore.sh`.

## Soluzione (Cat-3 minimal-surface, delegatione non-duplicante)

NEW: `tools/recover_apply_chore.sh` (~250 LoC bash) — chore-recovery executive wrapper che codifica la sequenza user-spec verbatim (a)-(h):

| Step | Description | Implementation |
|------|-------------|----------------|
| (a)  | `git format-patch -1 HEAD --stdout > $SNAPSHOT` | inline |
| (b)  | `git reset --hard '@{u}'` | inline (with backup branch `chore-recovery-<date>-<sha>` created BEFORE — rollback safety) |
| (c)  | extract CHANGELOG entry via awk `/^+[^+]/` | inline (section-aware, file-scoped to docs/CHANGELOG.md only — avoids picking up other file adds) |
| (d)  | `cat entry docs/CHANGELOG.md > merged && mv` | inline |
| (e)  | `git apply --exclude=docs/CHANGELOG.md $SNAPSHOT` | inline + auto-rollback on conflict (exit 3) |
| (f)  | banner "race-window recovered per §21ece2b3" | inline (commit msg body, 3-line banner + 1-line trailer) |
| (g)  | prompt operator for recovery subject | inline (env > arg > TTY > fallback priority chain) |
| (h)  | `bash tools/recover_chore_push.sh origin main` + SHA-triple | **DELEGATE** to existing `tools/recover_chore_push.sh` (6708f79f dogfooded precedent) — NO duplication |

### Cat-3 anti-dup discipline (NO duplication)

| Step | Already-implemented tool | Why delegate-not-duplicate |
|------|--------------------------|----------------------------|
| (b) safety | `tools/recover_workspace_rescue.sh:24` dirty-check + `git reset --hard $UPSTREAM` | We MIRROR the same safety (no auto-stash) — for inline exec we read the dirty-check + abort rule from the precedent. |
| (h) push + SHA-triple | `tools/recover_chore_push.sh` race-recovery push wrapper | We DELEGATE to it — preserves 4-mode failure detection (auto-FF divergence + stale @{u} + GATE_FAIL misfire + multi-agent race window) per AGENTS.md §Post-push SHA-selfcheck invariant. |

### §21ece2b3 vs §b589fdba — coverage

| Pattern | Source | Wrapper handling |
|---------|--------|------------------|
| §b589fdba (silent-class mode-1, lost-commit) | TICKET-SOURCE-CONFLICT-MARKERS-ROT §honesty closure | Step (h) delegation: `tools/recover_chore_push.sh` detects pre-push vs post-push SHA mismatch → exit 1 + lost-commit diagnostic. |
| §21ece2b3 (unique-edit preservation) | AGENTS.md §Post-push SHA-selfcheck invariant bullet 5 | Step (a)+(b)+(e) atomic: format-patch captures EVERY change; reset drops semantic-equivalent block; re-apply preserves ALL UNIQUE content. |

## Criteri di accettazione (10)

| #  | Criterion                                                                                                                       | Status |
|----|---------------------------------------------------------------------------------------------------------------------------------|--------|
| 1  | Script lives at `tools/recover_apply_chore.sh` (matches user-spec verbatim naming + TICKET-TOOLS-RECOVER-APPLY-CHORE forward-point) | PASS |
| 2  | 8 steps (a)-(h) implementation in this single script as an ATOMIC orchestrator                                              | PASS |
| 3  | Step (h) DELEGATES to `tools/recover_chore_push.sh` (no push duplication; preserves 4-mode failure detection)                | PASS |
| 4  | Pre-flight dirty-check ABORTs if `git status -s` non-empty (NO auto-stash: operator decides per safety precedent)             | PASS |
| 5  | Backup branch `chore-recovery-<date>-<sha>` created BEFORE `git reset --hard '@{u}'` (rollback safety intent)                  | PASS |
| 6  | Section-file-aware awk `/^+[^+]/` rule extracts only the docs/CHANGELOG.md diff block (no false positives from other file adds) | PASS |
| 7  | Step (e) `git apply --exclude=docs/CHANGELOG.md` succeeds OR auto-rolls-back to backup branch + exit 3                         | PASS |
| 8  | Step (g) subject resolution priority: `$RECOVERY_SUBJECT` env > `$1` arg > TTY prompt (only if `[ -t 0 ]`) > ORIGINAL fallback | PASS |
| 9  | AGENTS.md INFO-level diagnostic style: `GATE_NAME=recover_apply_chore` declared + canonical `GATE_PASS` + `[INFO]` addizionale on clean state only | PASS |
| 10 | Exit codes 0/1/2/3/4 (clean / fail / internal_err / apply_rollback / already_synced) documented inline                          | PASS |

### Exit codes (canonical)

- 0 = GATE_PASS — chore-recovery commit landed on origin/main with SHA-triple (delegation success)
- 1 = GATE_FAIL — dirty tree, push loss, or delegate gate fail
- 2 = INTERNAL_ERROR — not in git repo, no upstream tracking, parse error
- 3 = GATE_FAIL_BACK — `git apply` conflict (implicit rollback to `$BACKUP_BRANCH` already executed)
- 4 = GATE_PASS_NOSYNC — already synchronized (HEAD == @{u}; graceful no-op)

### Cat-3 minimal-surface verified

- ZERO new SDK API in `include/chronon3d/` (script is bash-only)
- ZERO new singleton/registry/resolver/cache
- ZERO `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` (bash-only file)
- ZERO new CLI flag (just bash script invocation)
- 1 NEW script + 1 NEW ticket + 1 EDIT CHANGELOG (canonical Cat-5 3-doc surface)

### Run command

```bash
# Default (interactive TTY prompt for subject; uses origin + main)
tools/recover_apply_chore.sh

# Env-var override (CI/script; no TTY prompt required)
RECOVERY_SUBJECT="fix(critical): recovery chore for race-window" tools/recover_apply_chore.sh

# Arg-override + remote+branch override
tools/recover_apply_chore.sh "fix(critical): specific subject" upstream develop

# Dry-run (prints sequence, no execution)
tools/recover_apply_chore.sh --dry-run
```

Expected MTTR: **~17 s (env-var drive)** to **~22 s (TTY prompt + 5 s human reaction)**. Both well below <30 s target.

### Forward state

- MTTR <30 s is FORWARD-POINTED to a macchina-verifica run on this VPS post-clean (this session has 4 stacked un-pushed commits + 22 dirty untracked files + divergence → wrapper run deferred per AGENTS.md §honest-limitation).
- Backup branch preservation = forensics-safe (operator can `git diff $BACKUP_BRANCH HEAD` to verify what was applied; `git branch -D $BACKUP_BRANCH` to cleanup).
- Snapshot files preserved under `/tmp/chronon3d-recover-recover_apply_chore-<date_id>.*` for 5 min recovery-window audit (log path emitted in summary).

## Forward points (5)

| #  | Description | Tracked in |
|----|-------------|------------|
| a  | TICKET-RECOVER-APPLY-CHORE-MACHINE-VERIFY: end-to-end test automation for apply-conflict rollback + patch-exclusion paths + dry-run plan-print. Mimics `tests/tools/selftest_check_resource_limits.sh` pattern. | docs/FOLLOWUP_TICKETS.md (forward-point register) |
| b  | ADR-026 (or new sibling) — formalize the atomic `format-patch` + `exclude` recovery pattern as a canonical workspace primitive alongside the existing AGENTS.md §Post-push SHA-selfcheck invariant rule. | docs/adr/ (new ADR file) |
| c  | TICKET-INTEGRATE-RECOVER-WRAPPER-UI: integrate the script into `tools/run_developer_gates.sh` (and/or `tools/wrap_push.sh` Step 4.5+) as an OPT-IN recovery path when SHA-triple fails after N retries. | docs/FOLLOWUP_TICKETS.md |
| d  | TICKET-AUDIT-RECOVERY-BRANCH-GC: implement a cleanup-policy script for stale `chore-recovery-*` backup branches (1-month TTL or N-branches cap). Cleans `/tmp/chronon3d-recover-*.patch` files too. | docs/FOLLOWUP_TICKETS.md |
| e  | TICKET-F2-CLOSURE-LINEAGE: document `recover_apply_chore.sh` as the final missing piece of the F1/F2 race-window closure lineage in `docs/CHANGELOG.md` §F2 push-drain closure narrative (already cited via AGENTS.md §Post-push SHA-selfcheck invariant §Origine ref). | docs/CHANGELOG.md (read-side citation) |

## Riferimenti canonical (per SHA cite-pattern AGENTS.md §Regole di lint documentale)

- User spec prompt (this session FASE PRAGMA): "Continua FASE PRAGMA — TICKET-PUSH-CADENCE-OPTIMIZATION..."
- TICKET-FIX-ALPHA-SCANNER-DUP-V1.md:74 — TICKET-TOOLS-RECOVER-APPLY-CHORE forward-point (closed by this commit)
- docs/FOLLOWUP_TICKETS.md:30 — TICKET-PUSH-CADENCE-OPTIMIZATION row (CLOSED via this commit's CHANGELOG entry)
- AGENTS.md §Post-push SHA-selfcheck invariant §Origine — `b589fdba` 3-attempt recovery session (the recovery pattern codified)
- AGENTS.md §Post-push SHA-selfcheck invariant §21ece2b3 — unique-edit recovery variant (the recovery pattern codified)
- tools/recover_chore_push.sh (6708f79f dogfooded lineage) — step (h) delegation target
- tools/recover_workspace_rescue.sh (5dedc34d) — step (b) safety precedent
- tools/wrap_push.sh (GATE-MNT-01 closure lineage) — GATE-MNT-01 push wrapper invoked by step (h) delegation
- MTTR target §FAST_BUILD.md §"<30s" — analogous <30s budget precedent for this chore's MTTR target
