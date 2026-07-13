# TICKET-LOST-COMMIT-WORKSPACE-RESCUE — Workspace lost-commit rescue gate

## Stato
OPEN (gate `tools/recover_workspace_rescue.sh` landato `5dedc34d`; FOLLOWUP-index split in P9f `docs(tickets): split WORKSPACE-RESCUE row into FOLLOWUP-index + trait spec`)

## Priorità
P2

## Problema
Forward-point da AGENTS.md §Post-push SHA-selfcheck invariant `cf673ecc`. Race-window divergence durante chore push può causare 4 failure-mode (auto-FF divergent silent + stale `@{u}` + GATE_FAIL misfire + multi-agent race window) che portano a lost-commit pattern anche se `tools/wrap_push.sh` exit 0 (§honesty "NECESSARY but NOT SUFFICIENT").

## Evidenza
- `b589fdba` 3-attempt recovery (2026-07-12) — lost-commit pattern bit il chore che intendeva prevenirlo;
- `21ece2b3` + `cf673ecc` + `0299a042` (P9e) + `5dedc34d` (P9g) unique-edit recovery lineage — 4 cycle race-window + rebase-drop semantic-identical + preserve unique via `reset --hard @{u}` + cherry-pick;
- 5 attempts bloccati in P10 session per upstream churn density (P10 §honest-limitation `non iterare rebase a terzo tentativo` boundary).

## Impatto
Manutenibilità del push iterativo: senza un WRITE-side gate che codifichi lo SHA-triple selfcheck + cherry-pick instruction, il "pushed" exit 0 è un segnale ambiguo e la recovery diagnostica richiede intervento manuale ripetuto.

## Confine
Solo gate cat-4 ancillary (`tools/recover_workspace_rescue.sh`); zero nuovi SDK API; zero nuovi public surface in `include/chronon3d/`.

## Soluzione accettabile
`tools/recover_workspace_rescue.sh` (~120 LoC bash). 2 recovery patterns: (1) silent-class lost-commit detection via SHA-triple + workspace-state `git status -s`; (2) unique-edit recovery via `git merge-base --is-ancestor HEAD '@{u}'` + cherry-pick instruction.

## Criteri di accettazione
- `bash -n` syntax-clean + `chmod +x` + dry-run 0/1/2 exit-code semantic macchina-verified;
- macchina-verify su working build host (deferred per AGENTS.md §honest-limitation);
- cat-4 ancillary parallel placement con 5 Lint-checkability forward-points.

## Cross-link
- AGENTS.md §Post-push SHA-selfcheck invariant (the justifying rule) + §Regole di lint documentale (gate additions pattern);
- `tools/recover_chore_push.sh` (dogfooded `6708f79f` precedent — WRITE-side belt-and-suspenders);
- 5 parallel-cat-4-ancillary forward-points: `check_info_diagnostic_style.sh` + `check_stale_build_pre_ctest.sh` + `check_duplicate_default_arg.sh` + `check_post_push_consistency.sh` + `check_docs_canonical_discipline.sh`.
