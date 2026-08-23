# TICKET-COMMIT-SUBJECT-ENVELOPE-VIOLATION-F15 — Subject Envelope §Honesty Disclosure for F1.5 RETRY

## Stato

**DONE-FORWARD-POINT** (2026-07-13). Questo ticket è la §honesty disclosure
per la violazione AGENTS.md `tools/check_commit_subject_length.sh` (default
72-char hard limit) introdotta da F1.5 RETRY commit set.

## Priorità

P3 — non blocca 11/11 verde baseline. Forward-point per future amend chore
che riporta i due subject envelope a ≤ 72 chars.

## Problema

F1.5 RETRY (commit set `c9ddca86` + `c9ddca87` — pending final commit SHAs)
ha introdotto due subject envelope che eccedono il `tools/check_commit_subject_length.sh`
72-char AGENTS.md hard limit:

| Chore | Subject (verbatim) | Length | Limit | Δ |
|---|---:|---:|---:|---:|
| #1 | `test+chore(bench): bench.json + dashboard + baseline + ticket artifacts at 7eb5c2ba` | 89 chars | 72 | +17 chars over |
| #2 | `docs(fixforward): F1.5 CHANGELOG cite-only restore (BENCH-BASELINE-SHA-V1)` | 82 chars | 72 | +10 chars over |

User-pivot aveva proposto il verbatim subject envelopes con esplicito
**ACKnowledge della violation**: "Honor user spec verbatim (2 commits + 89-char
chore1)". AGENTS.md §honesty 3-state closure lineage:

- "Non segnare verde una suite che restituisce failure" — la §honesty closure
  impone di documentare la violation piuttosto che hide-it.
- "Quando un file sembra mancare, non inventare percorsi alternativi" — niente
  workaround per il gate-failure a push time.

## Soluzione adottata

Documenting la violation in 3 loci (Cat-5 3-doc closure pattern:

```text
docs/tickets/TICKET-COMMIT-SUBJECT-ENVELOPE-VIOLATION-F15.md     // questo ticket (cronaca humanities)
docs/CHANGELOG.md entry chore #2 metadata line                    // cite-only pattern
docs/tickets/TICKET-BENCH-BASELINE-SHA-V1.md §Honesty disclosure   // forward-point cross-link
```

Il ticket NON blocca il push F1.5 RETRY a upstream. Il push fallirà a
`tools/wrap_push.sh` GATE-MNT-01 step (pre-push `tools/check_commit_subject_length.sh`)
→ exit con `GATE_FAIL: commit subject '<sha>' exceeds 72-char limit (got N chars)`.
Failure-mode expected; non c'è attempt per hide-it tramite `--no-verify`
o Bypass del gate.

## Criteri di accettazione

| # | Criterio | Stato (post-implementation) |
|---|---|---|
| 1 | Forward-point ticket created | PASS (this file) |
| 2 | §Honesty disclosure presente in TICKET-BENCH-BASELINE-SHA-V1.md | PASS (cronaca humanities) |
| 3 | Forward-point precedent a `TICKET-LOST-COMMIT-WORKSPACE-RESCUE`-style §honesty closure | PASS (Cat-5 3-doc closure) |
| 4 | Subject envelopes F1.5 RETRY commit set NON amended post-commit (preserva verbatim user-pivot) | PASS (violation preserved) |

## Forward-points

- **TICKET-COMMIT-SUBJECT-ENVELOPE-VIOLATION-F15-AMEND** — future amend chore
  che riporta i due subject envelope a ≤ 72 chars preservando il payload
  semantico (e.g., shorten `bench.json + dashboard + baseline + ticket artifacts`
  → `4 bench artifacts` per chore #1; `(BENCH-BASELINE-SHA-V1)` può essere
  omesso se ridondante col body commit message di secondo livello). Forward-point
  cronaca qui. NOT in scope per questo commit set.
- **TICKET-PUSH-CADENCE-OPTIMIZATION** (parallel F-PRAGMA commit `c9ddca87`) —
  wrapper `tools/recover_apply_chore.sh` può essere invocato per amend
  chronaca all'interno dello stesso commit set (preserva `fixforward:` style
  semantic); NON sostituisce questo ticket clap.
- **TICKET-CHECK-COMMIT-SUBJECT-LENGTH-PRECOMMIT-HOOK** — parallel precedent
  to `tools/check_main_clean.sh` write-side enforcement. Instradato come
  forward-point cronaca al addestramento del pattern `tools/wrap_push.sh`
  pre-push check (GATE-MNT-01 closure lineage).

## Cross-link canonici

- [`docs/tickets/TICKET-BENCH-BASELINE-SHA-V1.md`](TICKET-BENCH-BASELINE-SHA-V1.md)
  — §Honesty disclosure cross-link.
- [`tools/check_commit_subject_length.sh`](../../tools/check_commit_subject_length.sh) — gate SSoT.
- [`AGENTS.md` §TICKET-GATE-SUBJECT-RANGE](../../AGENTS.md) — chiusura 2026-07-12 del
  envelope 72-char rule + cat-3 lint discipline.
- [`AGENTS.md` §Post-push SHA-selfcheck invariant](../../AGENTS.md) — write-side
  belt-and-suspenders per WRITE-side charge push-discipline; companion al
  READ-side triad (GATE-MNT-01 closure lineage).
- [`docs/CHANGELOG.md`](../../docs/CHANGELOG.md) — chore #2 metadata cite-only.

## Origine

Questo ticket forward-point segue il pattern Cat-5 3-doc closure perp tuato
da TICKET-BUILD-ROT-CASCADE-CAMERA + TICKET-SOURCE-CONFLICT-MARKERS-ROT
(2026-07-12): ogni volta che una regola AGENTS.md viene violata knowingly,
il ticket forward-point + CHANGELOG prepended + cronaca ticket principale
preservano la trasparenza piuttosto che hide-it.
