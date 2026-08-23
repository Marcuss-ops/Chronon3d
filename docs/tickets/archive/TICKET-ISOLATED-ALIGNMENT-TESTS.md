# TICKET-ISOLATED-ALIGNMENT-TESTS — Isolated alignment + auto-fit regression locks

## Stato

DONE (2026-07-24)
- 2 test binary standalone compilati ed eseguiti con successo.
- I 3 casi alignment sono assertions bloccanti e passano tramite `TextDefinition` pubblico.
- I 3 casi auto-fit sono assertions bloccanti su determinismo cache, overflow esplicito e 5-run determinism.

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

## Forward-points

- **Fase 1 (DONE)**: bbox fix + cluster-fallback → TICKET-INK-BBOX-GEOMETRIC + TICKET-OPENTYPE-FEATURES-PASS (future ticket)
- **Fase 4 (future)**: word-binding + word-timing quality → TICKET-TIMED-WORD-BINDING + TICKET-WORD-TIMING-QUALITY (future ticket)
- **Fase 9 (DONE)**: CapCut reference corpus → TICKET-CAPCUT-REFERENCE-CORPUS
- **LocalEngine extraction (future)**: `LocalEngine` struct in `tests/text/test_text_auto_fit.cpp` (linee 33-41) duplica verbatim quello in `tests/text/test_auto_fit_font_size.cpp:50-58`. Forward-point: estrarre in `tests/helpers/text_test_engine.hpp` quando un 3rd user appare (Cat-3 anti-dup deferred — 2 users = borderline). Per reviewer #3.

## §Accepted deviations

- **Deviation #2 (reviewer #4)**: "Hello" è troppo corto per esporre kern pairs / ligatures. Forward-point: aggiungere sub-case "AVATAR" (kern) o "office" (ligatures) per copertura TICKET-OPENTYPE-FEATURES-PASS.
- **Deviation #3 (reviewer #5)**: ~~CHANGELOG entry descrive activation protocol inline. Per Cat-3 anti-dup canonical entry dovrebbe essere ≤1 sentence + ticket link; attivazione dettagliata vive nel ticket §Activation protocol.~~ **RESOLVED in chore fixup `afe70f33`**: nuovo CHANGELOG entry sintetizzato per Cat-3 ≤1 sentence + link discipline (linka al TICKET invece di duplicare activation protocol inline).

## Cross-link canonici

- `docs/FOLLOWUP_TICKETS.md`: row DONE P1
- `docs/CHANGELOG.md`: entry 2026-07-21
- `tests/text_golden/text_completeness/text_alignment.cpp`: Test 7 (EXPECT_FAIL precedent con box degenere, distinto da questo file)
- `tests/text/test_auto_fit_font_size.cpp`: 8 TEST_CASE esistenti per auto-fit (Pattern A precedent); questo file aggiunge 3 TEST_CASE ortogonali (cache on/off + impossible min + 5-run)
- `docs/tickets/TICKET-FALSE-GREEN-TEST-AUDIT.md`: parent ticket per i test di robustezza Fase 1-2
