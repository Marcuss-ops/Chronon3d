# TICKET-M1.5#10-SEQUENCE — Progressive deletion of `include/chronon3d/text/rich_text.hpp` (legacy polyfill)

> **Status:** PARTIAL (Step 1/4 done in working tree, commit pending this session).
>
> **Area:** Text Production V1 / Rimozione percorsi legacy (AGENTS.md v0.1 Cat-3 freeze-compliant).
>
> **Vincoli architetturali:** nessun nuovo file legacy, nessuna divisione di `rich_text.hpp` in sotto-headers polyfill; i 4 concetti sono migrati alle API canoniche modern; il file viene eliminato completamente al termine della sequenza.
>
> **Source of truth (working tree):** working file `tests/layout/test_design_kit.cpp` post-Step 1 drop obsolete TEST_CASE + doc-sync in the canonicals.

## User spec (verbatim)

> Esegui M1.5#10 — refactor di include/chronon3d/text/rich_text.hpp: NON dividerlo in nuovi file legacy. Migra invece i 4 concetti: RichTextLine → TextDocument; RichTextRun → TextStyleSpan; draw_rich_text() → LayerBuilder::text_run(...); comportamento decorative/anchor gestito dal percorso compilato. Dopo la migrazione: elimina completamente il file. Vincolo: marcato già oggi come pipeline legacy. Sequenza: questo item dopo M1.5#5 pronta. Lavora direttamente su main, un commit per step di migrazione, wrap_push.sh, doc sync nello stesso commit.

## Step 1/4 — Done (working tree, commit pending this session)

`tests/layout/test_design_kit.cpp` — obsolete `TEST_CASE("RichTextLine measures a mixed inline line with text, spacing and symbol")` rimosso. L'invariante "mixed inline line with text + space + star measured" non è riproducibile nel modello moderno (TextDocument + TextStyleSpan descrivono UNA stringa UTF-8 con override per range; le primitive `l.text()` + `l.star()` sono SEPARATE primitive nel LayerBuilder canonico → "decorative/anchor gestito dal percorso compilato"). La copertura equivalente vive in `tests/text/test_text_layout.cpp` + `tests/text/test_text_run_node_execute.cpp` (sub-trees del pipeline canonico).

La cancellazione è netta: il `TEST_CASE` rimosso non è sostituito da un test "parziale" o "di transizione" — è una cancellazione perché il concetto stesso ("measured mixed line") non esiste più nel modello canonico.

**Verifica macchina (working tree):**

- `bash tools/check_legacy_text_pipeline.sh` → PASS (legacy gate non tripa su transient empty+deleted test).
- `bash tools/check_doc_sync.sh` → PASS (canonicals changed in same commit).
- `cmake --build build --target chronon3d_backend_text` → exit 0 (`test_design_kit.cpp` post-delete non rompe compile-time symbol resolution).

## Step 2/4 — PLANNED (RichTextRun → TextStyleSpan)

Grep sweep `\\bRichTextRun\\b` su tutta la working tree dopo Step 1:

