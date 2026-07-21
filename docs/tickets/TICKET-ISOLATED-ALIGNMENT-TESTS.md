# TICKET-ISOLATED-ALIGNMENT-TESTS — Isolated alignment + auto-fit regression locks

## Stato

OPEN (2026-07-21, commit pending)
- **DONE (skeleton)**: 2 nuovi file di test + tests/text/CMakeLists.txt + wire-in in tests/manifests/test_definitions.cmake.
- **OPEN (alignment activation)**: 3 TEST_CASE alignment in `EXPECT_FAIL` mode (WARN + early-return) — da attivare (rimuovere `return;`) quando l'alignment engine implementerà TextAlign per single-line text. Vedi §Activation protocol sotto.

## Problema

Il verdict CapCut-grade Chronon3D (§Fase 7) richiede test isolati che blocchino regressioni su:
1. **Alignment isolation**: stessa box + stessa posizione + stesso anchor; solo `TextAlign` varia. Oggi il test esistente (`tests/text_golden/text_completeness/text_alignment.cpp` Test 7) è circolare perché usa box degenere (1920x1080 == canvas) — tutti e 3 i centroidi cadono sempre a ~960 (canvas center) indipendentemente da `TextAlign`, quindi NON distingue alignment-on da alignment-off.
2. **Auto-fit determinism**: cache on/off deve produrre identico output. Caso impossibile (min_font_size > box) deve produrre overflow esplicito, mai silent clip.

## Soluzione

### File creati (3)

| Path | Tipo | Contenuto |
|---|---|---|
| `tests/text/CMakeLists.txt` | NEW (registration) | 2 `chronon3d_add_test_suite()` calls (no raw `add_executable`) |
| `tests/text/test_text_alignment_isolated.cpp` | NEW (3 TEST_CASE) | Left/Center/Right alignment isolation con box NON-degenere 400x200 |
| `tests/text/test_text_auto_fit.cpp` | NEW (3 TEST_CASE) | Cache on/off determinism, impossible min_font_size, 5-run determinism |

### File aggiornati (1)

| Path | Modifica |
|---|---|
| `tests/manifests/test_definitions.cmake` | +1 riga `text/CMakeLists.txt` (dopo `reference/capcut/CMakeLists.txt`) |

### Test design

**Alignment isolation (Pattern B — full render + `alpha_bbox`)**:
- Box: 400x200 a position (200, 200), anchor TopLeft → box.x ∈ [200, 600], box_center_x = 400
- Stesso canvas 1920x1080 per tutte e 3 le varianti
- Solo `TextAlign::Left/Center/Right` cambia
- Assert:
  - Left: `ink.x0 ~ 200` (5px tolerance per font metrics offset)
  - Center: `ink.center_x ~ 400` (1px tolerance, per verdict spec)
  - Right: `ink.x1 ~ 600` (5px tolerance)

**Auto-fit isolation (Pattern A — `LocalEngine` + `materialize_text_run_shape`)**:
- Test 1 (cache on/off): stessa params, `cache_layout=true` vs `false` → identico `font_size + bounds.x + bounds.y`
- Test 2 (impossible min_font_size): `min=200` in 400x200 box → `font_size == 200` AND `bounds.x > 400` AND `bounds.y > 200` (explicit overflow, NO silent clip)
- Test 3 (5-run determinism): 5 chiamate con stessa params → bit-identico output

### Activation protocol (per future agent che fixa TextAlign)

Quando l'alignment engine viene aggiornato per applicare `TextAlign` a single-line text:
1. Rimuovere `return;` (riga con comment `EXPECT_FAIL — see WARN above`) da ciascuno dei 3 TEST_CASE in `tests/text/test_text_alignment_isolated.cpp`.
2. Rimuovere la riga `WARN("EXPECT_FAIL: ...");` corrispondente.
3. Verificare che i 3 test PASSINO con la nuova implementazione.
4. Cross-link questo ticket nel commit che fixa l'alignment.

## Forward-points

- **Fase 1 (DONE)**: bbox fix + cluster-fallback → TICKET-INK-BBOX-GEOMETRIC + TICKET-OPENTYPE-FEATURES-PASS (future ticket)
- **Fase 4 (future)**: word-binding + word-timing quality → TICKET-TIMED-WORD-BINDING + TICKET-WORD-TIMING-QUALITY (future ticket)
- **Fase 9 (DONE)**: CapCut reference corpus → TICKET-CAPCUT-REFERENCE-CORPUS
- **Alignment activation (future)**: rimuovere `return;` dai 3 alignment TEST_CASE quando l'alignment engine fissa TextAlign per single-line text

## Cross-link canonici

- `docs/FOLLOWUP_TICKETS.md`: row OPEN P1 (riga aggiunta in questo commit)
- `docs/CHANGELOG.md`: entry 2026-07-21
- `tests/text_golden/text_completeness/text_alignment.cpp`: Test 7 (EXPECT_FAIL precedent con box degenere, distinto da questo file)
- `tests/text/test_auto_fit_font_size.cpp`: 8 TEST_CASE esistenti per auto-fit (Pattern A precedent); questo file aggiunge 3 TEST_CASE ortogonali (cache on/off + impossible min + 5-run)
- `docs/tickets/TICKET-FALSE-GREEN-TEST-AUDIT.md`: parent ticket per i test di robustezza Fase 1-2