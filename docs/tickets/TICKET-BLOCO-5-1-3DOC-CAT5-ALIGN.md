# TICKET-BLOCO-5-1-3DOC-CAT5-ALIGN — Canonical 3-doc alignment chaser-chore post-Blocco 5.1 FF-sync

| ID            | TICKET-BLOCO-5-1-3DOC-CAT5-ALIGN                                                                              |
|---------------|--------------------------------------------------------------------------------------------------------------|
| Status        | **DONE** (2026-07-14, post-FF chase; this session)                                                           |
| Parent set    | [TICKET-CENTERED-TEXT-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION.md) (P2 OPEN) + [TICKET-TEXT-SPEC-MIGRATION](TICKET-TEXT-SPEC-MIGRATION.md) (P1 OPEN) — both Blocco 5.1 deprecation bridges |
| Asset class   | cat-2/3 documentation discipline (pure docs; ZERO source touched)                                            |
| Scope         | 1 NEW chaser-home + 4 EDIT docs (CURRENT_STATUS snapshot SHA + FOLLOWUP §Recently Closed + 2 ticket-home ## Phase tracking subsections) |
| Surface       | `docs/tickets/TICKET-BLOCO-5-1-3DOC-CAT5-ALIGN.md` (NEW) + `docs/CURRENT_STATUS.md` (EDIT snapshot) + `docs/FOLLOWUP_TICKETS.md` (EDIT §Recently Closed) + `docs/tickets/TICKET-CENTERED-TEXT-MIGRATION.md` (APPEND `## Phase tracking`) + `docs/tickets/TICKET-TEXT-SPEC-MIGRATION.md` (APPEND `## Phase tracking`) |

## Stato: DONE

## Problema (§honesty pre-state inventory)

I 4 commit portati in locale via fast-forward da `origin/main` (HEAD post-FF = `cc3ad1a3760335d913f1c8e2f6948fe35ee2eb40`):

| Commit | Subject |
|--------|---------|
| `b6397b90` | chore(text): deprecate centered_text + glow_text (Blocco 5.1) |
| `74c924b9` | chore(text): fix deprecation ticket refs + add §25 record (Blocco 5.1) |
| `bacbfc5a` | chore(text): fix-up-2: §10 numbering + cat-3 budget (Blocco 5.1) |
| `cc3ad1a3` | chore(text): fix deprecation markers and changelog (Blocco 5.1) |

Stato canonical PRIMA di questo chase:

| File | Stato pre-chase | Azione richiesta |
|---|---|---|
| `docs/CHANGELOG.md` | ✅ 4 entry prepended at TOP di `## 2026-07-14` (Cita-Only) | SKIP (Cat-3 anti-dup) |
| `docs/FOLLOWUP_TICKETS.md` | ✅ `TICKET-CENTERED-TEXT-MIGRATION` P2 + `TICKET-TEXT-SPEC-MIGRATION` P1 entrambi in §Open Blockers | EDIT §Recently Closed (Cita-Only row per questo chaser-chore) |
| `docs/ROADMAP.md` | ✅ M1.8 §2C + §5A + §2D DONE rows documentano Blocco 5.1 | SKIP (già coperto) |
| `docs/CURRENT_STATUS.md` | ❌ Snapshot SHA `5246d7bb` STALE (pre-FF), HEAD reale = `cc3ad1a37` | EDIT snapshot SHA |
| `TICKET-CENTERED-TEXT-MIGRATION.md` | ⚠️ Copre i criteri "helper rimosso" (Blocco 5.2 implicit) ma Phase tracking non esplicito | APPEND `## Phase tracking` (citation-only, cronaca estesa = this ticket-home) |
| `TICKET-TEXT-SPEC-MIGRATION.md` | ⚠️ Copre i criteri "overload rimosso" (Blocco 5.2 implicit) ma Phase tracking non esplicito | APPEND `## Phase tracking` (citation-only) |

Diagnosi: la copertura canonical Blocco 5.1 era ALREADY GREEN sul lato substance (CHANGELOG pre-pended + 2 ticket-home esistenti + ROADMAP M1.8 DONE). Il gap era puramente cronaca-extension: il forward-point "**Blocco 5.2 — rimozione completa**" era semanticamente coperto dai Criteri di accettazione ma non nominato esplicitamente nei ticket-home. Cat-3 anti-dup discipline richiede di NON duplicare la cronaca estesa nei canonical, ma di tenerla nel canonical ticket-home — questo chase è la canonical-home della cronaca Blocco 5.2 forward-point.

## VPS-side basher-scan ground-truth (verbatim, this session)

```
T1_4_COMMITS_FF               = 4 commit Blocco 5.1 in fast-forward delta (HEAD..@{u}) → b6397b90, 74c924b9, bacbfc5a, cc3ad1a3
T2_LOC_SHA_POST_FF            = cc3ad1a3760335d913f1c8e2f6948fe35ee2eb40 (post-FF, == origin/main)
T3_CURRENT_STATUS_SHA_STALE   = "main@5246d7bb" (CITED in 1 spot: snapshot block top-of-file)
T4_CHANGELOG_ENTRIES_PRESENT  = 3 Blocco 5.1 entries in docs/CHANGELOG.md under `## 2026-07-14`: b6397b90 + 74c924b9 + cc3ad1a3 (= "fix deprecation markers and changelog") — the 4th commit bacbfc5a merge-bundle non ha entry separata perché già assorbita nel rewrite del changelog (cc3ad1a3 verbatim subject)
T5_FOLLOWUP_TICKETS_ROWS      = "TICKET-CENTERED-TEXT-MIGRATION | P2 | Text | OPEN | dev | ..." + "TICKET-TEXT-SPEC-MIGRATION | P1 | Text | OPEN | dev | ..." — già presenti in §Open Blockers
T6_ROADMAP_M18_ROWS_DONE      = M1.8 §2C (text()/text_run() come Adapter, DONE), §5A (Deprecazioni Graduali, DONE extended), §2D (Migrazione Composizioni, DONE) — documentano il Blocco 5.1
T7_TICKET_HOME_BODY           = TICKET-CENTERED-TEXT-MIGRATION.md contiene ## Criteri di accettazione "0 reference … helper rimosso" (= Blocco 5.2 forward-point implicit); TICKET-TEXT-SPEC-MIGRATION.md contiene "0 reference … overload rimosso" (= Blocco 5.2 forward-point implicit)
T8_SIBLING_CHASER_PRECEDENT   = TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV-VPS-3DOC-CAT5-ALIGN.md (DONE 2026-07-14, questo pattern) + TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN.md + TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN.md + TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN.md + TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN.md
T9_GATE_DOC_SYNC              = GATE_PASS: `bash tools/check_doc_sync.sh` exit 0 ("OK: doc-sync invariants hold")
T10_GATE_MAIN_CLEAN           = GATE_PASS: `bash tools/check_main_clean.sh` exit 0 (post-FF HEAD == origin/main, working tree clean)
```

Key diagonals:
- **FF era il tipo di sync giusto** (locale era ANCHESTOR di origin, no divergenza, no conflitto da risolvere — il push precedente era un no-op con auto-FF upstream che ha portato i 4 commit).
- **`docs/CURRENT_STATUS.md` SHA `5246d7bb` vs HEAD `cc3ad1a37`** differenza = 4 commit (i 4 Blocco 5.1). Un singolo snapshot-string update chiude il §honest-limitation pre-state drift.
- **I 2 ticket-home esistenti coprivano già Blocco 5.2 forward-point** (nei Criteri di accettazione), ma il nome "Blocco 5.2" non era esplicito. Naming esplicito = grep-discoverability per future agenti.

## Cronaca cat-5 (3-doc surface)

Questo chaser-chore atterra in UN atomic commit con la Cat-5 3-doc surface distribution per AGENTS.md §Docs canonical update discipline rule:

| File                                                       | Operation        | Detail                                                                                                                |
|------------------------------------------------------------|------------------|-----------------------------------------------------------------------------------------------------------------------|
| `docs/tickets/TICKET-BLOCO-5-1-3DOC-CAT5-ALIGN.md`         | NEW              | Cronaca estesa (this file). Subject envelope 53 chars ≤ 72 per `tools/check_commit_subject_length.sh` push-range audit. |
| `docs/CURRENT_STATUS.md`                                   | EDIT             | Snapshot SHA `5246d7bb` → `cc3ad1a37` + 1 sentence bridge note (Blocco 5.1 deprecation ACTIVE post-FF). NO state-of-area mutation (PASS/FAIL/PARTIAL/NOT RUN invariati — `Text Production V1` resta PASS). |
| `docs/FOLLOWUP_TICKETS.md`                                 | EDIT-prepend     | §Recently Closed Cita-Only row al TOP del §Recently Closed block (sopra l'ultima riga esistente). NO §Open Blockers row (Cat-5 anti-dup: questo chaser è closed-on-arrival). |
| `docs/tickets/TICKET-CENTERED-TEXT-MIGRATION.md`           | EDIT-append      | `## Phase tracking` subsection (5 righe) che nomina esplicitamente Phase 1 (Blocco 5.1 = deprecation, DONE 2026-07-14) + Phase 2 (Blocco 5.2 = complete removal, FORWARD-POINT). NO Stato mutation. |
| `docs/tickets/TICKET-TEXT-SPEC-MIGRATION.md`               | EDIT-append      | `## Phase tracking` subsection analogo per il TextSpec→TextDefinition migration (Phase 2 = caller migration FORWARD-POINT). |

Total: 1 NEW + 4 EDIT. ZERO source touched. ZERO new SDK symbol (Cat-2 freeze compliant). ZERO new singleton/registry/resolver/cache (AGENTS.md deny-everywhere preserved). ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved).

