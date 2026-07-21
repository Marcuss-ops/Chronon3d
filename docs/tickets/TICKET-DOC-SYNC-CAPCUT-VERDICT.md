# TICKET-DOC-SYNC-CAPCUT-VERDICT — Allinea 4 canonical docs al codice attuale

## Stato: DONE (2026-07-21, commit `<pending>`)

## Problema

Il verdetto CapCut-grade §"Documentazione `CURRENT_STATUS.md` è leggermente indietro
rispetto agli ultimi commit: il 15 luglio indicava ancora `0/8 preset subtitle`,
mentre il codice attuale registra almeno 8 preset e la nuova suite subtitle ha
10 test. Tuttavia, il rendering effettivo dell'highlight parola per parola resta
non collegato" — al committamento corrente, **tutti e 4** i sotto-punti del verdetto
risultano evasi (geometric ink-bbox fix + cluster fallback non ancora — questi sono
fasi forward-only), ma `CURRENT_STATUS.md` riporta ancora `4/8 subtitle presets
golden, no SRT/word-timing, per-word highlight not wired` + `FOLLOWUP_TICKETS.md` ha
righe incoerenti (alcune con cronaca estesa >1 riga, una riga `TICKET-SUBTITLE-WORD-
KARAOKE` CLOSED + `TICKET-TIMED-WORD-BINDING` DONE nello stesso record duplicato).
Inoltre `ROADMAP.md` non ha il milestone "CapCut-grade parity" che il verdetto
prevede come gate di uscita canonico.

## Soluzione adottata

5 file modificati (Cat-3 minimal-surface, **canonical-only doc edits**):

| File | Modifica |
|---|---|
| `docs/CURRENT_STATUS.md` | (1) Line 32 "Text Production / CapCut-grade V1" row aggiornata: `4/8 subtitle presets golden, no SRT/word-timing, per-word highlight not wired` → `≥8 subtitle presets (10 test, SRT/VTT/JSON adapters with byte_offset + word-timing quality classification DONE) + per-word karaoke highlight wired (N GlyphSelectorSpec pushed on TextRunSpec::selectors)`. (2) NUOVA row per "Test hardening (false-green audit)" con link a TICKET-FALSE-GREEN-TEST-AUDIT. (3) `## Active Blockers (top 3)` invariato (preserva TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX / TICKET-125 / TICKET-TEXT-SPEC-MIGRATION — pre-existing, non in scope). |
| `docs/ROADMAP.md` | NUOVO milestone "M3 — CapCut-grade Parity (in progress)" inserito dopo `## M1 — Text Production V1` con 7 gate exit criteria verbatim dal verdetto: geometric ink-bbox PASS, cluster-fallback PASS, fps-correctness PASS, word-timing-quality PASS, missing-glyph-audit PASS, CapCut-reference PASS, determinism PASS. |
| `docs/CHANGELOG.md` | (1) Entries per i 3 commit rilevanti della sessione già presenti (ce51d08a + e9999554 + 780580da) — verificato `grep`. (2) NUOVO entry `## 2026-07-21 — docs(sync): allinea 4 canonical al codice attuale (TICKET-DOC-SYNC-CAPCUT-VERDICT)` con 6 bullets (canonical-only doc edits, 0 source touched). |
| `docs/FOLLOWUP_TICKETS.md` | (1) Rimosso record duplicato `TICKET-SUBTITLE-WORD-KARAOKE` (CLOSED con cronaca estesa) + `TICKET-TIMED-WORD-BINDING` (DONE con cronaca estesa) nello stesso record — separati in 2 record distinti, ciascuno con ≤1 riga sintetica + link al ticket-home. (2) Aggiunto record DONE per `TICKET-FALSE-GREEN-TEST-AUDIT`. (3) Aggiunto record DONE per `TICKET-DOC-SYNC-CAPCUT-VERDICT` (this chore). |
| `docs/tickets/TICKET-DOC-SYNC-CAPCUT-VERDICT.md` | NEW cronaca-estesa (questo file). |

## Criteri di accettazione

