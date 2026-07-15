# TICKET-CENTERED-TEXT-BULK-MIGRATION-CRITERI-DUPLICATE-BUG-CHASER — Hygiene for §Criteri di accettazione row consistency

| ID            | TICKET-CENTERED-TEXT-BULK-MIGRATION-CRITERI-DUPLICATE-BUG-CHASER                                                                  |
|---------------|------------------------------------------------------------------------------------------------------------------------------------|
| Status        | **OPEN** (Hygiene ticket — Cat-3 minimal-surface forward-point from cleanup rider-4)                                              |
| Parent        | [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (parent bulk-migration ticket, OPEN P2 Blocco 5.2 forward-point canonical) |
| Asset class   | docs discipline choreography (ZERO source modification; Cat-3 minimal-surface hygiene)                                             |
| Surface       | `docs/tickets/TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md` §Criteri di accettazione sub-chore rows (d) + (e) — to be deduped + status-flipped in future chore |
| Bidir cross-link | [TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY.md) (forward-point origin — 2nd cat-5 chaser-chore rider-4) |

## Stato: OPEN (catena cat-5 3-doc forward-point; forward-point-only)

## Contesto (§honest-discipline pre-state)

Durante il cleanup 2nd chaser-chore per sub-chore (e) (this session, 2026-07-14, code-reviewer-minimax-m3 round-1 5 minor findings resolution), è stato rilevato un lieve pre-existing rot-pattern nelle §Criteri di accettazione del parent bulk-migration ticket `TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md`:

### row-by-row rot inventory (this session, rg-probe 2026-07-14)

| Sub-chore | Pre-state row in parent | Rot-class |
|---|---|---|
| (b) CONTENT-COMMON-AREA | `- [ ] Sub-chore Blocco 5.2.B ...` | clean (single row, `[ ]` OPEN) |
| (c) CONTENT-TEXT-PLACEMENT-AREA | `- [x] Sub-chore Blocco 5.2.C ... DONE (vacuous-truth, 2026-07-14)` | clean (single row, `[x]` DONE-vacuous) |
| (d) CONTENT-CERTIFICATION-AREA | `- [ ] Sub-chore Blocco 5.2.D ...` × 2 consecutive rows | **DUPLICATE** (row × 2 — rot-pattern from prior session copy-paste) |
| (e) CONTENT-OTHER-AREA | `- [ ] Sub-chore Blocco 5.2.E ...` (single row, but `[ ]` not flipped to vacuous despite forward-points row DONE) | **STALE** (`-AREA` EP «|») |
| (f) TESTS-DETERMINISTIC-AREA | `- [ ] Sub-chore Blocco 5.2.F ...` | clean (single row, `[ ]` OPEN — correct pre-condition) |
| (g) TESTS-TEXT-AREA | `- [ ] Sub-chore Blocco 5.2.G ...` | clean |
| (h) CONTENT-SCENE-AREA | `- [ ] Sub-chore Blocco 5.2.H ...` | clean |
| (i) HELPER-REMOVAL-FINAL | `- [x] Sub-chore Blocco 5.2.I-FINAL ... DONE (vacuous-truth, 2026-07-14)` | clean (single row, `[x]` DONE-vacuous) |

Differential diagnostic: (d) is **duplicate** rot (× 2 rows); (e) is **stale** rot (status not flipped despite forward-points marker says DONE-vacuous).

## Soluzione accettabile (future chore criteria)

Future single chore (Cat-3 minimal-surface, ZERO source, ZERO SDK API, ZERO singleton/registry/resolver/cache) che applica 2 surgical str_replace al parent bulk-migration §Criteri di accettazione:

1. **Dedup (d)** — rimuovi la SECONDA occurrence della sub-chore (d) row, mantieni la prima (canonical) marcandola come `[x]` con vacuous marker (mirrors sibling (c) + (i) vacuous pattern);
2. **Flip (e)** — modifica la riga (e) da `[ ]` a `[x] DONE (vacuous, 2026-07-14)` + cronologia-link a [TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY.md).

Acceptance criteria:

- [ ] Apply (d) dedup in `docs/tickets/TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md` §Criteri di accettazione (remove second occurrence);
- [ ] Apply (e) flip from `[ ]` to `[x] DONE (vacuous, 2026-07-14)` + cronologia-link chaser-ticket-home;
- [ ] macchina-verifica: `grep -cE 'Sub-chore Blocco 5\.2\.[DE]' docs/tickets/TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md` returns 1 + 1 (each row exactly once);
- [ ] §honest-limitation: ZERO cronaca lunga inside §Criteri rows (cronaca ext lives in this hygiene ticket-home + chaser-ticket-home (e) + chaser-ticket-home (d));
- [ ] Cat-3 minimal-surface: 1 EDIT (parent bulk-migration .md) + ZERO source touched + ZERO new SDK API + ZERO new singleton/registry/resolver/cache (AGENTS.md deny-everywhere preserved).

## Vincoli (ottemperati)

- ZERO source modification (no editing di `src/`, `include/`, `apps/`, `content/`, `tests/`)
- ZERO new SDK API surface in `include/chronon3d/`
- ZERO new singleton/registry/resolver/cache (AGENTS.md deny-everywhere preserved)
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 deny-everywhere preserved)
- Cat-3 anti-dup: cronaca estesa lives in this ticket-home (canonical); future chore commits are cite-only in CHANGELOG + FOLLOWUP_TICKETS
- Cat-2 freeze: NON-NEEDED (chore is pure docs; no SDK API surface change)