- Atteso hit residuo: ZERO (l'unico consumer era il TEST_CASE rimosso in Step 1).
- Atteso canonical migration introdotta: nessuna — la `RichTextRun` struct (12 campi: kind, text, color, font, font_size, tracking, advance, paint, material, star_*) collassa naturalmente: i campi `kind` + `star_*` sono reduntanti nel modello canonico (le `l.star()` sono SEPARATE primitives); `text` + `color` + `font` + `font_size` + `tracking` mappano 1:1 su `TextSpec`/`TextStyleSpan`; `paint` + `material` sono già canonici.
- Se grep sweep conferma zero residui → Step 2 = doc-only commit (no source changes).

**Verifica macchina pre-Step-2:**

- `grep -rnE '\\bRichTextRun\\b' --include='*.cpp' --include='*.hpp'` → atteso ZERO hit escludendo `rich_text.hpp` (che verrà eliminato in Step 4).
- Compile dry-run su file che includevano transitivamente `rich_text.hpp` (post-aggregator modify in Step 4): nessuna regressione attesa.

## Step 3/4 — PLANNED (draw_rich_text() → LayerBuilder::text_run(...))

Inspection (working tree): `draw_rich_text()` è una inline free function in `rich_text.hpp:198`. La sua firma accetta `(LayerBuilder&, const RichTextLine&, Vec3, RichTextLayoutOptions, FontEngine*, std::string_view prefix)`.

Grep sweep `\\bdraw_rich_text\\b` su working tree (escludendo `rich_text.hpp` stesso + test_design_kit.cpp post-Step 1):

- Atteso hit residuo: ZERO. La survey pre-M1.5#10 (commit di M1.5#5+M1.5#6+M1.5#8) ha convertito tutti i content/showcases + scenes/camera call sites a `l.text(name, TextSpec{...})` (vedi M1.5#5 docstring: "15+ content/showcases verified" — search inventory registrato in M1.5#9 (1/5)).
- Atteso canonical migration introdotta: nessuna — `l.text(name, TextSpec{...})` è già canonico e viene emesso dalla funzione legacy stessa. La differenza M1.5#10 è di TIPO: il path canonico finale userà `l.text_run(name, TextRunParams{...})` (M1.5#5 lineage) che produce `TextRunShape` (TextAnimator V2), mentre il path legacy emette `l.text(...)` che produce `TextShape`. Step 3 sarà un no-op di codice (la funzione viene eliminata in Step 4 senza sostituti).

Se grep sweep conferma zero residui → Step 3 = doc-only commit.

## Step 4/4 — PLANNED (DELETE the file)

Operazioni atomiche nel commit finale:

1. `rm include/chronon3d/text/rich_text.hpp` (380 LOC).
2. `rm src/backends/text/rich_text.cpp` (90 LOC: measure + measure_run).
3. `include/chronon3d/layout/design_kit.hpp` line 17: droppa `#include <chronon3d/text/rich_text.hpp>`. Aggiorna il comment block di intestazione (line 1-15) per riflettere che `design_kit` ora ha solo `layout_stack` + `stroked_shapes` (NON più `rich_text`).
4. Grep sweep globale:
   - `grep -rnE 'rich_text\\.hpp|RichTextLine|RichTextRun|draw_rich_text|RichTextAnchor|RichTextVerticalAnchor|RichTextRunMetrics|RichTextLineMetrics|RichTextLayoutOptions' --include='*.cpp' --include='*.hpp'` → atteso ZERO hit (post-Step-1+2+3).
   - `grep -rnE 'rich_text\\.hpp' --include='*CMakeLists*'` → atteso ZERO hit (pre-M1.5#10 sweep conferma: nessuna reference build-system).
5. `cmake` re-configure: atteso zero target nuovi + zero target rotti (nessuna referenza build-system toccata).
6. AGENTS.md v0.1 Cat-3 freeze-compliant: zero new public API surface, zero new singletons/registries/caches.

**Verifica macchina finale:**

- `bash tools/check_legacy_text_pipeline.sh` → PASS post-delete (gate rimuove baseline entry).
- `cmake --build build` target sweep (`chronon3d_backend_text`, `chronon3d_layout_tests`, `chronon3d_scene_tests`) → atteso exit 0 ovunque.
- `bash tools/install_consumer_test.sh` → atteso Phase 4 PASS (nessun regress perché il public ABI non si espande, solo contrae).

## Trade-offs cross-step

| Trade-off | Resolution |
|---|---|
| I 3 tipi RichTextLine/RichTextRun/RichTextAnchor vs il modello canonico TextDocument+TextStyleSpan | mapping 1:1 a livello di CAMPO, ma a livello di TIPO il `RichTextLine` collassa (concetto "mixed line" non esiste più — separato in primitive layer). |
| `draw_rich_text()` vs `LayerBuilder::text_run(...)` | il path canonico finale è `l.text_run(...)` (produce `TextRunShape`); il path legacy emette `l.text(...)` (produce `TextShape`). La funzione legacy diventa un no-op post-3-step (elimina in Step 4 senza sostituti). |
| Mixed-line concept (text + star + space in single `RichTextLine`) vs separate primitives (`l.text()` + `l.star()`) | separate primitives — "decorative/anchor gestito dal percorso compilato" (compile-time composition). |
| Test coverage preservation | migration verso `tests/text/test_text_layout.cpp` + `tests/text/test_text_run_node_execute.cpp` (sub-trees del pipeline canonico). NON replace del test con uno "di transizione" — drop netto. |

## AGENTS.md v0.1 vincoli

- Cat-3 freeze: Rimozione percorsi legacy, dead code, API deprecate. ✓ (in scope).
- Cat-1/2/4/5 non applicabili: nessuna nuova feature, nessun test aggiunto (drop solo), nessun registry/resolver/cache introdotto.
- Zero new public API surface (canonical public surface `l.text(name, TextSpec)` + `l.text_run(name, TextRunParams)` invariato).
- Zero new singletons/registries/caches.

## Statistiche finali attese

- `rich_text.hpp`: 380 LOC → 0 LOC (-100%).
- `rich_text.cpp`: 90 LOC → 0 LOC (-100%).
- Public types removed: 3 (`RichTextLine` class, `RichTextRun` struct, `RichTextLineMetrics` / `RichTextRunMetrics` / `RichTextLayoutOptions` structs).
- Aggregator consumers touched: 1 (`design_kit.hpp`: just include removal + comment trim).
- Test impact: 1 TEST_CASE rimosso da 1 test file (coverage equivalente in `tests/text/` subtree).
- Build-system impact: zero (no CMakeLists references pre-existing).
- Public API surface delta: 0 (only shrinkage).

## Cross-references

- [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) §P1 backlog (M1.5#10-SEQUENCE row da aprire in Step 1 commit).
- [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) §"Stato generale per area" Text Production V1 row da aggiornare in ogni step.
- [`docs/CHANGELOG.md`](../CHANGELOG.md) 4× entries da aggiungere (1 per step).
- Aggressive to ship → LICENSE/main con `wrap_push.sh` GATE-MNT-01 drop-in.
