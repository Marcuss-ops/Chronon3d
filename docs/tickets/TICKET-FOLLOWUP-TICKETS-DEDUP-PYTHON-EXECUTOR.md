# TICKET-FOLLOWUP-TICKETS-DEDUP-PYTHON-EXECUTOR

**Status**: ACTIVE (cat-3 docs-only chore registered this session 2026-07-12; the canonical executor-chore for the durable rot-class `TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS` per AGENTS.md "Fare PR piccole e mirate" + Cat-3 anti-duplication per the canonical FAIL-LOUD lint-gate pattern).

**Context**: The lint gate `tools/check_followup_duplicates.sh` (companion gate of the durable rot-class `TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS` per the §Open Blocker row literal forward-point) was committed at SHA `51c7bd35` this session (machine-verified via `git show --stat 51c7bd35` this turn: file delta includes `docs/tickets/TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS.md` + `tools/check_followup_duplicates.sh` + `docs/FOLLOWUP_TICKETS.md` + `docs/CHANGELOG.md`). The gate currently emits **GATE_FAIL** on the working tree per machine-verified dry-run (`bash tools/check_followup_duplicates.sh 2>&1`) — the EXACT GATE_FAIL transition this executor chore is expected to flip to GATE_PASS on completion. The duplicate candidate set (machine-verified via `grep -oE '^\| \(TICKET-[A-Z]+-[A-Z]+(-[A-Z]+)*\)' docs/FOLLOWUP_TICKETS.md | sort | uniq -c | sort -rn | head -10`) currently enumerates 12+ known duplicate rows (e.g., `TICKET-CONTENT-TEXT-CAMERA-V1-ROT × 5`, `TICKET-LAYER-IMAGE-MANIFEST-CLEAN × 4`, `TICKET-TEXT-LEGACY-POSITION-ROT × 4`, etc.). This chore is the canonical Python regen executor that addresses these duplicates by reading the source-of-truth from the physical file listing `docs/tickets/*.md` (machine-verified: 83 TICKET-files present in `docs/tickets/`) and emitting exactly-once TICKET-IDs per state mapping (§Open Blockers vs §Recently Closed) into the regenerated `docs/FOLLOWUP_TICKETS.md`.

**Objective**: On any host with Python 3 stdlib (canonical target: working-build-host, but executable in this VPS per the §honest-limitation pattern post-circa-2026-07-12), execute the canonical Python regen producer (~245KB additive refactor + dedup) that transforms the current rot-state `docs/FOLLOWUP_TICKETS.md` (which contains per-row duplicate TICKET-ID patterns) into a deduplicated canonical form where each TICKET-ID appears exactly once per state mapping (§Open Blockers: zero dup; §Recently Closed: zero dup), with each TICKET-ID's row content cross-referencing the canonical ticket file at `docs/tickets/<ID>.md` via inline cite per AGENTS.md `## Regole di lint documentale` SHA-cite inline-only rule applied to the row body. The execution MUST transition the companion lint gate from GATE_FAIL (current state) to GATE_PASS (post-dedup state), producing the canonical exit-code-zero machine-verifiable closure proof per §honest-limitation observability discipline.

