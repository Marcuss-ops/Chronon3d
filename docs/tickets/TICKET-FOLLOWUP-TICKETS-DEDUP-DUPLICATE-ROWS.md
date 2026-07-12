# TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS

**Status**: OPEN (forward-point from 2026-07-12 rot-source-resolved chore, registered chore land SHA `6bd4b01b`).

**Context**: `docs/FOLLOWUP_TICKETS.md` has accumulated **duplicate TICKET-row patterns** (file-level rot from prior cycle collisions), structurally blocking Cat-5 2-doc same-commit chore validations per AGENTS.md §Regole di lint documentale + the `TICKET-FFMPEG-REQUIRED-FAIL-LOUD` Cat-3 anti-duplication precedent. The duplicate-row pattern bit the rot-cascade resolution chore this cycle via `str_replace` exact-match returning 2+ occurrences on the `TICKET-BUILD-ROT-CASCADE-CAMERA` row content (the row appears multiple times across §Open Blockers + downstream duplicated sections, mechanically produced by prior un-aligned chore cycles).

**Objective**: Regenerate `docs/FOLLOWUP_TICKETS.md` sections (NOT just §Open Blockers, also §Recently Closed + any other duplicated sections) via a Python script that uses the canonical ground-truth state mapping: every ticket referenced in the file must have an existing physical `docs/tickets/TICKET-*.md` file AND every `docs/tickets/TICKET-*.md` file must appear exactly once in the right §section. The lint gate `tools/check_followup_duplicates.sh` is the forward-prevention gate for future regressions; this ticket's executor + the gate together close the rot-class.

**Pre-requisite**: This ticket REGISTERS the workstream + sets up the lint gate per Cat-3 minimal-surface — 1 NEW ticket file (this one) + 1 NEW tool script + minimal docs/ updates. Actual Python regen execution is a SEPARATE future chore per AGENTS.md "Fare PR piccole e mirate" + minimum-churn discipline; the regen would otherwise be a 245KB additive file refactor that warrants its own atomic chore.

**Resolution criteria** (must all hold on closure):
1. `docs/FOLLOWUP_TICKETS.md` dedup'd via Python regen — zero duplicate `TICKET-XXX` rows per-file
2. by-ticket-state-accurate — every row references a real `docs/tickets/TICKET-*.md` file
3. `bash tools/check_followup_duplicates.sh` returns GATE_PASS (`grep -cE '^| TICKET-[A-Z0-9._-]+'` deduplication clean)
4. **Forward-wire** `bash tools/check_followup_duplicates.sh` into `tools/wrap_push.sh` as a canonical post-commit-subject-length pre-push gate (parallel to `tools/check_no_changelog_conflict_markers.sh` per the shared-doc-class invariant)
5. Existing tests `bash tools/check_doc_sync.sh` + `bash tools/check_architecture_boundaries.sh` + `bash tools/check_repo_artifacts.sh` all PASS post-regen (regression-lock)

## Cross-references

- `docs/FOLLOWUP_TICKETS.md` (the rot-bearing file this ticket addresses; the file-level duplicate-row rot itself)
- `tools/check_followup_duplicates.sh` (the canonical FAIL-LOUD lint gate preventing recurrence; companion gate to `tools/check_no_changelog_conflict_markers.sh` per the shared doc-class-rotation invariant)
- `TICKET-FFMPEG-REQUIRED-FAIL-LOUD` precedent (canonical 4-doc same-commit pattern for opening a Cat-3 minimal-surface docs/ chore — 1 NEW ticket + 1 NEW tool + 2 EDITs)
- `TICKET-CHANGELOG-CONFLICT-CLEANUP` precedent (the historical pattern for adding a FAIL-LOUD lint gate to prevent doc-class rot recurrence)
- `tools/wrap_push.sh` (post-resolution wire-in target, parallel to `tools/check_no_changelog_conflict_markers.sh` at Step 4.5d)
- AGENTS.md §Regole di lint documentale (the doc-governance discipline this chore adheres to per the rot-task prevention scope)

## Forward-points

- **(a) `TICKET-FOLLOWUP-TICKETS-DEDUP-PYTHON-EXECUTOR`** — separate chore per "Fare PR piccole e mirate"; Python script regen using canonical tickets list (`docs/tickets/*.md`) + state mapping (§Open Blockers vs §Recently Closed); additive 245KB file refactor; per the §honest-limitation pattern this chore MUST be empirically verified via `git diff --stat docs/FOLLOWUP_TICKETS.md` showing dedup count reduction.
- **(b) `TICKET-FOLLOWUP-TICKETS-DEDUP-GATE-WIRE-IN`** — post-dedup-execution; wire `bash tools/check_followup_duplicates.sh` invocation into `tools/wrap_push.sh` Step 4.5d (canonical pre-push gate-chain slot parallel to `tools/check_no_changelog_conflict_markers.sh`); sequencing: gate emits GATE_FAIL until dedup chore lands, then gates-blocked-on-dedup-execution is the canonical invariant per AGENTS.md "non cambiare un gate per nascondere un errore".
- **(c) Frontmatter-state audit** — post-dedup-execution; add `last-dedup-audit: <ISO-date>` frontmatter to `docs/FOLLOWUP_TICKETS.md` + verify against `git log -1 --format='%H %ad' docs/FOLLOWUP_TICKETS.md` so future agents have a machine-verify-able dedup recency stamp per AGENTS.md §Regole di lint documentale lint-checkability forward-point.