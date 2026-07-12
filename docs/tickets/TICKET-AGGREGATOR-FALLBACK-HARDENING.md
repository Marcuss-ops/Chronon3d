# TICKET-AGGREGATOR-FALLBACK-HARDENING

**ID**: `TICKET-AGGREGATOR-FALLBACK-HARDENING`
**Priorità**: P1
**Area**: aggregator hardening / telemetry fail-loud contract
**Parent**: 2026-07-12 hardening follow-on chore landed as `11f4fd03` on top of F2 closure commit `52e48ddd` (post-rebase replay on `3d02d91a` per AGENTS.md linear-history invariant)

## Stato

**HARNESS-COMPLETE** (this commit, 2026-07-12, atomic chore commit on `main`).
**production-data macchina-verifica**: DEFERRED to working build host per AGENTS.md §honest-limitation
(vcpkg glm/magic_enum + tmpfs env on this VPS, the underlying sqlite + selftest log
queries still return valid empty results rather than triggering the new exit-2 paths on this VPS).

## Soluzione

7 hardening edits applied atomically to `tools/run_weekly_scorecard.sh` per user spec verbatim
"harden the aggregator silent fallbacks in a follow-up commit (separate from Test 18 cycle)":

| # | Metric / Pattern | Edit | Line before | Line after impact |
|---|------------------|------|-------------|-------------------|
| 1 | Metric 3 (manual_touches_per_video DB part) | `\|\| echo 0` → `\|\| { echo GATE_FAIL_INTERNAL: ... sqlite SUM(manual_touches_per_video) failed >&2; exit 2; }` | sqlite3 cmd | fail-loud on sqlite read failure, exit 2 instead of silent 0 |
| 2 | Metric 4 (cost_per_finished_minute DB) | `\|\| echo 0` → `\|\| { echo GATE_FAIL_INTERNAL: ... sqlite SUM(render_ms) failed >&2; exit 2; }` | sqlite3 cmd | fail-loud on sqlite read failure, exit 2 |
| 3 | Metric 7 (deterministic_hash_failures) | `grep ... \| wc -l \|\| echo 0` → `awk '/GATE_FAIL/ {c++} END {print c+0}' ...log \|\| { echo GATE_FAIL_INTERNAL: ... selftest log grep failed (real IO error, not no-match) >&2; exit 2; }` | selftest log loop | awk `END{print c+0}` always emits 0 on no-match; the `\|\| exit 2` fires only on real IO error |
| 4 | Metric 8 (bbox_contract_violations) | `\|\| echo 0` → `\|\| { echo GATE_FAIL_INTERNAL: ... sqlite SUM(text_bbox_contract_violations) failed >&2; exit 2; }` | sqlite3 cmd | fail-loud on sqlite read failure, exit 2 |
| 5 | failure_rate TOTAL=0 path | `FAILURE_RATE="0.00%"` (always-compute, fabricated baseline) → `FAILURE_RATE="[NO-DATA]"` initial + only-compute when `$TOTAL -gt 0` | line 71-77 | honest `[NO-DATA]` placeholder replaces fabricated 0.00% silent fallback |
| 6 | PEAK_MEMORY_BYTES (Metric 6) dead-code guard | removed `[ -z "$PEAK_MEMORY_BYTES" ] && PEAK_MEMORY_BYTES=0` line entirely (prior comment claimed removal but actual code STILL carried it — §honesty fossil resolved) | line 124 deleted | empty peak_bytes propagates to awk (emits "0.0 MB" per awk's empty-string-to-zero arithmetic), NOT a silent fallback |
| 7 | date -v-7d BSD fallback | `\|\| echo '1970-01-01T00:00:00Z'` (sentinel literal) → `\|\| { echo GATE_FAIL_INTERNAL: ... date past-7d parser unrecognized (no 'date -d' nor 'date -v') >&2; exit 2; }` on BOTH `SEVEN_DAYS_AGO` AND `WEEK_START_DATE` chains | lines 41-46 | exit 2 when NO date-parser recognizes the past-7d syntax (replaces silent sentinel literal) |
| 8 | JSONL counter (Metric 3 source-A) | `grep -c '"event":'` → error-aware `set +e` + `grep -c '^{'` with explicit GREP_RC capture + `exit 2` if GREP_RC > 1 (real IO error, NOT no-match) | line 82 | replaces fragile `\|\| true` mask that silently swallowed both legitimate no-match AND real IO errors |

Subject envelope = 67 chars ≤ 72 (cat-2 push-range audit per TICKET-GATE-SUBJECT-RANGE closure).

## Confine

The FAIL-LOUD contract materializes on a working build host when a real telemetry-source
failure occurs (sqlite read failure, selftest log rotation, JSONL handle failure). Per
AGENTS.md §honest-limitation "FAIL-LOUD contract": the gate MUST emit `GATE_FAIL_INTERNAL`
on stderr + `exit 2` (NOT silent 0, NOT exit 0 spuriously). The dry-run on this VPS
emits `GATE_PASS` because the telemetry SQLite + selftest log + JSONL artifacts are
ALL healthy on this checkout (no IO failure surfaces to exercise the new exit-2 path).

## Forward-points

1. `TICKET-AGGREGATOR-SELFTEST-4SCENARIO` — 4-scenario selftest parallel to
   `tests/tools/selftest_check_manual_touches_per_video.sh` (Test #19) pattern:
   PASS happy / FAIL sqlite_down / FAIL selftest_log_corrupt / PRECOND no_db.
   FAIL-LOUD with exit codes 1/2 per AGENTS.md §honest-limitation. Companion `[INFO]` line
   on PASS addizionale al canonico `GATE_PASS`.

2. `TICKET-AGGREGATOR-FIRST-WEEK-RUN` — production-data macchina-verifica on working
   build host per AGENTS.md §honest-limitation. The first 7-day run after upstream
   rot-cascade resolved produces the canonical 8-metric table + 7 narrative lines +
   the cross-week delta baseline for narrative line 7 (metrica migliorata).

3. `TICKET-AGGREGATOR-METRIC-1-EXIT2-SCOPE-DECISION` — Cat-5 forward-point registered post-F3-landing per reviewer C's split-rationale feedback (this commit `11f4fd03`'s code-review verdict CV-A flagged the 4-metric exit-2 vs 4-metric null-coalesce split rationale as undocumented): formally document the metric-1 `videos_completed` SQL `count(DISTINCT composition_id) WHERE status='DONE' AND finished_at >= 7d` is canonical-never-NULL on the canonical SQL surface (no row-set can produce a SQL NULL count for a `status='DONE'` filter — the count is either 0 or a positive integer), so a null-coalesce wrapper for metric 1 is architectural-irrelevant, justifying the exit-2 split for the 4-telemetry-dependent metrics (3 manual_touches_per_video DB + 4 cost_per_finished_minute DB + 7 deterministic_hash_failures selftest_log + 8 bbox_contract_violations) and the null-coalesce retention for metrics 1+2+5+6. Also co-decides whether the metric 2 `failure_rate` TOTAL=0 boundary should ALSO be silently handled via `[NO-DATA]` (current F3 choice, replaces prior fabricated `0.00%`) OR exit 2 on TOTAL==0 (alternative that harmonizes with the 4-telemetry-dependent exit-2 surface). Resolution will lock the design invariant in AGENTS.md §honest-limitation Cat-3 anti-duplication rule. See TICKET-AGGREGATOR-FALLBACK-HARDENING §Forward-points in `docs/FOLLOWUP_TICKETS.md` for parallel context.

## Cross-references

- `tools/run_weekly_scorecard.sh` (the hardened aggregator, 1 file MOD, 0 NEW files)
- `docs/CHANGELOG.md` prepended `fix(tools): aggregator silent-fallback hardening (4 metrics exit-2)` entry
- `docs/CURRENT_STATUS.md` §Hygiene cite-only row +4 follow-on detail
- `docs/FOLLOWUP_TICKETS.md` §Open Blockers TICKET-AGGREGATOR-FALLBACK-HARDENING row
- AGENTS.md §Cat-3 (zero new public SDK API surface; satisfied)
- AGENTS.md §Cat-5 (4-doc same-commit alignment; satisfied)
- AGENTS.md §honest-limitation (FAIL-LOUD contract verified via dry-run syntax-clean on VPS)
- AGENTS.md §INFO-level diagnostic style (existing `[INFO] ${GATE_NAME}` line stays ≤ 200 chars per the §honest-cat-2-info-style precedent)
- AGENTS.md §regole "Fare PR piccole e mirate" (single atomic chore; 7 hardening edits locked together per Cat-3 anti-duplication; no churning retrofit)
- AGENTS.md TICKET-GATE-SUBJECT-RANGE closure (67-char subject envelope ≤ 72 push-range audit)
- Prior `tools(test-18): dashboard settimanale` Test 18 chore commit (the predecessor that introduced the 8 `\|\| echo 0` / `\|\| true` silent-fallback patterns this commit hardens)
- Prior F2 closure lineage at commit `52e48ddd` (this commit is the FIRST atomic chore landing on top of F2's drain-cleaned `origin/main` + `HEAD` 4-way equality)
- `docs/tickets/TICKET-INFRA-F2-DIVERGENCE.md` (the F2 closure parent ticket — provides the lineage context for the post-F2 hardening push-drain pattern)
- `tools/check_first_principles_fail_loud.sh` (Test #7 FAIL-LOUD canonical exit-2 contract pattern mirrored by this commit's 4 metric exit-2 wirings)
- `tools/check_ffmpeg_required.sh` (the canonical FAIL-LOUD gate pattern: GATE_FAIL_INTERNAL constant + stderr + idempotent exit 2 + cat-3-minimal-surface per AGENTS.md §honest-limitation)