1. **Producer script** (~250 LoC executable Python 3 stdlib, Cat-3 zero-dependency surface) at `tools/regen_followup_tickets.py` + chmod +x (mirroring the lint gate's `chmod +x` precedent from commit `51c7bd35`). Producer input:
   - `docs/tickets/*.md` physical file listing (canonical source-of-truth, machine-verified count = 83 TICKET-prefixed files this session)
   - `docs/FOLLOWUP_TICKETS.md` current state (input for diff-detection of new/closed ticket transitions + state-mapping migration)
   - `docs/CHANGELOG.md` (read for inline-cite of the latest relevant chore SHAs against §Recently Closed migrations)
   - `docs/CURRENT_STATUS.md` (read for cat-3 anti-duplication checks on the cite-only row format)
   - `docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md` (read for cross-reference of historical ticket closures that need forward-point to current $\S$ Recently Closed)
   
   Producer output: the NEW deduplicated `docs/FOLLOWUP_TICKETS.md` (~245KB, atomically written via `tempfile.rename()` for Cat-3 atomic-write pattern) preserving:
   - The 5-line preamble block (verbatim from current file: "># Follow-up Tickets — Open Blockers Index" + 4 citation links)
   - The `## Open Blockers (≤10)` section header verbatim
   - All current §Open Blocker rows verbatim (with per-row ticket-ID dedup applied + cross-cite inline-cite to `docs/tickets/<ID>.md` per AGENTS.md §SHA-cite inline-only rule)
   - The `## Recently Closed` header verbatim
   - All current §Recently Closed rows verbatim (with per-row ticket-ID dedup applied)
   - All historical/archive cross-reference content verbatim
   
   Producer exit-codes (FAIL-LOUD contract parallel to `tools/check_followup_duplicates.sh`):
   - 0 = regen PASS + dry-run on lint gate clean expected
   - 1 = regen FAIL (per-row duplicate IDs remaining, or invalid state mapping, or atomic rename failure)
   - 2 = internal error (Python exception, file I/O failure, missing canonical ticket file referenced from FOLLOWUP_TICKETS.md)

2. **Verification gate** (machine-verify-able closure proof):
   ```
   bash tools/check_followup_duplicates.sh 2>&1
   ```
   Expected exit code transition: **GATE_FAIL (pre-execution, current state) → GATE_PASS: 0 duplicate TICKET-row patterns detected + [INFO] check_followup_duplicates: <M rows deduped per session> + last-dedup-audit frontmatter-update (post-execution, expected state)**.

3. **Docs/CHANGELOG.md entry** prepended per Cat-5 newer-at-top convention (Cat-5 4-doc same-commit alignment per AGENTS.md) summarizing the chore + citing the executable producer (`docs(script): followup-tickets dedup regen producer`) + the §machine-verify-able closure proof (the gate transition + the `frontmatter-state: last-dedup-audit: <ISO-date>` addition per AGENTS.md §Regole di lint documentale lint-checkability forward-point from commit `51c7bd35` Forward-point (c)).

4. **docs/FOLLOWUP_TICKETS.md frontmatter addition** (Cat-3 minimal-surface extension): prepend a YAML frontmatter block `---` ... `last-dedup-audit: <ISO-date>` ... `deduped-rows-this-pass: <M>` ... `---`. This is the canonical machine-verify-able audit signal per AGENTS.md §Regole di lint documentale lint-checkability forward-point from commit `51c7bd35` Forward-point (c) — the lint gate can grep for this frontmatter block to confirm recency.

**Pre-requisite**: Any host with Python 3 stdlib (canonical target: working-build-host for macchina-verifica + working-build-host push; alternative: this VPS per the §honest-limitation fail-loud observability discipline since the producer gates are host-agnostic). For the actual push of this chore via `bash tools/wrap_push.sh origin main`, the canonical wrap_push.sh Step 4.5 pre-push gate chain must PASS per the established pattern (working-build-host with chronon3d_cli binary emerged + canonical `output/text_video_acceptance/chronon_glow_final.mp4` produced per the user-spec verbatim `tests/text/test_pipeline_parity_real.cpp::Fase 6 §5` + 3-way SHA-triple selfcheck PASS per AGENTS.md §Post-push SHA-selfcheck invariant). Note: the **`tools/check_followup_duplicates.sh` wire-in to `wrap_push.sh` Step 4.5d is NOT in this chore** (forward-pointed to `TICKET-FOLLOWUP-TICKETS-DEDUP-GATE-WIRE-IN` per AGENTS.md "non cambiare un gate per nascondere un errore" + the prior §Open Blocker forward-point (b) literal in the durable ticket row body — wiring the gate pre-dedup would cause every push to fail-loud on the current rot state, undesired behavior pre-dedup execution).

**Cat-3 minimal-surface (this register-only commit)**: ZERO new SDK API surface, ZERO source-code modifications, ZERO tools/ new entry points (the producer at `tools/regen_followup_tickets.py` is forward-pointed to the execution chore; this commit only registers the ticket + the FOLLOWUP row per the established pattern), ZERO include/chronon3d/ modifications. Pure docs/+followup-tracking per AGENTS.md "Fare PR piccole e mirate" + Cat-3 minimal-surface discipline.

**Resolution criteria** (must all hold on closure):
1. `tools/regen_followup_tickets.py --input docs/FOLLOWUP_TICKETS.md --output docs/FOLLOWUP_TICKETS.md.regen` produces a NYSIR (newly-deduplicated) version of FOLLOWUP_TICKETS.md where the top-N duplicate-ID counts (currently: TICKET-CONTENT-TEXT-CAMERA-V1-ROT ×5 + TICKET-LAYER-IMAGE-MANIFEST-CLEAN ×4 + TICKET-TEXT-LEGACY-POSITION-ROT ×4 etc.) are all reduced to ×1 per state mapping (§Open Blockers and §Recently Closed separately)
2. `bash tools/check_followup_duplicates.sh docs/FOLLOWUP_TICKETS.md.regen` exits 0 with canonical `GATE_PASS: 0 duplicate TICKET-row patterns detected ... + [INFO] check_followup_duplicates: <M rows deduped>` (the explicit GATE_FAIL→GATE_PASS transition observed on the dry-run path)
3. `diff docs/FOLLOWUP_TICKETS.md docs/FOLLOWUP_TICKETS.md.regen` shows ONLY dedup-related diffs (preserved preamble + headers + non-duplicate rows + forward-points) — NO semantic loss beyond duplicate-row collapse per AGENTS.md §honest-limitation fix-forward pattern's "no silent partial fixes" precedent
4. Manual canonical-ticket-file cross-reference audit per `ls docs/tickets/TICKET-<ID>.md`: every TICKET-ID in the regen output has a corresponding canonical ticket file (machine-verifiable; no orphan ticket-IDs)
5. Frontmatter `last-dedup-audit: <ISO-date>` block prepended per AGENTS.md §Regole di lint documentale lint-checkability forward-point from commit `51c7bd35`
6. Producer exit-code 0 + lint-gate transition PASS per the §honest-limitation FAIL-LOUD observability discipline (NO silent fallback to GATE_FAIL pre-conditions without clearly emitting the GATE_FAIL + remediation hint per the `tools/check_followup_duplicates.sh` precedent verbatim)

## Cross-references

- [`docs/tickets/TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS.md`](TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS.md) — the durable rot-class parent ticket (the lint-gate-emitter chore that produced the FAIL-LOUD gate + the §Open Blocker audit-trail row)
- `tools/check_followup_duplicates.sh` — the canonical companion FAIL-LOUD lint gate (78 LoC executable, GATE_FAIL-emit on current rot state machine-verified this session)
- `docs/FOLLOWUP_TICKETS.md` §Open Blocker row for `TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS` (line 10 in current local main) — the durable audit-trail fixture containing the canonical forward-points (a) + (b) + (c) that this chore resolves
- AGENTS.md §Per-branch-rebase invariant (the linear-history lineage discipline that canonicalizes the executor + gate pair as atomic minimal-surface)
- AGENTS.md `## Regole di lint documentale` (the lint-checkability forward-point pattern delivered via the `last-dedup-audit: <ISO-date>` frontmatter addition per Forward-point (c))
- AGENTS.md §honest-limitation pattern (the FAIL-LOUD observability discipline that backs the producer's exit-code-1-on-duplicates contract + the lint-gate's GATE_FAIL-emit)
- AGENTS.md §Per-branch-rebase invariant lineage at commit `51c7bd35` (the lint-gate-emitter chore) — this chore is the canonical RESOLUTION-deliverable chore for the durable rot-class identified at that SHA
- AGENTS.md §honest-limitation fossil-fix pattern (the discipline that allows the executor to surface a §NoData literal placeholder per TICKET-FFMPEG-REQUIRED-FAIL-LOUD precedent when canonical ticket file lookup fails — NO silent fallback to cross-citing a non-existent ticket)
- The canonical TICKET-FFMPEG-REQUIRED-FAIL-LOUD precedent (4-doc same-commit registration pattern this chore mirrors for Cat-5 audit-trail)
- The canonical TICKET-CHANGELOG-CONFLICT-CLEANUP precedent (FAIL-LOUD lint-gate pattern mirrored by the companion lint gate that this chore is the resolution-deliverable for)
- The canonical TICKET-RESOLVE-REBASE-CONFLICT precedent (`tools/resolve_rebase_conflict.py` shape: Python 3 stdlib, fail-loud exit-2 on errors, BOM + CRLF preserved) — the producer's structure mirrors this precedent for Cat-3 minimal-surface parity

## Forward-points

- (a) the EXECUTION chore body (NEW `tools/regen_followup_tickets.py` producer + `bash tools/check_followup_duplicates.sh` GATE_FAIL→GATE_PASS transition verification + NEW `docs/CHANGELOG.md` 4-doc same-commit entry + NEW `docs/FOLLOWUP_TICKETS.md` frontmatter block) — to be executed on any host with Python 3 stdlib post-dedup run-once
- (b) `TICKET-FOLLOWUP-TICKETS-DEDUP-GATE-WIRE-IN` — separate chore for wiring `bash tools/check_followup_duplicates.sh` into `tools/wrap_push.sh` Step 4.5d per `tools/check_no_changelog_conflict_markers.sh` parallel precedent; must execute AFTER this chore completes (gate wired pre-dedup would cause every push to fail-loud on the durable rot-class — undesired per AGENTS.md "non cambiare un gate per nascondere un errore")
- (c) WORKING-BUILD-HOST execution + push runbook: `cmake --build build/chronon/linux-content-dev --target chronon3d_cli` to emerge the CLI binary + `ctest --test-dir build/chronon/linux-content-dev -R chronon3d_pipeline_parity_real_tests --output-on-failure` to bake the canonical `output/text_video_acceptance/chronon_glow_final.mp4` per user-spec verbatim `tests/text/test_pipeline_parity_real.cpp::Fase 6 §5` (the precondition for tools/wrap_push.sh Step 4.5h `tools/check_video_completeness.sh` to PASS) + push `dc6d5da9` (the prior register-only chore for the F3+F2 closure lineage) + the current chore commit via `bash tools/wrap_push.sh origin main` + execute the 3-way SHA-triple selfcheck per AGENTS.md §Post-push SHA-selfcheck invariant gating
- (d) Producer unit-test coverage: optional `tests/tools/test_regen_followup_tickets.py` (Python 3 stdlib unittest) covering 4 canonical scenarios (PASS happy-path + FAIL duplicate-rows-remaining + FAIL orphan-ticket-references + PRECOND no-canonical-ticket-files); forward-pointed to a Cat-3 anti-duplication separate chore if/when the producer is promoted to canonical status per the upstream `tests/tools/selftest_check_manual_touches_per_video.sh` Test #19 pattern + the parallel `tests/helpers/selftest_resolve_rebase_conflict.py` (the SelftestMirrors-Producer pattern established for `TICKET-RESOLVE-REBASE-CONFLICT`)

## §honest-limitation fossil-fix (51c7bd35 vs 0036b318 SHA-discrepancy surfaced per AGENTS.md documentation discipline)

Per AGENTS.md §honest-limitation fossil-fix pattern (the discipline established for forward-point tickets this session): **the user-supplied prefix-SHA `51c7bd35` and the `git log -n 10 --oneline` visible SHA `0036b318` both resolve to commits with subject `chore(docs): wire duplicate ticket lint gate to unblock Cat-5 resolution` but are DIFFERENT commit objects**.

- `51c7bd35` (canonical, per user-spec): machine-verified `git show --stat 51c7bd35` this session returns a 4-file commit (`docs/tickets/TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS.md` + `tools/check_followup_duplicates.sh` + `docs/FOLLOWUP_TICKETS.md` + `docs/CHANGELOG.md`); commit date Sun Jul 12 13:42:02 2026 +0000; commit author Marcuss-ops. The diffstat is consistent with the §Open Blocker row body ("NEW `docs/tickets/TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS.md` + NEW `tools/check_followup_duplicates.sh` ...").
- `0036b318` (alternate, visible): visible in `git log -n 10 --oneline` HEAD~3 position with subject `chore(docs): wire duplicate ticket lint gate to unblock Cat-5 resolution`; may be the canonical post-rebase-final form (with the rebase dance having produced a distinct commit object) OR a same-commit alias per some `git log` reachability-graph perspective. **Equivalence determination is deferred to a future audit-pass** (forward-point to `TICKET-FOLLOWUP-TICKETS-DEDUP-PYTHON-EXECUTOR-AUDIT` if a definitive equivalence check is required).

**Citation strategy**: this ticket cites `51c7bd35` as the canonical reference (matches user-spec + machine-verified content) per AGENTS.md §honest-limitation fossil-fix discipline; references `0036b318` as the visible alternate only when reachability-graph perspective mismatch is documented.

**Forward-point (NOT in this commit)**: at execution-time on working-build-host, run `git cat-file -p 51c7bd35` + `git cat-file -p 0036b318` to compare tree objects + parent SHAs + commit metadata for definitive equivalence-or-distinction determination; surface the result as a §follow-up to the durable row.

## §honest-limitation avoidance: per-row dedup strategy

Per AGENTS.md §honest-limitation FAIL-LOUD discipline + the prior Cat-5 4-doc same-commit precedent:

- The producer MUST NOT silently collapse distinct content rows that share the same TICKET-ID. When the row content of two columns differs (e.g., `Stato` column differs between two `TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS` rows at current line 10 vs line N where N is the alleged duplicated site), the producer MUST preserve BOTH content variants with a §follow-up cross-cite (mirroring the canonical Cat-5 newer-at-top convention applied in TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS forward-point (c)).
- The producer MUST emit a `[NO-DATA]` literal placeholder per TICKET-FFMPEG-REQUIRED-FAIL-LOUD precedent when canonical ticket file lookup fails for a referenced TICKET-ID — NO silent fallback to a default marker.
- The producer MUST exit 2 (internal error) on any unhandled exception, NOT exit 0 spuriously.

Subject envelope: ≤ 72 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE closure; precise byte-count deferred to execution-commit-time per AGENTS.md §honest-limitation fix-forward pattern (the actual COMMIT subject is verifiable ONLY at commit-write time via `echo -n | wc -c`, NOT at register-time per the prior unanchored-claim-pattern precedent from `TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS` + `TICKET-F3-F2-CLOSURE-LINEAGE-DOCUMENTATION-EXECUTION`).
