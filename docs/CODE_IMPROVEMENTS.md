# Chronon3D — Code Improvement Opportunities

> Generato il 16/06/2026 da analisi automatica del codebase.
> Standard C++ target: **C++20** (`CMAKE_CXX_STANDARD 20`, `REQUIRED ON`).
> Ogni voce include: priorità, impatto, sforzo stimato, file interessati.

---

## 🟢 GIÀ IMPLEMENTATO (questa sessione)

| # | Cosa | Commit |
|---|------|--------|
| 1 | **Concepts C++20** su 4 template (`AnimatedValue<T>`, `compute_stagger_ranks_impl`, `register_effect_processor`, `settings_from_args`) | `c8e76f48` |
| 2 | **Ranges/views** (`std::views::keys`, `values`, `filter`, `transform`) in 5 registry file | `c8e76f48` |
| 3 | TiltSweepTitleV2: cinematic push-in reveal (2D path) + dead-code cleanup | `6eb3b6c2` |

---

## 🟡 PRIORITÀ ALTA — Da fare subito

### 1. `std::string_view` nei parametri funzione
**Impatto:** ALTO &nbsp;|&nbsp; **Sforzo:** BASSO (~30 min) &nbsp;|&nbsp; **Rischio:** Nessuno

Molte funzioni prendono `const std::string&` quando basterebbe `std::string_view`, forzando allocazioni non necessarie quando si passano string literal o `string_view` esistenti.

**Pattern da cercare:**
```cpp
// ORA — alloca se chiami con "literal"
void register_effect(EffectDescriptor descriptor);  // descriptor.id è std::string
void foo(const std::string& path);

// DOPO
void register_effect(EffectDescriptor descriptor);  // id può restare string (serve owning)
void foo(std::string_view path);                     // nessuna allocazione
```

**File candidati:**
- `include/chronon3d/assets/asset_registry.hpp`
- `include/chronon3d/scene/builders/scene_builder.hpp`
- `include/chronon3d/scene/builders/layer_builder.hpp`
- Tutti i catalog (`effect_catalog`, `shape_registry`, `sampler_registry`, `source_registry`, `graph_node_registry`)
- `include/chronon3d/api/backgrounds.hpp`
- `include/chronon3d/api/animations.hpp`

**Regola:** usa `std::string_view` se la funzione **non salva** la stringa. Usa `const std::string&` se la **salva** (es. costruttori, `emplace`).

---

### 2. `constexpr` su funzioni utility pure
**Impatto:** MEDIO &nbsp;|&nbsp; **Sforzo:** BASSO (~45 min) &nbsp;|&nbsp; **Rischio:** Nessuno

Decine di funzioni matematiche/utility sono pure ma non marcate `constexpr`, perdendo opportunità di calcolo a compile-time.

**Pattern:**
```cpp
// ORA
inline f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
inline f32 clamp(f32 v, f32 lo, f32 hi) { return v < lo ? lo : v > hi ? hi : v; }

// DOPO
inline constexpr f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
inline constexpr f32 clamp(f32 v, f32 lo, f32 hi) { return v < lo ? lo : v > hi ? hi : v; }
```

**File candidati:**
- `include/chronon3d/math/expression.hpp` (914 righe, molte funzioni pure)
- `include/chronon3d/math/color.hpp`
- `include/chronon3d/math/raster_utils.hpp`
- `include/chronon3d/math/glm_types.hpp`
- `src/effects/curves.cpp`
- `src/scene/path_utils.cpp`

**Ricerca rapida:**
```bash
rg "^(inline|static) (f32|double|int|bool|Vec2|Vec3|Color) \w+\(" include/ src/ --type cpp --type hpp -l
```

---

### 3. `[[nodiscard]]` su getter e funzioni pure
**Impatto:** MEDIO &nbsp;|&nbsp; **Sforzo:** BASSO (~20 min) &nbsp;|&nbsp; **Rischio:** Nessuno

Il codebase già usa `[[nodiscard]]` in molti punti, ma manca su diverse funzioni dove chiamare e ignorare il risultato è sicuramente un bug.

**Pattern:**
```cpp
// ORA
bool contains(std::string_view id) const;
const EffectDescriptor& get(std::string_view id) const;
std::vector<std::string> available() const;

// DOPO
[[nodiscard]] bool contains(std::string_view id) const;
[[nodiscard]] const EffectDescriptor& get(std::string_view id) const;
[[nodiscard]] std::vector<std::string> available() const;
```

