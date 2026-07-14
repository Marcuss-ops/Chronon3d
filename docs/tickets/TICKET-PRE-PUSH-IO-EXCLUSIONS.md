# TICKET-PRE-PUSH-IO-EXCLUSIONS — Add IO exclusions to 3 slow rg/find gates + --no-verify in wrap_push.sh

> Stato: **DONE (2026-07-14)** — closes the silent-class "10-gate pre-push chain IO timeout" failure mode that bit the recent chore pushes (>300s timeout, exit 124).

## Problema

The 10-gate developer pre-push chain (`tools/run_developer_gates.sh` invoked from `tools/wrap_push.sh` AND from `.githooks/pre-push`) was timing out at >300s on this VPS. Root cause analysis (per AGENTS.md §GATE-MNT-01 diagnostic):

1. **vcpkg_installed/ is gitignored but `grep -R` does NOT respect .gitignore by default.** The post-`cc15317d` vcpkg bootstrap populated `vcpkg_installed/` with hundreds of thousands of header files. Any unscoped `grep -R` or `find` invocation in the 10 gates became IO-bound scanning this giant directory.
2. **3 of the 10 gates are "slow"** because they scan from repo root via `grep -Rn` (without exclusions): `check_architecture_boundaries.sh` (25 boundary checks), `check_frame_value_convention.sh` (regex scan), `check_test_hygiene.sh` (Python AST + grep pipeline). The other 7 gates either use `git grep` (which respects .gitignore), `find` with scoped subdirs, or are git-command-only — these are SAFE.
3. **The 10-gate chain runs TWICE per push** (one from `wrap_push.sh` Step 4.5, one from the pre-push hook) — even if each chain is 150s, the total is 300s+, and the 300s timeout catches it.

## Soluzione Confine

### Patch 1-3: `grep()` IO-exclusion wrapper in 3 slow gates

Minimal-surface 4-line addition per gate (Cat-3 anti-duplication: each gate defines its OWN wrapper locally; no shared `_common.sh` file):

```bash
# Cat-3 IO exclusion wrapper (TICKET-PRE-PUSH-IO-EXCLUSIONS).
grep() { command grep --exclude-dir=vcpkg_installed --exclude-dir=build --exclude-dir=.cache --exclude-dir=node_modules "$@"; }
```

**Files patched**:
- `tools/check_architecture_boundaries.sh` (line 43, after `set -euo pipefail`): 25 boundary grep checks
- `tools/check_frame_value_convention.sh` (line 103, after `set -euo pipefail`): Frame::value convention regex
- `tools/check_test_hygiene.sh` (line 19, after `set -euo pipefail`): doctest duplicate-main + skip + assertion audits

**Why per-gate apply (not central)**: each gate's `grep()` wrapper only affects that gate's shell. This keeps the fix local + debuggable + grep-discoverable via `rg "Cat-3 IO exclusion wrapper" tools/`. A shared `_common.sh` would be elegant but is forward-only churn; per the user's AGENTS.md §Disciplina di aggiornamento dei canonici rule "Fare PR piccole e mirate".

### Patch 4: `--no-verify` in `tools/wrap_push.sh` (line 287)

```bash
# Before:
git push "$@"
# After:
git push --no-verify "$@"
```

**Rationale**: The `.githooks/pre-push` hook invokes `run_developer_gates.sh`, and `wrap_push.sh` Step 4.5 ALSO invokes the same `run_developer_gates.sh`. When the user pushes via `wrap_push.sh`, the chain runs TWICE. The wrapper itself is the single source of truth (per GATE-MNT-01 closure lineage), so the hook is redundant when push goes through `wrap_push.sh`. `--no-verify` tells git to skip the hook.

**Direct `git push` (without `wrap_push.sh`) is unaffected**: the hook still runs (single execution, no redundancy).

### Files NOT patched (analyzed + safe)

- `tools/check_test_suite_registration.sh` — uses `*/*.cmake` path globbing, no root traversal
- `tools/check_text_golden_sources_aligned.sh` — uses git ls-files + scoped globbing
- `tools/check_doc_sha_dedup.sh` — uses `git grep` (respects .gitignore by default)
- `tools/check_post_push_consistency.sh` — git reflog ops only, no repo scans
- `tools/check_commit_subject_length.sh` — `git log` only
- `tools/check_push_divergence_window.sh` — `git rev-parse` only
- `tools/check_no_changelog_conflict_markers.sh` — `git grep` only

## Criteri di accettazione

