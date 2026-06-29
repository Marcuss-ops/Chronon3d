# Single Selector Pipeline — Migration Plan

> **Versione**: 1.0 (ATOMO 1 — lock contract)
> **Snapshot**: `main@HEAD` post-landing di questo atomo
> **Regola Git**: NO BRANCHES. NO PR. SOLO `main`. UN ATOMO = UN COMMIT.
> **Push hygiene**: rebase prima del push se altri agenti hanno pushato in parallelo; `HEAD == origin/main` dopo ogni push.
> **Lingua**: italiano (allineato a `docs/STATUS.md`, `docs/NEXT_STEPS.md`, `docs/FOLLOWUP_TICKETS.md`).

## Premessa

Il sistema testuale di Chronon3D comprende attualmente una **seconda pipeline incompleta** affiancata a quella legacy, ancora attivamente utilizzata da `RangeSelector`. La nuova pipeline dichiara esplicitamente che il vecchio `TextUnitMap` resta per oltre 30 caller e che la migrazione è rimandata. Il nuovo evaluator inoltre costruisce temporaneamente uno spec legacy e richiama il vecchio evaluator: **il legacy è ancora il vero motore del range dispatcher**.

Esiste inoltre un problema strutturale di integrazione:

- `text_unit_map.cpp` e `glyph_selector_v2.cpp` non sono inclusi nel target `chronon3d_text_core`;
- `test_glyph_selector_spec.cpp` non è collegato alla lista dei core test;
- il candidate canonico (`include/chronon3d/text/glyph_selector_spec.hpp`) include l'header legacy e ridefinisce in proprio `GlyphSelectorSpec` — esattamente la dipendenza circolare da eliminare.

Questo piano vincola la convergenza a **un solo** sistema canonico, eliminando definitivamente i sistemi paralleli.

---

## (a) Architettura finale obbligatoria

```text
Source UTF-8 + PlacedGlyphRun + ParagraphLayout
                    ↓
           TextUnitMap canonico
              (otto livelli)
                    ↓
            GlyphSelectorSpec
         (variant authoring canonico)
                    ↓
               compile_selector()
                    ↓
              CompiledSelector
                    ↓
             TextAnimator runtime
                    ↓
           TextAnimatorEvaluator
                    ↓
            GlyphInstanceState[]
```

### Pipeline contract (regole di flusso)

1. **Input testuale**. Il testo sorgente UTF-8 passa per `PlacedGlyphRun` → `ParagraphLayout`.
2. **Costruzione del map canonico**. `ParagraphLayout` costruisce il **`TextUnitMap` canonico` con gli **otto livelli canonici**:
   - `Byte`
   - `CodePoint`
   - `GraphemeCluster`
   - `ShapedGlyph`
   - `Word`
   - `Line`
   - `Paragraph`
   - `SemanticSpan`
3. **Authoring**. L'autore definisce un `GlyphSelectorSpec` (variant: `RangeSelector` | `WigglySelector` | `ExpressionSelector`).
4. **Compile-once**. Una sola volta per text-run (NON per glifo, NON per frame), `compile_selector()` produce un `CompiledSelector` immutabile.
5. **Runtime resolution**. `TextAnimator` viene risolto in `CompiledTextAnimator` (selectors già compilati).
6. **Evaluation canonica**. `TextAnimatorEvaluator` itera `CompiledSelector` × glyph index → `SelectorWeight` → `GlyphInstanceState` per glyph.

### Identità canoniche (definitive)

```cpp
// UN solo TextUnitMap canonico (8 livelli)
struct TextUnitMap;        // query: unit_index(glyph, TextUnitKind), unit_count(TextUnitKind)

// UN solo GlyphSelectorSpec (variant-based)
struct RangeSelector;
struct WigglySelector;
struct ExpressionSelector;
using GlyphSelectorVariant = std::variant<RangeSelector, WigglySelector, ExpressionSelector>;
struct GlyphSelectorSpec {
    std::string id;
    GlyphSelectorVariant kind;
    std::vector<TextUnitRef> targets;
};