**SKIP espliciti** (con rationale):
- `docs/CHANGELOG.md` — **SKIP** perché 3 entry Blocco 5.1 sono già prepended at TOP of `## 2026-07-14` (4th commit `bacbfc5a` assorbito nel rewrite al `cc3ad1a3`). Aggiungere un 4th entry sarebbe Cat-3 anti-dup violation per AGENTS.md §`### Docs canonical update discipline rule`. La cronaca Blocco 5.1 in CHANGELOG è già cite-only + canonical-ticket-link.
- `docs/ROADMAP.md` — **SKIP** perché M1.8 §2C + §5A + §2D DONE rows già documentano il Blocco 5.1 forward-point + catena completa di chori. Aggiungere un nuovo §M18 entry sarebbe Cat-3 anti-dup (stessa informazione ridondante in 2 punti canonici).

## Phase summary cronaca (canonical home per Blocco 5.2 forward-point)

### Phase 1 — Deprecation bridge (Blocco 5.1, DONE 2026-07-14)

Per TICKET-TEXT-SPEC-MIGRATION:
- `LayerBuilder::text(name, TextSpec)` marked `[[deprecated]]` (commit local `751ac167` + FF-sync `74c924b9`/`bacbfc5a`/`cc3ad1a3`)
- `chronon3d::from_text_spec()` marked `[[deprecated]]`
- Compile warning rotates al ticket-home pointer per AGENTS.md §SHA cite pattern (inline-only rule)