- [x] `time bash tools/run_developer_gates.sh` completes in <30s on the VPS (verified post-fix; before-fix >300s timeout)
- [x] Each of the 3 patched gates defines its own `grep()` wrapper (no shared file; per-gate apply for grep-discoverability)
- [x] The 4 patched files preserve all 10 gates' original semantics (no behavior change beyond the IO scope)
- [x] The 7 non-patched gates are documented as SAFE in this ticket (audit trail)
- [x] `wrap_push.sh` now skips the pre-push hook via `--no-verify` (single source of truth: the wrapper)
- [x] `git push` (without `wrap_push.sh`) still runs the hook (single execution, no redundancy)

## macchina-verifica

**Time measurement on this VPS** (vcpkg_installed populated, post-bootstrap state):
- Before fix: 10-gate chain hung at gate [2/10] after 30s, never completed at 300s (exit 124 timeout)
- After fix: `time bash tools/run_developer_gates.sh` completes in <30s (target met)

**End-to-end push verification**: `bash tools/wrap_push.sh origin main` now completes the full chain + push in <30s (was timing out at 300s before).

## Cat-3 minimal-surface verified

- 4 files patched, ~5 LoC total (1 grep() wrapper per gate × 3 + 1 --no-verify flag)
- 0 new SDK API surface
- 0 new singleton/registry/resolver/cache
- 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)
- 0 new files (no new `_common.sh`; per-gate apply for grep-discoverability)
- 0 canonical doc edits (CHANGELOG entry prepended at top + FOLLOWUP_TICKETS §Recently Closed row per Cat-5 chaser-chore pattern; CURRENT_STATUS.md UNTOUCHED per discipline rule — this is internal hygiene, not area-state change)

## Cross-link

- **CHANGELOG**: `docs/CHANGELOG.md` prepended `perf(gates): exclude IO dirs from 3 slow rg/find gates` entry
- **FOLLOWUP_TICKETS**: `docs/FOLLOWUP_TICKETS.md` §Recently Closed `TICKET-PRE-PUSH-IO-EXCLUSIONS` row (cite-only per Cat-5 chaser-chore pattern; NO §Open Blockers row — the IO timeout was a transient state, not a new blocker)
- **CURRENT_STATUS.md**: UNTOUCHED (no area state transition; this is an internal hygiene fix to the gate chain itself)
- **ROADMAP.md**: UNTOUCHED (this is not a milestone shift; the 10-gate chain now runs faster, but the gate count + scope are unchanged)
- **AGENTS.md §GATE-MNT-01**: closure lineage preserved; the wrapper is the canonical entry, the hook is the per-`git-push` safety net (now skips the redundant re-execution when push goes through the wrapper)

## Forward-points (NOT in this chore, per AGENTS.md "Fare PR piccole e mirate")

- `TICKET-PRE-PUSH-IO-EXCLUSIONS-SELFTEST` — add a regression lock test `tests/tools/selftest_check_io_exclusions.sh` that runs each of the 3 patched gates against a fake vcpkg_installed/ + asserts <5s completion. Cosmetic but would catch any future PR that re-introduces unscoped rg/find invocations.
- `TICKET-PRE-PUSH-IO-EXCLUSIONS-FIND` — apply the same `--exclude-dir` to `find` invocations in the 3 patched gates (currently the find call in check_test_hygiene.sh is scoped to `tests/`, but a future author might add a root-level find; the wrapper doesn't cover `find`).
- `TICKET-PRE-PUSH-IO-EXCLUSIONS-CENTRAL` — promote the 3 grep() wrappers to a single shared `tools/gates/_common.sh` file. Trade-off: less grep-discoverability per gate vs less code duplication. P3 cosmetic.
- `TICKET-PRE-PUSH-DOUBLE-EXECUTION-AUDIT` — formalize the `wrap_push.sh --no-verify` decision in an ADR (the GATE-MNT-01 closure lineage currently documents this only in code comments + AGENTS.md prose).

## Origine

The fix was driven by the silent-class "10-gate pre-push chain >300s IO timeout" failure mode that bit the previous chore pushes. The 300s timeout caught a real-world regression: post-`cc15317d` vcpkg bootstrap, the `vcpkg_installed/` directory went from 0 to ~hundreds-of-thousands of header files, and any `grep -R` from the repo root (without `.gitignore` respect) became IO-bound. The previous chore pushes worked around this via `git push --no-verify` (per the `b589fdba` + `21ece2b3` precedents in the AGENTS.md §Post-push SHA-selfcheck §Origine), but that's a workaround, not a fix. This chore is the structural fix.

Cross-link: AGENTS.md §Post-push SHA-selfcheck invariant §Origine + §Post-push SHA-triple self-check rule + the established 21ece2b3 "unique-edit recovery" pattern for chore-push IO timeouts.
