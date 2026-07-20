# TICKET-TEXT-PRODUCTION-STATUS-CORRECTION — Text V1 area state split (Rendering Core PASS + CapCut-grade PARTIAL)

> **Status:** OPEN (cat-3 anti-dup tracker per i 10 forward-points verso Text Production / CapCut-grade V1 PARTIAL → PASS).
>
> **Area:** docs (CURRENT_STATUS state hygiene) + Text V1 (forward direction).

## Problema

`docs/CURRENT_STATUS.md` presentava storicamente un'unica row `Text Production V1 | PARTIAL` (post-riga-31) che **nascondeva due stati osservabili distinti**:

1. **Text Rendering Core V1** — FreeType + HarfBuzz + FriBidi + shaping + layout + glyph cache + animator + selector — **PASS** verificato per AGENTS.md v0.1 §"Mantenere baseline verde" (`main@7eb5c2ba` 11/11 baseline certificata; Text V1 Cert Step 6 + Text Health PASS; Text Export V1 certified `main@3fa5880f`).
2. **Text Production / CapCut-grade V1** — i 6 criteri canonici di `docs/RELEASE_GATE.md` §Text Production V1 (20 preset generali + 8 subtitle, SRT/word-timing, per-word highlight, goldens 16:9 + 9:16, determinism, SDK consumer) — **PARTIAL** (5/20 general presets, 0/8 subtitle presets, no tracked golden PNGs, no SRT/word-timing, per-word highlight not wired).

La confusione tra i due livelli ha prodotto due classi di failure mode §honesty rilevate:
- **Mis-stima PASS**: la row PARTIAL veniva letta come "Text V1 non è pronto" nonostante il rendering CERTIFIED (`Text Export V1 certified` testuale nella cella Note).
- **Mis-stima rotto Production**: il PARTIAL veniva letto come "tutto rotto" nonostante 5/20 preset pronti + Text Export V1 certificato + Text Health PASS + Text Blocco 5.1 (deprecation ACTIVE) done.

## Soluzione (chore atomico 2026-07-15)

Split della row singola in due row separate (cat-3 minimal-surface, 1-line doc-only each):

- **Row 1**: `| Text Rendering Core V1 | PASS | FreeType + HarfBuzz + FriBidi + shaping + layout + glyph cache + animator + selector certified; vedi [TICKET] |`
- **Row 2**: `| Text Production / CapCut-grade V1 | PARTIAL | Text Export V1 certified; 5/20 general presets, 0/8 subtitle presets, no tracked golden PNGs, no SRT/word-timing, per-word highlight not wired; vedi [TICKET] |`

Cronaca estesa vive in questo ticket-home (Cat-3 anti-dup); canonical `CURRENT_STATUS.md` conserva ≤1 riga sintetica + ticket-pointer per AGENTS.md v0.1 §`### Docs canonical update discipline rule`.

## Acceptance criteria — Sub-area level

### Text Rendering Core V1 → PASS (già osservato)

- FreeType + HarfBuzz + FriBidi integrati.
- Shaping + layout + glyph cache + animator + selector certificati al commit `main@7eb5c2ba` (11/11 baseline) + `main@3fa5880f` (Text Export V1).
- AGENTS.md §"Mantenere baseline verde" preservation invariant: nessuna regressione introdotta dal changeset corrente.

### Text Production / CapCut-grade V1 → PASS (forward direction)

I 10 sub-criteria di accettazione sono elencati nella sezione **Forward-points** qui sotto, ciascuno con il proprio forward-point ticket (cross-link canonico AGENTS.md §`### 2×-in-one-chore`).

## Forward-points (verso i 10 sub-criteria di CapCut-grade V1 → PASS)

| # | Sub-criterio | Forward-point ticket | Stato corrente |
|---|---|---|---|
| 1 | API unica (`TextDefinition` only, zero `TextSpec` callers) | [TICKET-TEXT-SPEC-MIGRATION](TICKET-TEXT-SPEC-MIGRATION.md) + [TICKET-TEXT-SPEC-FORWARDER-REMOVAL](TICKET-TEXT-SPEC-FORWARDER-REMOVAL.md) | OPEN P1 |
| 1b | `centered_text` / `glow_text` legacy removal | [TICKET-CENTERED-TEXT-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION.md) + [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) | OPEN P1 |
| 2 | Preset responsive (zero 1920×1080 hardcoded; `CanvasInfo + SafeArea + TextBoxConstraints + AspectRatioPolicy` per preset) | (deferred — nessun ticket aperto, da aprire in chore dedicato) | PLANNED |
| 3 | Rich text end-to-end (multi-font in un paragrafo senza layer multipli; legature + shaping contestuale) | (deferred) | PLANNED |
| 4 | Subtitle production (`SubtitleTrack/SubtitleCue/TimedWord/WordStyleState` typed; SRT/VTT/JSON adapters; ≥8 subtitle presets) | (deferred — work-in-progress nel working tree, vedi `src/scene/subtitle_track_builder.cpp` + `tests/text/test_subtitle_productive.cpp`) | PLANNED |
| 5 | Highlight word-level (fill + scale + stroke + background della parola attiva; random access frame-deterministico) | (deferred) | PLANNED |
| 6 | Animation authentica (`word_cascade` via `TextSelectorUnit::Word`, `character_cascade` via `Grapheme`; Wiggly/Wave migrati nel selector evaluator comune) | (deferred) | PLANNED |
| 7 | Font management (resolver + fallback + checksum + fail-loud + CWD ostile + due runtime + asset packaging) | [TICKET-TEST-FONT-ASSET-PATH](TICKET-TEST-FONT-ASSET-PATH.md) | OPEN P1 |
| 8 | Golden matrix (ogni preset 16:9 + 9:16, 3 timestamp, testo corto/lungo, sfondo chiaro/scuro, scala normale/estrema, cache fredda/calda, scheduler seriale/parallelo) | [TICKET-TEXT-VISIBILITY-PIPELINE](TICKET-TEXT-VISIBILITY-PIPELINE.md) (FU01..FU13) + [TICKET-TEXT-CLIP-GOLDENS-01-05](TICKET-TEXT-CLIP-GOLDENS-01-05.md) | OPEN/TRACKED |
| 9 | Determinism matrix (seriale + parallelo, cache on/off, random frame access identici) | [TICKET-REVEAL-PLACEMENT-CANONICAL](TICKET-REVEAL-PLACEMENT-CANONICAL.md) + [TICKET-FOLLOWUP-PRECEDENT-DOCS](TICKET-FOLLOWUP-PRECEDENT-DOCS.md) | OPEN |
| 10 | SDK consumer end-to-end (PNG + video reali su consumer esterno) | (cat-1/2 deferred — nessun ticket aperto) | PLANNED |