// UN solo CompiledSelector runtime
struct CompiledRangeSelector;
struct CompiledWigglySelector;
struct CompiledExpressionSelector;
using CompiledSelectorKind = std::variant<CompiledRangeSelector, CompiledWigglySelector, CompiledExpressionSelector>;
struct CompiledSelector {
    std::string id;
    CompiledSelectorKind kind;
    ResolvedSelectorTargets targets;
    SelectorCombineMode combine;
};
```

---

## (b) Adapter legacy boundary (UNICA compatibilità ammessa)

```text
LegacyRangeSelectorSpec
            ↓
   adapt_legacy_selector()
            ↓
   GlyphSelectorSpec canonico
```

### Regole cardinali dell'adapter

- **Mono-direzionale**: SOLO `legacy → canonical`. Mai `canonical → legacy`.
- **Funzione pura**: nessun side effect, nessuna lettura dal renderer, nessun accesso al `TextUnitMap`.
- **Senza valutazione**: niente `evaluate_*` dentro l'adapter.
- **Dipendenza canonica dal legacy**: ZERO. Il subsystem canonico **non importa** `LegacyRangeSelectorSpec`.
- **Adapter inverso**: bandito. Esiste solo `adapt_legacy_selector()`.
- **Deprecazione esplicita**: `LegacyRangeSelectorSpec` viene marcato `[[deprecated]]`.
- **Usi ammessi**: SOLO nell'adapter stesso e nei suoi test di parity.

### Test di parity (obbligatorio)

`adapt_legacy_selector(legacy)` deve produrre lo stesso risultato atteso del vecchio range per:

- tutte le shape;
- tutti gli order;
- tutti i `combine mode`;
- offset animato;
- amount animato;
- esclusione spazi;
- random seed deterministico.

---

## (c) Cose bandite

Nel codice finale **NON devono esistere**:

| Bandito | Motivo |
|---|---|
| `LegacyTextUnitMap` (struct/class) | Duplica l'identità del `TextUnitMap` canonico |
| `evaluate_selector_v2` | Suffisso `_v2` = sistema parallelo non migrato |
| `evaluate_selectors_v2` | Idem |
| Qualsiasi simbolo con suffisso `_v2` | Anti-pattern di subsystem non-migrato |
| `evaluate_legacy_selector(s)` attivo | Sostituito dall'evaluator canonico |
| Conversioni `canonical → legacy` | Inversione del flusso: bandita |
| Temporary legacy spec nel range dispatcher | Il dispatcher canonico NON deve conoscere il legacy |
| Due definizioni di `GlyphSelectorSpec` | Un candidate canonico variant-based, uno solo |
| Due implementazioni della segmentazione testuale | `TextUnitMap` è UNA sola entità |
| Nuovi caller legacy | Il legacy decade; l'adattatore è l'unica interfaccia |
| `using GlyphSelectorSpec = LegacyRangeSelectorSpec` | Alias permanente bandito |
| Compilation per glifo o per frame | `compile_selector` è UNA sola volta per text-run |
| `#include <chronon3d/text/glyph_selector.hpp>` dal runtime canonico | L'header legacy è visibile solo dall'adapter |
| `dispatch_range` → legacy synthesis + call | Il dispatcher canonico include la matematica range direttamente |

---

## (d) Definition of Done

Il piano è chiuso **solo quando** la seguente matrice è 15/15 PASS:

```text
[PASS] Esiste una sola definizione di TextUnitMap
[PASS] TextUnitMap espone tutti gli 8 livelli canonici
[PASS] Esiste un solo GlyphSelectorSpec variant-based
[PASS] Esiste CompiledSelector
[PASS] TextAnimatorEvaluator usa solo selector compilati
[PASS] Nessun simbolo con suffisso _v2
[PASS] Nessun evaluator legacy attivo
[PASS] LegacyRangeSelectorSpec entra soltanto nell'adapter
[PASS] Nessuna conversione canonical → legacy
[PASS] CMake compila tutte le sorgenti canoniche
[PASS] I test selector canonici sono realmente eseguiti
[PASS] Parity dei range selector verificata (adapter test verde)
[PASS] Determinismo verificato
[PASS] Public header check verde
[PASS] Gate architetturali verdi
```

### Verifica finale (post-atomo 12)

```bash
cmake --preset linux-ci-lean-gate
cmake --build --preset linux-ci-lean-gate --target chronon3d_core_tests
ctest --test-dir build/chronon/linux-ci-lean-gate \
  -R chronon3d_core_tests \
  --output-on-failure

bash tools/check_architecture_boundaries.sh
bash tools/check_architecture_boundaries_selftest.sh
bash tools/check_doc_sync.sh
```