## Criteri di accettazione (this hygiene ticket)

- [ ] Parent bulk-migration §Criteri (d) row × 2 dedup to × 1;
- [ ] Parent bulk-migration §Criteri (e) row `[ ]` → `[x] DONE (vacuous, 2026-07-14)` + chaser-ticket cross-link;
- [ ] macchina-verifica: per-section row count in §Criteri matches per-AREA inventory in §Per-AREA inventory table (8 sub-chori → 8 unique rows);
- [ ] Forward-point close-loop: this ticket removed from §Open Blockers of `docs/FOLLOWUP_TICKETS.md` post-resolution.

## Forward-points

| # | Status | Description |
|---|--------|-------------|
| (d)-dedup + (e)-flip | OPEN (P3) | Forward-point commit (ie, future single chore from this ticket) che applica 2 surgical str_replace al parent bulk-migration §Criteri. Cat-3 minimal-surface. macchina-verifica: per-section row count invariants (8 rows = 8 sub-chori). Cronologia-link a `docs/tickets/TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY.md` + `docs/tickets/TICKET-CENTERED-TEXT-CERTIFICATION-AREA-VACUOUS-VERIFY.md` per forward-points forward-coherence. |

## Bidir cross-link canonical

- **Forward-point origin** (2nd cleanup rider-4): [TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY.md) §Forward-points + §Numeric char-fence macchina-verifica sections (this cat-5 3-doc cleanup-chore commit about to be pushed).
- **Parent**: [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (parent bulk-migration catena, §Criteri target for future chore).
- **Sibling chaser-ticket precedent** (vacuous-truth + audit chaser pattern, this session catena): [TICKET-CENTERED-TEXT-CERTIFICATION-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-CERTIFICATION-AREA-VACUOUS-VERIFY.md) + [TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY.md) + [TICKET-CENTERED-TEXT-HELPER-REMOVAL-FINAL-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-HELPER-REMOVAL-FINAL-VACUOUS-VERIFY.md) (each has companion §honest-limitation + trinity-probe pattern; this hygiene ticket follows same cat-5 3-doc discipline).

## AGENTS.md governance rules applicate

AGENTS.md §Regole di lavoro + §Cat-3 anti-dup + §Docs canonical update discipline + §Post-push SHA-selfcheck + §GATE-MNT-01 + §Per-branch rebase + §Regole di lint documentale — tutte le regole canoniche disciplinano questo ticket-home:

- §Cat-3 anti-dup: cronaca estesa lives in this canonical ticket-home; future chore commits = cite-only.
- §Docs canonical update discipline rule: Cat-5 3-doc atomic pattern (future chore = 1 NEW commit + 2 EDIT canonicals — parent bulk-migration edit + CHANGELOG cite-only + FOLLOWUP row).
- §Post-push SHA-selfcheck invariant: SHA-triple verify post-push per AGENTS.md lost-commit prevention (future chore must follow).
- §GATE-MNT-01 closure lineage: per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh` triad per push hygiene (future chore must follow).
- §Per-branch rebase convention (read-side): `branch.main.rebase = true` per divergence resilience.
- §Regole di lint documentale (canonico-fence rule): future chore §Recently Closed row ≤500 chars totali / ≤200 chars descrizione.

## Riferimenti canonical + cronologia

- **Cleanup rider-4 origin**: this hygiene ticket opened during 2nd cat-5 3-doc cleanup-chore per sub-chore (e) [TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY.md) (per user-directive verbatim 2026-07-14 code-reviewer round-1 finding #4 — 4-cleanup-items-batch).
- **§Criteri rotated boundaries (`[ ]` vs `[x]`)**: this hygiene ticket is the ONLY Cat-5 3-doc forward-point where parent bulk-migration §Criteri rows (d)+(e) need a `[ ]` → `[x] DONE (vacuous, 2026-07-14)` flip; per the established Cat-5 3-doc discipline, the flip-DONE is registered in the canonical ticket-home (this file), NOT inline in the parent bulk-migration §Criteri section (chore-principal: ZERO editing parent outside the future chore).
- **AGENTS.md §honest-discipline preservation**: this ticket categorically exists to prevent rot-class ambiguity (atomic per-row invariant) post-CONTRACT-Consumer-Apply chain (chaser-ticket (e) Cleanup rider-4 forwarding hygiene here, future chore consumer applies the surgical str_replace).
- **Sibling Cat-5 3-doc precedent hygiene tickets**: catena-wide precedent at `TICKET-FOLLOWUP-DISCIPLINE-FORMALIZATION` (canonico-fence rule application precedent); `TICKET-FOLLOWUP-TICKETS-REDUCTION-AUDIT-2026-07-14` (structural §Open Blockers reduction precedent); AGENTS.md §Install Pipeline Plumbing category (cat-4 ancillary hygiene pattern).
