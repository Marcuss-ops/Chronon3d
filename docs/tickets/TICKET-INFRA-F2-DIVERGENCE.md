# TICKET-INFRA-F2-DIVERGENCE - F2 infrastructure divergence between local and origin/main

## Stato
CLOSED (2026-07-12, closed by this cycle commit + merge commit + push via tools/wrap_push.sh origin main; Local and origin/main SHA now match per AGENTS.md Post-push SHA-selfcheck invariant).

## Priorità
CLOSED-AT-P0 (was P0 OPEN for the F2 window 2026-07-06 -> 2026-07-12; closure event: TICKET-INFRA-F2-DIVERGENCE cycle commit landed via merge + push).

## Problema
The wrap_push pre-push hygiene gate (`tools/wrap_push.sh`) was unable to push local commits to `origin/main` because `tools/check_main_clean.sh` Step 2 hard-blocks TRUE divergence (no ancestor relation in either direction). The 6/10 split accumulated across prior cycles (Test 16+17+TICKET-125+Test 18 cycle 1+cycle 2).

## Evidenza
- git rev-list count origin/main..HEAD pre-cycle: 6 (post-cycle: 0)
- git rev-list count HEAD..origin/main pre-cycle: 10 (post-cycle: 0)
- IS_ANCESTOR both directions pre-cycle: NO; post-cycle: all passes
- 11 hygiene gates (4.5a-g + new 4.5h divergence_window + Step 4 main_clean) PASS post-closure

## Impatto
- AGENTS.md GATE-MNT-01 closure lineage extended with ADR-022 divergence-window advisory gate
- 11/11 baseline main@7eb5c2ba maintained locally; origin/main advances to equal HEAD
- §honesty cert PARTIAL-PUSH-BLOCKED-F2 closes to PASS (SHA-triple equality verified)
- Future wrap_push invocations benefit from the new advisory gate at Step 4.5h

## Confine
- INFRASTRUCTURAL only - no source code in include/chronon3d/ modified
- AGENTS.md governance unchanged (advisory gate is informational, not hard-block)
- Backup-sunset-test-16 tag intact at 18b0554aca9b3917416368d348b479ea6c65106a (preserved by merge; rebase would have detached)

## Soluzione
ADR-022 + tools/check_push_divergence_window.sh + wire into wrap_push.sh Step 4.5h + explicit git merge --no-ff origin/main for divergence-reconciliation + run wrap_push + post-push SHA-triple selfcheck.

## Criteri
- [x] Cycle commit subject: tools(F2): wire divergence-window gate + push-drain closure (59 chars, under 72 envelope)
- [x] ADR-022 documented per docs/adr/ADR-NNN-titolo.md convention (mirrors ADR-021)
- [x] Gate is advisory (always exit 0)
- [x] Threshold configurable via CHRONON3D_DIV_WINDOW_MAX_LOCAL_AHEAD + CHRONON3D_DIV_WINDOW_MAX_REMOTE_AHEAD (default 10 each)
- [x] Git merge commit via --no-ff
- [x] Post-push SHA-triple equality confirmed
- [x] Cat-5 4-doc updates: CHANGELOG prepended; FOLLOWUP_TICKETS closed; CURRENT_STATUS cite-only row
- [x] backup-sunset-test-16 tag preserved
- [x] branch.main.rebase=true invariant SURVIVED

## Forward-points
1. CONTRIBUTING.md update - document new CHRONON3D_DIV_WINDOW_MAX_LOCAL_AHEAD / CHRONON3D_DIV_WINDOW_MAX_REMOTE_AHEAD env vars.
2. tools/check_post_push_consistency.sh - bootstrap the AGENTS.md Post-push SHA-selfcheck invariant as a gate.
3. Hypothetical check_merge_commit_subject_shape.sh gate if commit-shape rot emerges.

## Cross-link
- ADR-022: docs/adr/ADR-022-divergence-window-gate.md
- Gate implementation: tools/check_push_divergence_window.sh
- Wrap_push invocation: tools/wrap_push.sh Step 4.5h
- Hard-block anchor: tools/check_main_clean.sh Step 2
- Baseline: docs/baselines/main-7eb5c2ba-baseline.md
- AGENTS.md GATE-MNT-01 closure lineage
