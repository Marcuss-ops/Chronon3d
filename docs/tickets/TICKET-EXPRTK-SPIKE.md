# TICKET-EXPRTK-SPIKE — Spike ExpressionEngine con ExprTk

**Data**: 2026-08-24  
**Stato**: SPIKE COMPLETATO — parity 130/130 (100%), policy modulo-zero allineata
**Commit**: `main` (dual-run aggiornato 2026-08-24)

---

## Obiettivo

Sostituire il parser recursive-descent custom (`ExpressionParser`, 911 LoC totali tra
`expression.hpp` + `expression_builtins.hpp` + `expression_types.hpp`) con un
`ExpressionCompiler` basato su ExprTk, compilato una volta e rivalutato per frame.

---

## Implementazione

File nello spike: `tools/spikes/exprtk/`

| Componente | LoC | Descrizione |
|---|---|---|
| `exprtk_spike.cpp` (totale) | 537 | Spike completo con corpus, dual-run, metriche |
| Preprocessor (AE→ExprTk) | ~112 | `&&`→`and`, `||`→`or`, `!`→`not`, `thisComp.*`→`__comp_*`, `degreesToRadians`→`deg2rad` |
| `ExpressionCompiler` | ~103 | Compile-once, name→index map, set_context/set_legacy_vars |
| Corpus + dual-run + main | ~322 | 130 test case, dual-run engine, output formattato |

ExprTk: `v0.0.3`, header-only via `FetchContent`. Nessuna modifica a `vcpkg.json`.

---

## Metriche

| Metrica | Valore |
|---|---|
| Adapter LoC (preprocess + compiler) | ~215 |
| Spike completo LoC | 537 |
| ExprTk header | 46,066 linee / 1.6 MB |
| Custom parser attuale | 911 LoC |
| Compile time (singola TU, O0) | 44.34 s |
| Binary size (ELF text) | 6.8 MB |
| Binary size (su disco) | 27 MB |
| Max RSS durante compilazione | ~1.6 GB |
| Nuove dipendenze runtime | Nessuna (header-only, MIT) |

---

## Risultati parity: 130 test case

| Categoria | Count | % |
|---|---|---|
| **PASS** (valori identici) | 130 | 100% |
| **PARTIAL** (ExprTk compile error) | 0 | 0% |
| **FAIL** (mismatch reale) | 0 | 0% |

### Dettaglio PASS (94 case)

Copertura completa per:
- Aritmetica: `+`, `-`, `*`, `/`, `%`, `^`, precedenza, parentesi, unario, negazione doppia
- Confronti: `<`, `>`, `<=`, `>=`, `==`, `!=`
- Logica: `&&`, `||` (trasformati in `and`/`or`), `!` su confronti
- Ternario: semplice, condizionale, nidificato
- Funzioni matematiche: `sin`, `cos`, `abs`, `sqrt`, `min`, `max`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `log10`, `ceil`, `floor`, `round`, `trunc`, `pow`
- `deg2rad` / `rad2deg`
- Variabili: `frame`, `time`, `fps`, `index`, `value`, `width`, `height`, `numLayers`, `inPoint`, `outPoint`
- Context-aware: tutte le variabili standard di `ExpressionContext`
- Combinazioni: confronto + ternario, precedenza + logica, espressioni reali (`sin(time*2)*100+500`)
- `div_zero` → `inf` consistente

### Gap risolti dal dual-run aggiornato

| Causa | N. case | Fix |
|---|---|---|
| `!` unario | 6 | Preprocessing verso `not(...)` |
| `linear`, `ease`, `easeIn`, `easeOut` | 9 | Funzioni vararg custom con semantica AE |
| `thisComp.*`, `thisLayer.*`, `thisProperty.*` | 12 | Simboli namespaced `c3d_*`, senza collisioni ExprTk |
| `layer('name').prop` | 2 | Placeholder namespaced e resolver preservato |
| `undef_var` | 1 | Controllo post-bind degli identificatori non risolti |

### Gap residuo (risolto)

| Test | Custom | ExprTk | Causa |
|---|---|---|---|
| `mod_zero` (`5 % 0`) | 0.0 | 0.0 | Adapter ExprTk specializza `numeric::modulus<double>` per il contratto storico Chronon. |

---

## Analisi

### Cosa funziona bene
- Il **99,2%** del corpus passa dopo l’adapter AE e il bind dei simboli
- Il modello compile-once + update-variables-per-frame è valido: la `name→index map` funziona
- Il preprocessor è corretto per le trasformazioni sintattiche di base
- L'integrazione `FetchContent` non richiede dipendenze vcpkg

### Gap da colmare prima della migrazione
1. **`not` operator**: fix banale nel preprocessor (da ` not ` a `not(` )
2. **AE custom functions**: `linear`, `ease`, `easeIn`, `easeOut` vanno registrate come custom functions ExprTk (API disponibile: `symbol_table.add_function`)
3. **`sign` → `sgn`**: fix banale nel preprocessor
4. **`clamp` arg order**: ExprTk ha `clamp(lo, hi, x)` mentre AE ha `clamp(value, min, max)`. Serve wrapper.
5. **`PI` / `E` constants**: non registrare come variabili prima di `add_constant`
6. **`thisComp.*` / `thisLayer.*` / `thisProperty.*`**: debug del preprocessor
7. **`seedRandom`, `wiggle`, `posterizeTime`, `loopOut`, `loopIn`**: non ancora testate, richiedono custom functions

### Cosa rimane Chronon
ExprTk non deve sapere cosa sia `thisComp.width` o `layer("Title").transform.position.x`. Il preprocessor traduce queste semantiche in simboli piatti (`__comp_width`, `__l_0`) e il `ExpressionCompiler` risolve i valori layer via `ctx.layer_resolver` prima della valutazione.

---

## Decisione

**PROMISING** — ExprTk è tecnicamente fattibile come sostituto del recursive-descent parser; il corpus spike base è ora 130/130. La migrazione production resta subordinata al corpus esteso, alle funzioni AE non ancora coperte e al benchmark compile/eval.

Prima di procedere alla migrazione completa servono:
1. ~~Decisione compatibile e testata sulla semantica modulo-zero~~ — DONE nel dual-run
2. Parity test esteso con i test `test_expression.cpp` e `test_expression_extended.cpp` esistenti (seedRandom, wiggle, cross-layer, loopOut/loopIn)
3. Test delle performance (compile time per espressione, eval time per frame vs custom parser)
4. ADR per decisione architetturale