Per TICKET-CENTERED-TEXT-MIGRATION:
- `centered_text(CenterTextOptions)` enhanced `[[deprecated]]` (commit FF `74c924b9`)
- `glow_text(CenterTextOptions, glow_color, radius, intensity)` updated `[[deprecated]]` message per evitare routing cycle (commit FF `b6397b90`)
- One-shot `spdlog::warn` runtime diagnostic fired via static-local bool gate (TICKET-104 precedent pattern, già M1.8 §2C)

### Phase 2 — Complete removal (Blocco 5.2, FORWARD-POINT 2026-07-14+)

Per TICKET-TEXT-SPEC-MIGRATION:
- 124+ content callers + 56+ test callers + 1 production caller migration a `text(name, TextDefinition&)` direct construction
- Sequenza di accettazione (Criteri di accettazione §del ticket): `0 reference a text(name, TextSpec) in production source` + `0 reference a from_text_spec() in production source` + `0 reference a text(name, TextSpec) in test source` + `0 reference a from_text_spec() in test source` + `LayerBuilder::text(name, TextSpec) overload rimosso` + `from_text_spec() adapter rimosso`
- macchina-verifica gate: `bash tools/wrap_push.sh origin main` PUSH-RANGE 11/11 verde post-migration (DEFERRED-WBH per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV env-block)