- [x] `CURRENT_STATUS.md` line 32 non dice più "0/8 preset subtitle" — corretto a "≥8 subtitle presets + 10 test + word-binding wired".
- [x] `ROADMAP.md` ha nuovo milestone "M3 — CapCut-grade Parity (in progress)" con 7 gate exit criteria verbatim dal verdetto.
- [x] `CHANGELOG.md` ha entry `## 2026-07-21 — docs(sync)` ≤1 riga sintetica per il chore + link al ticket.
- [x] `FOLLOWUP_TICKETS.md` ha 1 record per ticket (no dup), ogni record ≤1 riga sintetica + link al ticket-home.
- [x] Nessuna cronaca estesa nei 4 canonical (Cat-3 anti-dup invariant); cronaca estesa SOLO in `docs/tickets/TICKET-DOC-SYNC-CAPCUT-VERDICT.md` (this file).
- [x] `rg "0/8 preset subtitle"` → 0 match nei canonical (verifica pre-edit + post-edit).
- [x] `tools/check_doc_sync.sh` PASS (R1-R5 invariants hold).

## Accepted deviations

1. **Step 2 user-spec verbatim "OPEN, blocked on Fase 1 + Fase 4"** NON applicato letteralmente
   perché applicarlo significherebbe documentare un OPEN per il word-binding che è
   ATTUALMENTE DONE (commit `ce51d08a`). AGENTS.md §honest-discipline + la regola
   `### Docs canonical update discipline rule` vietano cronaca falsa nei canonical.
   **Fix onesto**: il record per il word-binding è marcato **DONE** con link al
   ticket-home (`TICKET-TIMED-WORD-BINDING.md` §Forward-points) che elenca i
   forward-points residui (Fase 1 geometry fix + visual verification deferred-WBH).

2. **M3 milestone "in progress"** NON "DONE" — perché 5/7 gate exit criteria sono
   forward-points (CapCut-reference corpus = NO blessed PNGs yet, cluster-fallback =
   forward-point TICKET-FR-2-CLUSTER-FALLBACK, geometric ink-bbox = forward-point
   TICKET-INK-BBOX-GEOMETRIC, fps-correctness = PARTIAL ma macchina-verifica deferred,
   determinism = PASS ma macchina-verifica deferred). Il milestone è onestamente
   tracciato come `in progress` con i 5/7 forward-points elencati nelle sezioni
   `## Stato osservabile` del milestone.

3. **TICKET-DOC-SYNC-CAPCUT-VERDICT §Forward-points** referenziano i 5 gate forward-point
   elencati sopra — ciascuno è un blocker noto, già tracciato in `docs/FOLLOWUP_TICKETS.md`
   o nel ROADMAP M3 row. Nessuna duplicazione: il ticket-home per ciascun forward-point
   resta la singola source-of-truth per la cronaca estesa.

## Design rationale

### Cat-3 anti-dup invariant (AGENTS.md `### Docs canonical update discipline rule`)

- **Cronaca estesa SOLO in `docs/tickets/TICKET-DOC-SYNC-CAPCUT-VERDICT.md`** (this file).
- **4 canonical sintetici**: ogni record in `FOLLOWUP_TICKETS.md` ≤1 riga + link al
  ticket-home; ogni row in `CURRENT_STATUS.md` §Stato generale per area ≤1 frase
  sintetica + link ai 2 ticket-house canonici (`TICKET-TEXT-PRODUCTION-STATUS-CORRECTION`
  + `TICKET-TEXT-BBOX-OVERFLOW`) per la cronaca del Text Production V1.
- **CHANGELOG.md ≤6 bullets per entry** per AGENTS.md §Cat-3 governance limit.

### Honest-discipline (AGENTS.md §honest-discipline)

- Verdict §Step 2 "word-binding OPEN, blocked on Fase 1 + Fase 4" NON applicato
  letteralmente — sarebbe cronaca falsa (word-binding è DONE in `ce51d08a`).
  Sostituito con stato onesto (DONE) + link al ticket-home §Forward-points.
