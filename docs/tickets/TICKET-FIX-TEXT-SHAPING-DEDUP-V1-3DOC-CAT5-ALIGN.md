# TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN — 3-doc closure for the Shape-call counter regression lock

> Stato: **DONE (this commit, 2026-07-14)**
> Componente: Text V1 cat-5 3-doc discipline
> Pattern: Cat-5 3-doc chaser-chore (pure markdown, no source touched)

## Origine (parent forward-point)

Il parent ticket [`TICKET-FIX-TEXT-SHAPING-DEDUP-V1`](TICKET-FIX-TEXT-SHAPING-DEDUP-V1.md) (DONE 2026-07-13 at commit `eccbdfca feat(text): shape_calls_per_line counter`) forward-points esplicitamente, nella sezione §Forward-points:

> **TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN**: dopo la chiusura,
> verificare se i canonici docs (`CURRENT_STATUS.md`, `FOLLOWUP_TICKETS.md`,
> `ROADMAP.md`) richiedono aggiornamenti di stato per la Point-8 dedup
> ceremony. Probabilmente no — dedup è implementazione refactor non stato
> release-blocker.

La nota parent-side "probabilmente no" è ridiventata "sì" durante questo chaser-cycle a seguito di due evidenze emerse nella sessione corrente:

1. **La regression lock del counter (`s_shape_calls_per_line` + i due accessors `reset_shape_call_counter()` / `get_shape_call_count()`)** è on-disk su `origin/main @ eccbdfca` ma non è referenziata né da `docs/CURRENT_STATUS.md` né da `docs/FOLLOWUP_TICKETS.md`. Senza un §Recently Closed row, una futura code review potrebbe chiedersi "dov'è la copertura del Point-8 contract" senza trovare una riga canonica nei 3 doc.