**File candidati:** tutti gli header in `include/chronon3d/registry/`, `include/chronon3d/effects/`, `include/chronon3d/extension/`, `include/chronon3d/render_graph/registry/`.

---

## 🟠 PRIORITÀ MEDIA — Prossima sessione

### 4. Split `text_layout_engine.hpp` (1617 righe)
**Impatto:** MEDIO &nbsp;|&nbsp; **Sforzo:** MEDIO (1-2 ore) &nbsp;|&nbsp; **Rischio:** Basso

Il file più grosso del progetto. Contiene 4 responsabilità distinte:

| Sottosistema | Righe stimate | Header proposto |
|---|---|---|
| Bidi algorithm (UAX#9) | ~200 | `text_bidi_segmenter.hpp` (già in parte separato) |
| Line breaking (UAX#14) | ~300 | `text_line_breaker.hpp` |
| Tokenization | ~200 | `text_tokenizer.hpp` |
| Core layout engine | ~600 | `text_layout_engine.hpp` |

**Vantaggio:** chi include solo bidi non compila tutto il resto. Tempi di compilazione ridotti per rebuild incrementali.

---

### 5. IWYU — Include What You Use
**Impatto:** MEDIO &nbsp;|&nbsp; **Sforzo:** MEDIO (~1 ora) &nbsp;|&nbsp; **Rischio:** Basso

Molti file includono header non necessari, aumentando i tempi di compilazione e le dipendenze spurie.

**Tool consigliato:** `include-what-you-use` (IWYU) o diagnostica `clangd`.

**Esempio già beccato:** `graph_node_registry.cpp` includeva `<algorithm>` ma dopo la conversione a ranges non usa più nulla da lì.

**Stima guadagno:** 5-15% riduzione tempi di compilazione.

---

### 6. `std::span` al posto dei puntatori grezzi nei rasterizer
**Impatto:** MEDIO &nbsp;|&nbsp; **Sforzo:** BASSO (~30 min) &nbsp;|&nbsp; **Rischio:** Basso

Alcuni rasterizer accettano ancora puntatori grezzi + size separato invece di `std::span`:

```cpp
// ORA
void rasterize(const Color* src, int w, int h, Color* dst);
void composite(BlendMode mode, Framebuffer& dst, const Framebuffer& src, float* depth_buffer);

// DOPO (coerente con il resto del codebase che già usa std::span massicciamente)
void rasterize(std::span<const Color> src, int w, int h, std::span<Color> dst);
```

**File candidati:** `src/backends/software/rasterizers/`, `include/chronon3d/backends/software/rasterizers/`

---

### 7. Header-only → split `.hpp` / `.cpp` dove possibile
**Impatto:** MEDIO &nbsp;|&nbsp; **Sforzo:** MEDIO (~1 ora) &nbsp;|&nbsp; **Rischio:** Medio

Alcuni header contengono implementazioni che potrebbero stare in `.cpp`:

| Header | Righe | Note |
|---|---|---|
| `animated_value.hpp` | 593 | Molti metodi potrebbero essere out-of-line |
| `animation_curve.hpp` | 454 | Idem |
| `text_animator.hpp` | 495 | Idem |
| `rich_text.hpp` | 449 | Idem |

**Vantaggio:** compilazione più veloce, binary size ridotta.

**Attenzione:** i template devono restare in header. Spostare solo metodi non-template.

---

## 🔵 PRIORITÀ BASSA — Quando c'è tempo

### 8. `noexcept` su move constructor/assignment mancanti
**Impatto:** BASSO &nbsp;|&nbsp; **Sforzo:** BASSO (~15 min) &nbsp;|&nbsp; **Rischio:** Nessuno

Alcune classi con move semantics non dichiarano `noexcept`, impedendo ottimizzazioni in `std::vector`:

```cpp
// ORA
MyClass(MyClass&& other);
MyClass& operator=(MyClass&& other);

// DOPO
MyClass(MyClass&& other) noexcept;
MyClass& operator=(MyClass&& other) noexcept;
```

**File candidati:** tutte le classi con move constructor in `include/chronon3d/`.

---

### 9. Uniformare `std::size_t` vs `size_t` vs `usize`
**Impatto:** BASSO &nbsp;|&nbsp; **Sforzo:** BASSO (~15 min) &nbsp;|&nbsp; **Rischio:** Nessuno**

Il codebase usa un mix di `std::size_t`, `size_t`, e `usize` (typedef interno). Scegliere una convenzione unica.

---

### 10. `std::bit_cast` al posto di `reinterpret_cast` per type punning
**Impatto:** BASSO &nbsp;|&nbsp; **Sforzo:** BASSO (~10 min) &nbsp;|&nbsp; **Rischio:** Basso**

In `curves.cpp` c'è un pattern di type punning che potrebbe usare `std::bit_cast` (C++20):

```cpp
// ORA
const uint64_t xi = static_cast<uint64_t>(std::bit_cast<uint32_t>(pt.x));
//                               ↑ già std::bit_cast, ok!

// Cercare eventuali reinterpret_cast<float, uint32_t> rimasti
```

**Nota:** il codebase già usa `std::bit_cast` in `curves.cpp`. Verificare che non ci siano `reinterpret_cast` per type punning altrove.

---

### 11. Aggiungere `std::array` di nomi counter per debug
**Impatto:** BASSO &nbsp;|&nbsp; **Sforzo:** BASSO (~20 min) &nbsp;|&nbsp; **Rischio:** Nessuno**

`counters.hpp` usa X-macro per generare campi struct allineati (anti false-sharing). Non si può convertire in `constexpr array` senza rompere tutto, ma si può **affiancare** un array di nomi per logging/introspezione:

```cpp
constexpr std::array<std::string_view, render_counters_field_count()> kCounterNames = {
#define X(name) #name,
    CHRONON_RENDER_COUNTERS(X)
    CHRONON_RENDER_COUNTERS_SYSTEM(X)
    CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X
};
```

---

## ⚫ DA NON FARE (over-engineering)

| Cosa | Perché no |
|---|---|
| **Moduli C++20** (`import std;`) | Richiede riscrivere build system + tutti gli `#include`. Compilatori non maturi. |
| **Coroutine** (`co_await`) | Il renderer è CPU-bound, non I/O-bound. Non servono. |
| **`std::mdspan`** | È C++23, sei su C++20. `std::span` copre già il 90% dei casi. |
| **Convertire X-macro counters in `constexpr array`** | I campi hanno `alignas(64)` anti false-sharing. Un array non può replicarlo. |
| **Split `highway_color_kernels.cpp`** (1484 righe) | È SIMD generato da macro — splitting romperebbe l'ottimizzazione del compilatore. |
| **Eliminare tutti i `mutable`** | Sono tutti legittimi: mutex, cache lazy, atomics. `mutable` è la keyword corretta. |
| **Doxygen completo** | Il codice è già auto-documentante, i commenti esistenti sono buoni. |

---

## 📊 Riepilogo rapido

| # | Cosa | Priorità | Tempo |
|---|---|---|---|
| 1 | `std::string_view` nei parametri | 🟡 ALTA | 30 min |
| 2 | `constexpr` su utility pure | 🟡 ALTA | 45 min |
| 3 | `[[nodiscard]]` su getter | 🟡 ALTA | 20 min |
| 4 | Split `text_layout_engine.hpp` | 🟠 MEDIA | 1-2 ore |
| 5 | IWYU include cleanup | 🟠 MEDIA | 1 ora |
| 6 | `std::span` nei rasterizer | 🟠 MEDIA | 30 min |
| 7 | Header-only → split .hpp/.cpp | 🟠 MEDIA | 1 ora |
| 8 | `noexcept` su move | 🔵 BASSA | 15 min |
| 9 | Uniformare `size_t` | 🔵 BASSA | 15 min |
| 10 | `std::bit_cast` audit | 🔵 BASSA | 10 min |
| 11 | Array nomi counter per debug | 🔵 BASSA | 20 min |

---

## 📈 Metriche del codebase

| Metrica | Valore |
|---|---|
| Standard C++ | C++20 (required) |
| File di test | 229 |
| File sorgente | 250 |
| Rapporto test/src | 92% |
| TODO/FIXME sparsi | Solo 4 |
| File più grande | `text_layout_engine.hpp` (1617 righe) |
| Smart pointer vs raw new/delete | 173+ `make_shared`/`make_unique` vs 110 `new`/`delete` (solo in infrastruttura low-level) |
| `std::span` usage | 118+ match |
| Thread safety | `std::shared_mutex` per read-heavy, `std::atomic` per counter |