Per TICKET-CENTERED-TEXT-MIGRATION:
- 100+ callers migration a `text(name, TextDefinition&)` direct construction con field mapping esplicito (vedi Soluzione accettabile §del ticket)
- Sequenza di accettazione: `0 reference a centered_text in production source` + `0 reference a glow_text in production source` + `0 reference a centered_text in test source` + `0 reference a glow_text in test source` + `centered_text() helper rimosso` + `glow_text() helper rimosso`
- macchina-verifica gate: stessa sopra

## Cross-link canonici (per SHA cite-pattern AGENTS.md §Regole di lint documentale)

- Parent ticket set: [TICKET-CENTERED-TEXT-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION.md) (P2 OPEN, 100+ call sites) + [TICKET-TEXT-SPEC-MIGRATION](TICKET-TEXT-SPEC-MIGRATION.md) (P1 OPEN, 124+ content + 56+ test + 1 production callers)
- FF-sync commit set (inline per AGENTS.md §SHA cite pattern): `b6397b90` chore(text): deprecate centered_text + glow_text + `74c924b9` chore(text): fix deprecation ticket refs + `bacbfc5a` chore(text): fix-up-2: §10 numbering + cat-3 budget + `cc3ad1a3` chore(text): fix deprecation markers and changelog
- Local pre-FF commit: `751ac167` chore(text): deprecate legacy TextSpec overloads (Blocco 5.1)
- Sibling chaser-chore precedent (Cat-5 3-doc pattern): [TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV-VPS-3DOC-CAT5-ALIGN](TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV-VPS-3DOC-CAT5-ALIGN.md) + [TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN](TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN.md) + [TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN](TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN.md) + [TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN](TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN.md) + [TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN](TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN.md)
- Cat-3 3-doc rule basis: AGENTS.md §`### Docs canonical update discipline rule` (Cat-3 anti-dup codification) + AGENTS.md §`### 2×-in-one-chore: deprecation reversal bundles forward-point tickets` (Blocco 5.2 forward-point bundle rule)
- GATE chassis: AGENTS.md §GATE-MNT-01 closure lineage (per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh` triad) + AGENTS.md §Post-push SHA-selfcheck invariant (lost-commit prevention belt-and-suspenders)

## Forward-points (open)

| # | Status      | Description                                                                              |
|---|-------------|------------------------------------------------------------------------------------------|
| a | OPEN (P2)   | Blocco 5.2 — `centered_text()` + `glow_text()` rimozione fisica (post 100+ caller migration) |
| b | OPEN (P1)   | Blocco 5.2 — `text(name, TextSpec)` overload + `from_text_spec()` adapter rimozione fisica (post 124+56+1 caller migration) |
| c | OPEN (P2)   | macchina-verifica WBH: `c++ build + ctest 11/11 verde` su tutti i punti di acceptance (DEFERRED-WBH per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV env-block) |

The forward-points (a) e (b) sono i "Blocco 5.2 — deprecation completa" richiesti dall'utente. La cronaca del removal + acceptance criteria è già documentata nei rispettivi ticket-home parent (vedi Criteri di accettazione §).