Dopo la chiusura mirata, eseguire gli **11 gate della baseline** (vedi `docs/baselines/main-9c98aa7c-gates-promoted.md`) sullo stesso SHA di `main`.

---

## Sequenza operativa su `main`

| # | Atomo | Scope sintetico | Forbidden-list |
|---|---|---|---|
| 1 | Lock contract (questo documento) | `docs/TEXT_SELECTOR_SINGLE_PIPELINE_PLAN.md` | 1 file |
| 2 | Isolate legacy | rename `GlyphSelectorSpec → LegacyRangeSelectorSpec` + propagation callers | `include/chronon3d/text/glyph_selector.hpp` + tutti i caller legacy toccati |
| 3 | Promote 8-level map | cancellazione vecchio `TextUnitMap` + promozione canonico 8 livelli | `include/chronon3d/text/text_unit_map.hpp` + rimozioni/aggiornamenti |
| 4 | Consolidate variant | correzione `GlyphSelectorSpec` variant + dipendenze | `include/chronon3d/text/glyph_selector_spec.hpp` + test |
| 5 | Add CompiledSelector | nuovo subsystem canonico | `include/chronon3d/text/compiled_selector.hpp` + `src/text/compiled_selector.cpp` |
| 6 | Replace `_v2` evaluator | rename + body merge | `src/text/glyph_selector_v2.cpp` (cancellato) + `src/text/glyph_selector_evaluator.cpp` (nuovo) |
| 7 | Legacy adapter | nuovo adapter one-way + parity test | `include/chronon3d/text/legacy_selector_adapter.hpp` + `src/text/legacy_selector_adapter.cpp` + `tests/text/test_legacy_selector_adapter.cpp` |
| 8 | Migrate TextAnimatorSpec | type surface change | `include/chronon3d/text/animation/text_animator_spec.hpp` |
| 9 | Migrate TextAnimatorEvaluator | call-site rewrite | `src/text/animation/text_animator_evaluator.cpp` |
| 10 | Wire sources + tests | registration CMake | `src/text/CMakeLists.txt` + `tests/core_tests.cmake` |
| 11 | Remove parallel runtime | dead-code elimination | cancellazioni + aggiornamenti |
| 12 | Anti-regression gate | new CI denial | `tools/check_architecture_boundaries.sh` (estensione) |

Ogni atomo = 1 commit atomico. Push immediato su `main`. Rebase prima del push se altri agenti hanno pushato in parallelo. `HEAD == origin/main` dopo ogni push.

### Regola Git (vincolante, ripetuta)

```text
NO BRANCHES.
NO PR.
NO SATELLITE.
SOLO main.
UN ATOMO = UN COMMIT.
REBASE prima del push se altri agenti hanno pushato.
HEAD == origin/main dopo ogni push.
git log -n 5 dopo ogni push.
```

---

## Post-closure doc-sync

Dopo che la matrice DoD è 15/15 PASS:

1. `docs/MIGRATION_TEXT_SPEC.md` — snapshot `main@HEAD` + lista dei 12 SHA + riepilogo DoD.
2. `docs/FOLLOWUP_TICKETS.md` — chiusura ticket migrazione (riga in "Recently closed").
3. `docs/STATUS.md` — rimozione aperture P0.x text-selector.
4. `docs/NEXT_STEPS.md` — reposizionamento text-selector migration come CHIUSA post-baseline.

---

## Vincoli transversali (non negoziabili)

- **Anti-duplicazione**: nessun nuovo registry, resolver, sampler o service locator. Tutta la nuova logica usa esclusivamente l'infrastruttura canonica già presente nel repository.
- **No GUI / No browser**: il core è headless CPU-first. Nessuna nuova dipendenza GPU o browser-based.
- **No suffix `_v2`**: bandito permanentemente. Qualunque subsystem di nuova generazione usa il proprio nome canonico senza suffisso di versione.
- **Compatibilità ABI**: durante la migrazione, il `LegacyRangeSelectorSpec` viene solo `[[deprecated]]`, mai cancellato prima che l'adapter e i suoi test siano verdi.
