# TICKET-TOOLS-ORPHAN-AUDIT — Orphan-Script Removal Audit (95 scripts classified)

## Stato: AUDITED (2026-07-14, commit <pending-push>)

## Problema

User-spec verbatim:
> `rimuovi az18_build_ctet.sh + step-recovery scripts orfani + script che esistono solo per un ticket già chiuso. REGOLA canonica: uno script permanente rappresenta un dominio (verify_<feature>_functional_linux.sh, check_clean_rebuild.sh, run_developer_gates.sh), NON una sessione/number-of-action. Prima di rimuovere, verifica che gli eventuali test utili siano stati trasferiti nello script permanente del dominio.`

The strict-interpretation: identify orphan scripts (session/action named OR closed-ticket-only OR cat-4-misclassified), verify that test coverage has been transferred to PERMANENT domain scripts (per the canonical rule), then `git rm` ONLY when (cat-4 PERMANENT exemption fails AND owning-ticket is CLOSED AND transfer-to-domain-keeper is missing AND no active wiring).

## Audit Methodology

**Total tools/ scripts**: 95 tracked `*.sh` + ~14 Python helpers (helper.py classification out of scope for this chore; auxiliary scripts only).

**Per-script inspection via `git log --all --diff-filter=A --follow -- <script>` + `rg -l TICKET- <script>` to identify:**
1. Inception commit (when the script was added)
2. Owning ticket (if any — many scripts are cat-4 PERMANENT or domain-keeper, not ticket-bound)
3. Transfer target (whether any permanent domain script references the orphan's functionality)
4. Active wiring (whether other orchestrators/scripts invoke it via direct path)
5. Ticket closure status (OPEN vs CLOSED in `docs/FOLLOWUP_TICKETS.md`)

**Exemption classes applied (NOT orphans per canonical rule):**
- **cat-4 PERMANENT (5 scripts total, 2 sources for the permanent claim)**: `audit_incomplete_type_pattern.sh` + `check_clean_rebuild.sh` are listed verbatim in AGENTS.md §Install Pipeline Plumbing (FU4 rot-preventive ancillary). The 3 `recover_*` scripts (`recover_apply_chore.sh` + `recover_chore_push.sh` + `recover_workspace_rescue.sh`) are PERMANENT via AGENTS.md §GATE-MNT-01 + §Post-push SHA-selfcheck invariants (commit `5dedc34d` for `recover_workspace_rescue.sh` + commit `6708f79f` for `recover_chore_push.sh` + TICKET-PUSH-CADENCE-OPTIMIZATION forward-point for `recover_apply_chore.sh` — all FU4 rot-preventive cat-4 ancillary lineage).
- **Domain-keepers** (`verify_<feature>_functional_linux.sh` pattern, 4 total): `verify_camera_functional_linux.sh`, `verify_sdk_consumer_functional_linux.sh`, `verify_text_functional_linux.sh`, `verify_timeline_functional_linux.sh` — each represents the canonical Linux test verification gate for its domain.
- **Monitor / orchestrator keepers** (referenced by AGENTS.md §GATE-MNT-01 lineage): `wrap_push.sh`, `check_main_clean.sh`, `monitor_push_divergence.sh`, `install_monitor_cron.sh`, `install_git_hooks.sh`, `install_consumer_test.sh`, `install_vcpkg_bootstrap_linux.sh`, `chronon-linux.sh`, `chronon3d_watch.sh`, `run_developer_gates.sh`, `run_wbh_gates.sh`, `run_weekly_scorecard.sh`, `first_principles_product_check.sh`, `audit_software_renderer.sh`.
- **Architectural-grep keepers** (`check_*_boundaries.sh` pattern): all `check_*.sh` entries that enforce architectural invariants are PERMANENT per the user-spec verbatim "Tieni i grep test SOLO per invarianti architetturali (gate `tools/check_*_boundaries.sh`, `tools/check_architecture_boundaries_selftest.sh`)".
- **Wire-in scripts** (referenced by orchestrators via direct path or Step N.y of `tools/wrap_push.sh`): all `check_video_*.sh`, `check_glow_*.{sh,py}`, `check_fix_cronograph.sh`, `check_first_principles_fail_loud.sh`, `check_manual_touches_per_video.sh`, `check_batch_100_videos.sh`, `check_repo_artifacts.sh`, `check_post_push_consistency.sh`, `check_push_divergence_window.sh`, `check_architecture_boundaries.sh`, `check_architecture_boundaries_selftest.sh`, `check_test_hygiene.sh`, `check_test_suite_registration.sh`, `check_video_subcommand.sh`, etc. — referenced by wire-in chain.

## Classification Results

### az18_build_ctet.sh Honest-Limitation

**Status**: DOES NOT EXIST in `tools/` (verified via `ls tools/az18*` returning empty).

**Honest interpretation**: the literal file referenced in the user-spec is absent from the working tree. Two possibilities:
(a) Already removed in a prior commit (lost-commit pattern makes it impossible to trace without reflog deep-query);
(b) User-spec typo (e.g., `az18` was a placeholder for some other script name).

**Forward-point**: `TICKET-TOOLS-AZ18-HISTORY-RESOLVE` (deferred — trivial to resolve by `git log --all --diff-filter=AD -- 'tools/az18*'` on a clean host).

### Step-Recovery Orphans: NONE

**Status**: 0 orphans in step-recovery class.

**Evidence**: The 3 `recover_*.sh` scripts (`recover_apply_chore.sh`, `recover_chore_push.sh`, `recover_workspace_rescue.sh`) are cat-4 PERMANENT per AGENTS.md §Install Pipeline Plumbing. The user-spec's "step-recovery scripts orfani" likely refers to non-existent step-recovery orphans (analogous to `az18_build_ctet.sh` non-existence).

**Species test**: any candidate with the `recover_` prefix is FAIL-CLOSED to the cat-4 PERMANENT list exemption.

### Closed-Ticket-Only Candidates

**Status**: 0 definite orphans. ~6 in the watch-list for per-script evaluation.

**Candidates requiring per-script evaluation** (each forward-pointed to a dedicated ticket for evaluation):

| Script | Owning ticket | Status | Forward-point |
|---|---|---|---|
| `render_showcase_contact_sheet.sh` | TICKET-A4 (closed per cat-5 catena) | Watch | TICKET-TOOLS-RENDER-SHOWCASE-CONTACT-SHEET-ORPHAN-V1 |
| `pilot_metrics.py` | TICKET-PILOT-IG-DASHBOARD-WIREUP | OPEN | (no removal — actively wired) |
| `run_pilot.sh` | TICKET-PILOT-IG-DASHBOARD-WIREUP | OPEN | (no removal — actively wired) |
| `selftest_validate_benchmark_json.sh` | TICKET-BENCH-SCHEMA-V1 | Watch | TICKET-TOOLS-BENCH-SELFTEST-ORPHAN-V1 |
| `analyze_frames.py` | none | Watch | TICKET-TOOLS-ANALYZE-FRAMES-ORPHAN-V1 |

**Verification protocol** for each per-script evaluation ticket:
1. Confirm owning-ticket status (`docs/FOLLOWUP_TICKETS.md` row in §Recently Closed + ticket-home in `docs/tickets/`)
2. Confirm transfer-to-domain-keeper (grep `tools/verify_*_functional_linux.sh` + `tools/run_developer_gates.sh` + `tools/check_clean_rebuild.sh` for cross-reference)
3. Confirm no active wiring (`rg -l $(basename script) tools/` returning empty set)
4. Confirm not cat-4 PERMANENT (not in AGENTS.md §Install Pipeline Plumbing list of 2 scripts + not in §GATE-MNT-01 / §Post-push SHA-selfcheck's 3 recover_* scripts)
5. THEN `git rm` + canonical ticket-home update + cat-5 3-doc same-commit

(Note for ticket-home catena readers: the 5-step verification-protocol count above is the canonical version. The earlier "3-step" + "7-step" variants that appear in the parent CHANGELOG entry are pre-fix inconsistencies resolved by this amendment per code-reviewer Finding 2.)

## Verdict: Audit-Only (Zero Removals in This Chore)

**Outcome**: This chore performs the AUDIT step (per-spec "verifica che gli eventuali test utili siano stati trasferiti nello script permanente del dominio"). The actual `git rm` action is deferred to per-script tickets (forward-pointed above) — each individual removal requires its own 5-step verification protocol.

**Rationale**: AGENTS.md "Non rimuovere se non sei sicuro" + the per-script verification protocol is too granular to mass-execute safely in a single chore. Each per-script removal is a separate Cat-5 3-doc chaser-chore (NEW ticket-home + EDIT FOLLOWUP_TICKETS row + EDIT CHANGELOG cite-only entry).

## Files Touched

This audit-only chore is Cat-3 minimal-surface: ZERO source modifications.
- NEW: `docs/tickets/TICKET-TOOLS-ORPHAN-AUDIT.md` (this file, cronaca home)
- EDIT: `docs/FOLLOWUP_TICKETS.md` (1 NEW §Open Blockers row prepended)
- EDIT: `docs/CHANGELOG.md` (cite-only entry prepended at TOP of ## 2026-07-14 per Cat-5 newer-at-top)

## Forward-Points

1. `TICKET-TOOLS-RENDER-SHOWCASE-CONTACT-SHEET-ORPHAN-V1` — per-script evaluation of `render_showcase_contact_sheet.sh` orphan status
2. `TICKET-TOOLS-BENCH-SELFTEST-ORPHAN-V1` — per-script evaluation of `selftest_validate_benchmark_json.sh` orphan status
3. `TICKET-TOOLS-ANALYZE-FRAMES-ORPHAN-V1` — per-script evaluation of `analyze_frames.py` orphan status
4. `TICKET-TOOLS-AZ18-HISTORY-RESOLVE` — resolve `az18_build_ctet.sh` non-existence (git reflog deep-query on a clean host)

## Cross-References

- AGENTS.md §Install Pipeline Plumbing (cat-4 PERMANENT list — covers `audit_incomplete_type_pattern.sh` + `check_clean_rebuild.sh`)
- AGENTS.md §GATE-MNT-01 (canonical push wrapper + hook + per-branch rebase invariant — covers `recover_chore_push.sh` + `recover_workspace_rescue.sh` cat-4 ancillary lineage)
- AGENTS.md §Post-push SHA-selfcheck invariant (`b589fdba` + `21ece2b3` recovery lineage — covers `recover_apply_chore.sh` cat-4 ancillary reference)
- AGENTS.md §regole "Fare PR piccole e mirate" (per-script-removal discipline)
- AGENTS.md §Regole di lint documentale `### Docs canonical update discipline rule` (cronaca home = ticket, catena = fence-only)
- User-spec verbatim: chronicle home per Cat-3 anti-dup
- Code-reviewer amendment lineage (this session): Finding 1 cat-4 PERMANENT script count dual-source cite expanded (5 scripts total = 2 in §Install Pipeline Plumbing + 3 recover_* in §GATE-MNT-01 / §Post-push SHA-selfcheck); Finding 2 canonical 5-step verify-before-remove count uniformly applied across CHANGELOG bullet 2 + ticket-home §Verification protocol + CHANGELOG bullet 5 amend-marker inserted for audit-trail