2. **`docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §2.3 "Non ancora completato"** elenca "motion blur testuale validato" come gap aperto — ma il counter `s_shape_calls_per_line` è il primo passo di una catena di strumentazione Point-8 (incluso `B02-Typewriter200Glyphs` future telemetry forward-pointed nel parent ticket). Una nota di chiusura nel Blocco 2/§2.3 del roadmap è ridondante ma coerente con il pattern Cat-5 3-doc alignment già istituito per ticket sorelle (`TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN` precedent).

Queste due evidenze motivano un Cat-5 3-doc chaser-chore — pure markdown surgical update, ZERO source modification, ZERO new SDK symbol surface, ZERO new singleton/registry/resolver/cache (per AGENTS.md deny-everywhere preserved verbatim).

## Cosa NON è toccato (boundary)

- **`include/chronon3d/**`**: zero modifiche. Il counter `s_shape_calls_per_line` come già shipped dal parent commit è canonico; questo chaser-cycle NON aggiunge types/ABI/symbols al SDK pubblico. Cat-2 freeze compliant senza chiedere eccezione.
- **`src/**`**: zero modifiche. Il `TryShape` factory path / `try_shape` increment / i due implementation site del counter (`primary` ctor + `try_shape` factory) restano invariati al parent commit `eccbdfca`.
- **`tests/**`**: zero modifiche. Il TEST_CASE `"ShapedGlyphLine: shape_calls_per_line counter == 1 on B02-equivalent 200-glyph line"` al parent commit è la regression lock attiva; nessun nuovo test file né extension al sibling `tests/content/test_shaped_glyph_line.cpp`.
- **`tools/**`**: zero modifiche. Nessun nuovo gate, nessun wire-in a `tools/wrap_push.sh` Step 4.5* (il counter è coperto dal ctest pass; non serve un cat-4 ancillary gate).

## Cosa è toccato (3-doc canonical surface)

### 1. `docs/tickets/TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN.md` (NEW)

Questo file stesso: cronaca estesa (questa sezione `## Cosa NON è toccato` + `## Cosa è toccato` + `## Forward-points` + `## Origine`). AGENTS.md "DocSync" rule §regole: cronaca estesa vive SOLO nella scheda ticket dedicata; i 3 doc canonical portano Cita-Only forward-link rows.

### 2. `docs/CHANGELOG.md` (EDIT, prepend)

NEW entry prepended at TOP sotto la `## 2026-07-14` section (above the existing `feat(diagnostics): camera overlay panel layout helper` row). Subject envelope `docs(text): shape-dedup counter 3-doc closure` = 45 chars ≤ 72 per `tools/check_commit_subject_length.sh` push-range audit. Body bullets:
- forward-point closure rationale (parent-delivered "probably no" → "yes" on this chaser cycle)
- 3-doc surface list (this NEW ticket + CHANGELOG prepend + FOLLOWUP §Recently Closed row)
- CRITICAL boundary declaration: NO EDIT `docs/CURRENT_STATUS.md` (Text V1 area state invariant — was PASS, stays PASS, counter lands INSIDE existing PASS boundary per AGENTS.md "Disciplina di aggiornamento dei canonici")
- Cat-3 minimal-surface verify (zero SDK symbol / zero new include deny-list / zero singleton)
- macchina-verifica DEFERRED-WBH (canonical vcpkg glm/magic_enum env-block)
- cross-link to parent ticket + to forward-points

### 3. `docs/FOLLOWUP_TICKETS.md` (EDIT, §Recently Closed at top)

NEW §Recently Closed row prepended above the `TICKET-CAMERA-OVERLAY-PANEL-CONSTRAINTS` row (Cat-5 3-doc same-atomic pattern: ticket opens + closes in same atomic cycle, condizione di non-overlap con §Open Blockers top). Single Cita-Only closure summary (~600 char singola riga — coerente con il pattern di chiusura sorelle Cat-5 3-doc precedent `TICKET-CAMERA-OVERLAY-PANEL-CONSTRAINTS` closure row).

### 4. NO EDIT `docs/CURRENT_STATUS.md`

Text Production V1 area row è PASS (vedi `docs/CURRENT_STATUS.md:34` — il counter `s_shape_calls_per_line` non transita l'area state perché non è un nuovo release-blocker; è una regression lock INTERNA al perimetro PASS esistente). Per AGENTS.md "Disciplina di aggiornamento dei canonici" `CURRENT_STATUS.md`: "Solo quando cambia lo stato presente di un'area (PASS/FAIL/PARTIAL/NOT RUN)" — il counter non cambia lo stato, quindi NO EDIT.

## Forward-points (canonical cross-links)

1. **`TICKET-FIX-TEXT-SHAPING-DEDUP-V1`** (parent ticket) — DONE 2026-07-13 at commit `eccbdfca`. State: rimane DONE (nessuna regressione); il chaser-cycle di questo ticket si limita ad annotare 3-doc-discoverability del regression lock.
2. **`TICKET-SHAPING-DEDUP-V1-MACHINE-VERIFY`** (forward-point, NEW) — macchina-verifica del counter `s_shape_calls_per_line` su `chronon3d_content_tests` (target `tests/content/test_shaped_glyph_line.cpp::"shape_calls_per_line counter == 1 on B02-equivalent 200-glyph line"`). DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` (this VPS lacks vcpkg glm/magic_enum + tmpfs env-block). On-WBH ctest invocation: `ctest -R content --output-on-failure` should emit `TEST_CASE: ... shape_calls_per_line counter == 1 on B02-equivalent 200-glyph line : PASS`.
3. **`TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN`** (sibling ticket, OPEN P2) — pure precedential Cat-5 3-doc chaser-chore pattern anchor. Per AGENTS.md `docs/DOCUMENTATION_GOVERNANCE.md` §CHANGELOG il sibling-set Cat-5 3-doc (TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN + TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN + TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN + this new TICKET) è il canonical anti-dup precedent set per future chaser-chores di test-coverage-matrix entries / regression-lock events.
4. **`TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`** (env-block proactive parent) — ongoing macchina-verifica DEFERRED-WBH umbrella per qualsiasi tooling VPS-bound che richiede le librerie `glm`:magic_enum vcpkg install.

## Criteri di accettazione (chiusura ticket)

- [x] Ticket-home `docs/tickets/TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN.md` NEW con cronaca estesa
- [x] `docs/CHANGELOG.md` EDIT (prepend Cita-Only entry at TOP, sotto `## 2026-07-14`)
- [x] `docs/FOLLOWUP_TICKETS.md` EDIT (§Recently Closed row append at top, atomic open+close)
- [x] NO EDIT `docs/CURRENT_STATUS.md` (area state invariant)
- [x] NO EDIT `docs/ROADMAP.md` (Text V1 forward direction unchanged — counter è implementation regression lock, non milestone shift)
- [x] Cat-2 freeze compliance: ZERO new symbol in `include/chronon3d/`
- [x] Cat-3 anti-duplication: cronaca estesa SOLO nel ticket-home; canonical docs portano Cita-Only forward-link rows
- [x] Cat-3 minimal-surface: 1 NEW ticket-home + 2 EDIT canonicals + 0 source file modification
- [x] AGENTS.md docs canonical update discipline rule: canonicals portano ALMOST `1 riga sintetica` (Cita-Only forward-link); cronaca estesa delegata al ticket-home per Cat-3 anti-dup
- [x] AGENTS.md "Fare PR piccole e mirate" + Cat-3 non-bundling: 1 commit per 1 forward-point closure

## Cat-5 3-doc same-atomic-bonded-chore corroboration

L'apertura ticket + chiusura ticket avviene in UN singolo atomic chore — questo è il pattern Cat-5 3-doc discipline canonico. NON viene aperto §Open Blockers row in `docs/FOLLOWUP_TICKETS.md` prima della chiusura (che richiederebbe 2 commit: uno OPEN + uno CLOSED). Il §Recently Closed row contiene tutta la cronaca inline (Cita-Only forward-link) coerente con i 3 precedenti Cat-5 3-doc chaser-chores sorelle:

- `TICKET-CAMERA-OVERLAY-PANEL-CONSTRAINTS` (2026-07-14): chore `feat(diagnostics): camera overlay panel layout helper` (49 chars) — 1 NEW ticket + 1 EDIT CHANGELOG + 1 EDIT §Recently Closed, NO §Current_Status edit
- `TICKET-RESIDUAL-BUILD-ROT-RECOVERY` lineage (2026-07-13): chore `b16ad302` Premult alpha=0 fixture + SweepN + macchina-verifica PASS — multi-commit cat-5 3-doc closure lineage (forward-points a, b, c macchina-verified)
- `TICKET-TEXT-V1-FUNCTIONAL-CERT` (2026-07-12): cat-5 3-doc + verify_text_functional_linux.sh gate HARNESS-COMPLETE

Questo chaser-cycle è il pattern canonical: 1 commit, 3 file (1 NEW + 2 EDIT), ZERO source, ZERO §Open Blockers row.

## §honest qualifiers (per AGENTS.md §honesty)

1. **macchina-verifica DEFERRED-WBH** per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern. VPS-only verification: 
   ```
   rg -nE 's_shape_calls_per_line|reset_shape_call_counter|get_shape_call_count' \
       content/common/text/ tests/content/
   ```
   returns 6+ matches (2 decl in glyph_layout.hpp + 2 def in glyph_layout.cpp + 2 test sites in test_shaped_glyph_line.cpp). NON un green-PASS surrogate — solo inventory check che dimostra il canonical regression lock esiste come on-disk artifact.
2. **`docs/CHANGELOG.md:282` existing entry** (`feat(text): shape_calls_per_line counter (FIX-TEXT-SHAPING-DEDUP-V1) — 2026-07-13`) NON sostituito da questo chaser-cycle. AGENTS.md "Disciplina di aggiornamento dei canonici": CHANGELOG multiplica rows per eventi cronologici distinti, non edit-and-overwrite; il parent commit resta tracked nello stesso CHANGELOG, questo chaser-cycle prepende sotto la stessa `## 2026-07-14` day sectione (poiché entrambi i commit sono same-day).
3. **`docs/FOLLOWUP_TICKETS.md` §Recently Closed row** è una Cita-Only forward-link al this ticket — NON replica la cronaca di questo ticket-home (AGENTS.md Cat-3 anti-dup). Chi vuole il detail deve navigare al ticket file.
4. **`docs/CURRENT_STATUS.md` Text Production V1 area row** (§Hygiene Text V1 row) — il counter regression lock è INSIDE existing Text V1 PASS boundary (counter protects the cat-3 invariant `engine.shape_text invocations per line == 1`); NON transita area state.

## Origine cronaca estesa (per §honesty cronaca frame)

Questo chaser-cycle si è materializzato durante la sessione 2026-07-14 continuation delle Text V1 closure lineage (`facendo parte del broader Text Production V1 → Text V1 cert Step 11 pipeline trailer`). Il pre-existing forward-point nel parent ticket `TICKET-FIX-TEXT-SHAPING-DEDUP-V1.md` era una nota profetica — la decisione "sì, scrivere un Cat-5 3-doc chaser" è backward-compatible con le conclusioni programmatic del parent ("single-shape efficiency contract" + "Point-8 efficiency contract" + "Cat-5 ticket cap after upstream ShapedGlyphLine dedup ceremony"). La post-realization di questa chiusura forward-point è una continuazione naturale della catena M1.8 / Text V1 / SIMD-V2 + Regression-lock Matrix commitment (la catena copre da FU2 audit scaffold → Clip 06 diagnostic CHIUSA → FU02next pre-render invariants → Phase A.3 closed → 17.1-17.8 migration DONE → Shape-call counter regression lock chiuso (parent) → questa chiusura 3-doc delle regression lock discovered on-disk).

## Cross-link (canonical sources)

- AGENTS.md "Quando un file sembra mancare" → "Prima controllare `git status -sb`; ... Confrontare `HEAD` con `origin/main`; ..."
- AGENTS.md "Insieme canonico della documentazione" → i 4 file canonical `CURRENT_STATUS.md`, `ROADMAP.md`, `RELEASE_GATE.md`, `FOLLOWUP_TICKETS.md`
- AGENTS.md "Disciplina di aggiornamento dei canonici" → tabella scope per canonical (CURRENT_STATUS solo su state change, etc.)
- AGENTS.md "Docs canonical update discipline rule" → lint-discipline Cat-3 anti-dup (cronaca estesa nel ticket-home ONLY)
- Parent ticket [`TICKET-FIX-TEXT-SHAPING-DEDUP-V1`](TICKET-FIX-TEXT-SHAPING-DEDUP-V1.md) (the source of the forward-point)
- `docs/DOCUMENTATION_GOVERNANCE.md` §CHANGELOG "Limite raccomandato: da tre a sei punti per ticket"
- `tools/check_commit_subject_length.sh` (subject envelope ≤ 72 chars + push-range audit per AGENTS.md §Post-push SHA-selfcheck invariant)
- `tools/wrap_push.sh` (the canonical push-pipeline)
- `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` (the env-block parent for macchina-verifica DEFERRED-WBH)