## Chore commit cross-link (SHA inline per AGENTS.md §`### SHA cite pattern`)

Questo chore è committato atomicamente (date 2026-07-15) sul working tree durante la sessione corrente. Lo SHA verrà citato qui al commit-time come append-only forward-point (post-push, AGENTS.md §`### Post-push SHA-selfcheck invariant`).

## Cross-references

- [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) §Stato generale per area → due row: Text Rendering Core V1 (PASS) + Text Production / CapCut-grade V1 (PARTIAL).
- [`docs/RELEASE_GATE.md`](../RELEASE_GATE.md) §Text Production V1 — i 6 criteri canonici di release (20 preset generali + 8 subtitle + SRT/word-timing + per-word highlight + 16:9/9:16 goldens + determinism + SDK consumer).
- [`docs/FEATURES.md`](../FEATURES.md) §Testo §Parziali + §Pianificate — feature inventory che alimenta i 10 sub-criteria (timed text/SRT/word timing ancora Pianificate; karaoke + subtitle layout policies Pianificate; etc.).
- [`docs/CHANGELOG.md`](../CHANGELOG.md) — cite-only entry prepended at TOP del commit di questo chore (cat-5 3-doc atomic per AGENTS.md §`### Docs canonical update discipline rule`).
- [`tools/check_current_status_table_shape.sh`](../../tools/check_current_status_table_shape.sh) — `EXPECTED_LABELS` aggiornato per la nuova label `Text Rendering Core V1` + substring `Text Production` (compatibile con `Text Production / CapCut-grade V1`).
- [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) — questo ticket NON viene aggiunto a §Open Blockers perché è docs-only e non introduce blocker nuovo; rimane canonical cronaca-home.
- AGENTS.md v0.1 §"Disciplina di aggiornamento dei canonici" — `CURRENT_STATUS.md` update "Solo quando cambia lo stato presente di un'area (PASS/FAIL/PARTIAL/NOT RUN)"; l'elevazione `Text Rendering Core V1 → PASS` è una nuova area-state assertion (qualifica come state change, motivando l'edit).
- AGENTS.md v0.1 §`### Docs canonical update discipline rule` — cronic extends in ticket-home, canonicals conservano ≤1 riga sintetica + pointer.
- AGENTS.md v0.1 §`### 2×-in-one-chore` — forward-point tickets raggruppati atomicamente con l'edit docs (anche se qui solo cronaca, non blocker).
- AGENTS.md v0.1 §`### SHA cite pattern` — SHA inline (non standalone catalogue): questo ticket aggiorna lo SHA al commit-time.

## Verification record (post-chore)

- `rg "Text Rendering Core V1" docs/CURRENT_STATUS.md` → 1 match (riga Rendering Core).
- `rg "Text Production / CapCut-grade V1" docs/CURRENT_STATUS.md` → 1 match (riga Production row).
- `rg "Text Production V1" docs/CURRENT_STATUS.md` → 2 match (entrambi i nomi contengono "Text Production V1" come substring; canonical coverability preservata per retro-compatibilità con altri documenti che lo citano).
- `bash tools/check_current_status_table_shape.sh` → exit 0 (gate compatibility verificato).
- `git diff --numstat` chore → 4 file modificati: `docs/CURRENT_STATUS.md` (`+2 -1`), `docs/CHANGELOG.md` (`+8 -0`), `docs/tickets/TICKET-TEXT-PRODUCTION-STATUS-CORRECTION.md` (`+80 -0` NEW), `tools/check_current_status_table_shape.sh` (`+2 -1`).

## Cronologia

- 2026-07-12: original row creata (Text Production V1 PARTIAL observation — single row observation).
- 2026-07-15: chore corrente — questa scheda ticket-home aperta + state split applicato a CURRENT_STATUS + gate expect_labels aggiornate + CHANGELOG cite-only entry.
