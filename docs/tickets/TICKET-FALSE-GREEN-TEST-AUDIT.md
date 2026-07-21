# TICKET-FALSE-GREEN-TEST-AUDIT — Test hardening (falso-verde remediation)

## Stato: DONE (2026-07-21, commit `<pending>`)

## Problema

Il verdict CapCut-grade §6 (test che farei) elenca 6 classi di test "falsi-verdi"
esistenti in `tests/certification/test_text_production_v1.cpp` + sibling files.
Il commento del test dichiara "verifica `glyph_count>0, missing_glyph_count==0,
ink_bounds non vuoto, visible_ink_pixels>100`" ma il codice reale del helper
`check_anti_false_green()` verifica solo bbox non-empty + visible_px > 100.
Inoltre:
- UTF-8 test (line 295) ritorna silenziosamente su null fb (silent PASS).
- Auto-fit verifica `font_size` (pre-raster) ma non `ink_bbox` (post-raster).
- Alignment test mischia posizione + anchor + alignment (no isolation).
- Typewriter test verifica solo `visible_pixels` monotonic, non `cluster_visible_count`.
- Clipping test usa testo che entra comodamente nel box (no real clipping).
- Alignment test presuppone alignment funzioni (KNOWN LIMITATION nel file).

## Soluzione adottata

5 file modificati (Cat-3 minimal-surface, **append-only + 1 helper strengthening**):

| File | Modifica |
|---|---|
| `tests/certification/test_text_production_v1.cpp` | `check_anti_false_green()`: aggiunte dimension assertions esplicite (`bbox.x1 - bbox.x0 > 0`, `bbox.y1 - bbox.y0 > 0`) + rimosso redundant cast. UTF-8 TEST_CASE (CertText/utf8): `if (fb == nullptr) return;` rimosso → `REQUIRE(fb != nullptr)` + dimension assertions. |
| `tests/text/test_auto_fit_font_size.cpp` | NEW TEST_CASE 9: renderizza via `composition()` + `SoftwareRenderer` + scansiona `alpha_bbox(fb)` post-raster. Verdict verbatim: `ink_bbox.right <= 400`, `ink_bbox.bottom <= 200`. |
| `tests/text_golden/text_completeness/text_alignment.cpp` | NEW TEST_CASE 7 [EXPECT_FAIL]: stessa box + pos + anchor, solo TextAlign varia, `\|cx - 960\| <= 1px`. EXPECT_FAIL perché alignment NON è implementato per single-line (KNOWN LIMITATION file header line 7-11). |
| `tests/text_golden/text_completeness/text_typewriter.cpp` | NEW TEST_CASE 7: 6 frame `F0/F1/F5/F10/F20/F30`, monotonic `visible_pixels` + stable layout invariant (`bbox.x1` non retreat, `\|y0 - prev_y0\| <= 2px`, `\|y1 - prev_y1\| <= 2px`). |
| `tests/text_golden/text_clip/text_completeness.cpp` | NEW TEST_CASE: `font_size=200` in `400x200` box, `TextOverflow::Clip`, scansiona pixel dentro/fuori clip rect → `outside_count == 0`, `inside_count > 100`. |

## Criteri di accettazione

- [x] `check_anti_false_green()` ora ha dimension assertions esplicite (no silent 1px bbox).
- [x] UTF-8 test rifiuta null fb (`REQUIRE(fb != nullptr)`).
- [x] Auto-fit NEW TEST_CASE usa post-raster alpha_bbox scan.
- [x] Alignment NEW TEST_CASE usa EXPECT_FAIL pattern (no false-green rot).
- [x] Typewriter NEW TEST_CASE copre F0/F1/F5/F10/F20/F30 + stable layout.
- [x] Clipping NEW TEST_CASE usa `font_size=200` in `400x200` (real overflow).
- [x] Cat-3 minimal-surface verificato: ZERO nuove SDK API; ZERO `<msdfgen>/<libtess2>/<unicode[/...]>`.
- [x] Cronaca estesa SOLO in questo ticket; canonical docs ricevono solo 1 riga sintetica.

## Accepted deviations

1. **Audit-driven `glyph_count > 0` + `missing_glyph_count == 0`** (Step 1):
   NON verificati direttamente via `audit_text_visibility()` (TextVisibilityAudit, gated
   by `CHRONON3D_BUILD_DIAGNOSTICS`). Motivazione: il test path canonico usa
   `composition() + renderer.render()` che NON espone il `TextRunShape` richiesto
   da `audit_text_visibility()`. Plumbare l'audit attraverso la pipeline richiede
   refactoring del test infrastructure oltre lo scope di questo chore. Il proxy
   bbox+visible_px è la canonical user-spec observable (vedi `TICKET §Forward-points`
   per il deferred-WBH path). Honest-limitation disclosed in helper docstring.

2. **`cluster_visible_count == visible_pixels` proxy** (Step 4 typewriter):
   il proxy è ESATTO SOLO per testo ASCII (current test `"TYPEWRITER REVEAL TEST"`
   è ASCII: grapheme==byte==char step). Per testo non-ASCII (combining marks,
   ZWJ emoji), il proxy sotto-conta graphemes. Acceptable per il current test
   ASCII; forward-point `TICKET-FALSE-GREEN-TEST-AUDIT-CLUSTER-COUNT` per
   helper UTF-8-aware (`count_clusters_in_substring(substr)`).

3. **Step 2 ink_bbox via post-raster alpha_bbox scan** (round-2 Finding #3):
   la versione originale usava `shape->layout->bounds` (pre-raster). Round-2
   reviewer ha flaggato che il verdict chiede "visible ink", non "layout bounds".
   Rewrite: composition() + SoftwareRenderer + `alpha_bbox(fb)`. Più costoso
   (full render) ma cattura il caso "font_size shrinks ma ink still overflows"
   che il pre-raster bbox NON rileva.

4. **Step 5 hardcoded clip rect coords** (round-2 Finding #5):
   `[760, 440, 1160, 640]` calcolato manualmente da `placement + anchor + box`.
   Acceptable per il current test (comment MANUAL COORDS aggiunto per
   manutenibilità). Forward-point: helper `compute_clip_rect_for_box(placement,
   anchor, box)` per derivation automatica.

5. **Step 3 alignment test EXPECT_FAIL pattern** (round-2 Finding #2):
   il test PRESUME alignment funzioni per single-line text, ma il file header
   documenta la KNOWN LIMITATION. Fix: rename a `[EXPECT_FAIL: alignment not
   implemented for single-line text]` + `WARN` + `return` early. Quando alignment
   viene implementato, rimuovere il `return` early e il test diventa regression
   lock reale.

## Design rationale

### Cat-3 minimal-surface

- **ZERO nuovi simboli in `include/chronon3d/`** (tutte le modifiche sono in `tests/`).
- **ZERO `<msdfgen>` / `<libtess2>` / `<unicode[/...]>`** introdotti (Gate 5 Check 11).
- **ZERO nuovi singleton/registry/resolver/cache/service-locator**.
- **Append-only** per i 4 nuovi TEST_CASEs (no modification of existing
  TEST_CASEs che altri CI potrebbe dipendere).
- **1 helper strengthening** (`check_anti_false_green` aggiunge dimension
  assertions esplicite — backward-compatible).

### Honest-discipline

- UTF-8 test: `REQUIRE(fb != nullptr)` esplicito (rifiuto di silent PASS su null fb).
- Alignment test: EXPECT_FAIL pattern (no false-green rot quando alignment
  non è implementato).
- Typewriter rationale comment: esplicito "ASCII-only proxy" disclaimer.
- Auto-fit: post-raster scan (verifica ink reale, non layout).
- Clipping: MANUAL COORDS comment esplicito (manutenibilità).

### Forward-points (per deferred-WBH)

| Forward-point | Priority | Note |
|---|---|---|
| `TICKET-FALSE-GREEN-TEST-AUDIT-AUDIT-DRIVEN` | P2 | Plumb `audit_text_visibility()` through `composition() + renderer.render()` for direct `glyph_count > 0 + missing_glyph_count == 0` verification. |
| `TICKET-FALSE-GREEN-TEST-AUDIT-CLUSTER-COUNT` | P3 | UTF-8-aware cluster counter `count_clusters_in_substring(substr)` for typewriter non-ASCII cases. |
| `TICKET-FALSE-GREEN-TEST-AUDIT-CLIP-RECT-HELPER` | P3 | `compute_clip_rect_for_box(placement, anchor, box)` helper for auto-derived clip rect. |

## Cross-link

- AGENTS.md §honest-discipline (silent PASS rejection via `REQUIRE(fb != nullptr)`).
- `tests/certification/test_text_production_v1.cpp:60` (helper strengthening).
- `tests/certification/test_text_production_v1.cpp:295` (UTF-8 REQUIRE).
- `tests/text/test_auto_fit_font_size.cpp:230` (NEW TEST_CASE 9 post-raster).
- `tests/text_golden/text_completeness/text_alignment.cpp:228` (NEW TEST_CASE 7 EXPECT_FAIL).
- `tests/text_golden/text_completeness/text_typewriter.cpp:175` (NEW TEST_CASE 7).
- `tests/text_golden/text_clip/text_completeness.cpp:750` (NEW TEST_CASE clip).
- `include/chronon3d/text/text_visibility_audit.hpp` (gated audit, deferred-WBH).
