# TICKET-PERF-ANIMATED-VALUE-EXPRESSION-ZERO-ALLOC-20261117 — Eliminazione di `split_expr_args` (`std::vector<std::string>`) e `std::unordered_map` per-frame in `evaluate_solid_color_expression`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Eliminazione allocazioni heap nel loop di valutazione espressioni animate)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/animations/animated_value.cpp` (righe 13–86)
  - `include/chronon3d/animation/core/detail/animated_value_expressions.hpp`
- **Owner:** C3D Animation maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/animations/animated_value.cpp:57-77`, dentro `evaluate_solid_color_expression()`:

```cpp
const size_t inner_start = kSolidPrefix.size();
const size_t inner_len = close - 1 - inner_start;
const std::string inner(expr.substr(inner_start, inner_len));
const auto args = split_expr_args(inner);

if (args.size() != 4)
    return std::nullopt;

const std::unordered_map<std::string, double> vars{
    {"frame", frame},
    {"time", t},
    {"fps", static_cast<double>(fps)},
    {"index", static_cast<double>(ctx.index)},
};
```

**Difetto verificato**:
Ad ogni singolo frame in cui viene valutato un `FillStyle` o `StrokeStyle` con espressione `solid(r, g, b, a)`:
1. Viene costruita una `std::string inner` e un `std::vector<std::string> args` (fino a 6 allocazioni sull'heap);
2. Viene allocata da zero una **`std::unordered_map<std::string, double>`** con 4 elementi (allocazione array di bucket + 4 nodi concatenati sull'heap via `operator new`, e immediata `operator delete` 15 righe più sotto!).

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Parsing `string_view` stack-only per i 4 argomenti di `solid(...)`
In `src/animations/animated_value.cpp`:
1. Scrivere un helper interno `bool split_solid_4_args(std::string_view inner, std::array<std::string_view, 4>& out) noexcept` che scansiona `inner` bilanciando le parentesi `(` e `)` e popola le 4 slice `std::string_view` **sullo stack, con 0 allocazioni heap**.
2. Mantenere `detail::split_expr_args(const std::string&)` solo se referenziata da test esterni (verificare con `git grep split_expr_args`).

### Passo 2 — Evitare la ricostruzione di `std::unordered_map` ad ogni chiamata
1. Se `math::evaluate_expression` richiede `const std::unordered_map<std::string, double>&`, verificare se tutti gli argomenti di `solid(r, g, b, a)` sono semplici letterali numerici (es. `"1.0"`, `"0.5"`): in quel caso `std::from_chars` converte il float in ~5 nanosecondi senza nemmeno costruire la mappa `vars` né invocare il parser di espressioni!
2. Quando un argomento contiene effettivamente variabili (`frame`, `time`, `fps`, `index`), riutilizzare una `thread_local std::unordered_map<std::string, double> tls_vars` pre-popolata con le 4 chiavi, aggiornando solo i 4 `double` per riferimento (`tls_vars["frame"] = frame; ...`) → **0 allocazioni di nodi hash-map in steady-state**.

---

## 3. Demolition Debt

Nessuno.

---

## 4. Verifica e Criteri di Chiusura

1. **Zero Allocazioni Heap**:
   - Valutare `evaluate_solid_color_expression("solid(0.1, 0.2, 0.3, 1.0)", ...)` e `evaluate_solid_color_expression("solid(frame/100.0, 0.2, 0.3, 1.0)", ...)` per 1.000 iterazioni: **0 allocazioni heap** dopo la prima chiamata.
2. **Suite di Test**:
   - `chronon3d_core_tests` verde.