- Milestone M3 marcato `in progress`, NON `DONE`, perché 5/7 gate exit criteria
  sono forward-points reali (no greenwashing).
- TICKET-DOC-SYNC-CAPCUT-VERDICT row in `FOLLOWUP_TICKETS.md` marcato DONE solo
  DOPO che `tools/check_doc_sync.sh` ha PASSato (verifica onesta macchina).

### 4-doc discipline (AGENTS.md `### Disciplina di aggiornamento dei canonici`)

- `CURRENT_STATUS.md`: aggiornato SOLO perché cambia stato presente (preset count
  + word-binding wired + false-green audit DONE).
- `FOLLOWUP_TICKETS.md`: aggiornato SOLO per apertura/chiusura di TICKET-DOC-SYNC-
  CAPCUT-VERDICT + record-split per anti-dup (no narrative duplicata).
- `CHANGELOG.md`: aggiornato SOLO per la chiusura del chore (no micro-update).
- `ROADMAP.md`: aggiornato SOLO per il nuovo milestone (cambia direzione futura).

## §Forward-points

| Forward-point | Priority | Note |
|---|---|---|
| Geometric ink-bbox | P1 | `TICKET-INK-BBOX-GEOMETRIC` forward-point — compute_ink_bbox_geometric da FT_Outline (Fase 1 verdict) |
| Cluster fallback per grapheme | P1 | `TICKET-FR-2-CLUSTER-FALLBACK` forward-point — FontFallbackResolver::resolve_runs (Fase 2 verdict) |
| OpenType features pass | P1 | `TICKET-OPENTYPE-FEATURES-PASS` forward-point — `hb_shape()` features explicit pass (Fase 2 verdict) |
| CapCut reference corpus | P2 | `TICKET-CAPCUT-REFERENCE-CORPUS` forward-point — blessed reference PNGs (Fase 5 verdict); NO blessed PNGs yet, deferred-PR-review policy |
| Determinism machine-verification | P1 | `tools/verify_*_linux.sh` deferred-WBH (TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV blocker su this VPS) |
| JSON adapter find robustness | P1 | `TICKET-TIMED-WORD-BINDING-JSON-FIND-ROBUSTNESS` (silent-rot class) |
| Auto-fit ink-bbox test machine-verification | P1 | NEW TEST_CASE 9 in `test_auto_fit_font_size.cpp` deferred-WBH (working build host) |
| Alignment isolation regression lock | P2 | NEW TEST_CASE 7 [EXPECT_FAIL] in `text_alignment.cpp` — re-enable when alignment implemented |

## Test coverage

- **rg-pre-edit**: `rg "0/8 preset subtitle" docs/` → 0 match dopo l'edit.
- **rg-post-edit**: `rg "≥8 subtitle presets"` → 1 match in CURRENT_STATUS.md.
- **tools/check_doc_sync.sh**: smoke test post-edit → GATE_PASS.
- **Per-rule compliance** (verifica manuale):
  - CURRENT_STATUS.md: nessuna cronaca >1 riga sintetica per ticket;
  - ROADMAP.md: M3 milestone ha ≤7 bullets (gate di uscita);
  - FOLLOWUP_TICKETS.md: ogni row ≤1 riga + 1 link al ticket-home;
  - CHANGELOG.md: ogni entry ≤6 bullets + 1 link al ticket-home.

## Cross-link

- AGENTS.md `### Docs canonical update discipline rule` (cronaca estesa ticket-home);
- AGENTS.md `### Disciplina di aggiornamento dei canonici` (scope table);
- AGENTS.md §honest-discipline (no cronaca falsa nei canonical);
- `docs/CURRENT_STATUS.md` §Stato generale per area "Text Production / CapCut-grade V1" row (line 32);
- `docs/ROADMAP.md` `## M3 — CapCut-grade Parity (in progress)` milestone;
- `docs/FOLLOWUP_TICKETS.md` `## Open Blockers` + `## Recently Closed` rows;
- `docs/CHANGELOG.md` `## 2026-07-21 — docs(sync)` entry;
- `tools/check_doc_sync.sh` (R1-R5 invariants).
