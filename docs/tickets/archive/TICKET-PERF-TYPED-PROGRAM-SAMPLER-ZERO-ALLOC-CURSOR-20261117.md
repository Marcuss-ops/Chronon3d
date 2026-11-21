# TICKET-PERF-TYPED-PROGRAM-SAMPLER-ZERO-ALLOC-CURSOR-20261117 — Eliminazione delle allocazioni per-frame (`std::string`, `std::vector` particelle) e introduzione cursore O(1) in `TypedProgramSampler::sample_into`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P1/P2 (Percorso caldo CPU per-frame su tutti i job `CompiledProgram`)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/compiler/typed_program_sampler.cpp` (righe 16–45, 64–175)
  - `include/chronon3d/compiler/frame_parameters.hpp`
- **Owner:** C3D Compiler maintainer

---

## 1. Diagnosi Esatta del Problema

L'ispezione di `src/compiler/typed_program_sampler.cpp` mostra tre inefficienze eseguite ad **ogni singolo frame** dentro `TypedProgramSampler::sample_into()`:

1. **Distruzione e riallocazione degli elementi di `block.layers` ad ogni frame** (`typed_program_sampler.cpp:64-70` e `:157-172`):
   ```cpp
   block.layers.clear();
   ...
   for (const auto& layer : program.layers()) {
       SampledLayerParameters sampled;
       ...
       sampled.elements.reserve(result.particles.size());
   ```
   `block.layers.clear()` invoca il distruttore di ogni `SampledLayerParameters`, liberando il buffer heap di `sampled.elements` (`std::vector<SampledElementParameters>`) e le stringhe nel `std::variant` `sampled.payload`. Poi nel loop ricrea un `SampledLayerParameters sampled` da zero e rialloca `sampled.elements` sull'heap ad ogni frame!
2. **Copie heap di `std::string` per ogni layer `Image` e `Video`** (`typed_program_sampler.cpp:86-94`):
   ```cpp
   case NodeKind::Image:
       sampled.payload = SampledImageParameters{
           layer.asset, layer.size, layer.radius};
       break;
   case NodeKind::Video:
       sampled.payload = SampledVideoParameters{
           layer.source.empty() ? layer.asset : layer.source,
           layer.size, layer.loop};
       break;
   ```
   Ogni path di asset > 15 caratteri (SSO limit in `libstdc++`) causa una chiamata a `operator new` + `memcpy` + `operator delete` per ogni layer immagine/video a ogni frame.
3. **Ricerca binaria `std::upper_bound` ripetuta su tutte le curve senza memoria del frame precedente** (`typed_program_sampler.cpp:31-36` e `:119-122`):
   Durante l'esportazione video sequenziale ($frame = 0, 1, 2, \dots$), `sample_track` riparte da capo con `std::upper_bound` su ogni canale animato invece di verificare in O(1) il segmento di keyframe attivo al frame precedente.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Riutilizzo in-place di `block.layers[i]` senza distruggere la capacità interna
In `src/compiler/typed_program_sampler.cpp:64-70`:
1. Sostituire `block.layers.clear()` + `push_back` con `block.layers.resize(program.layers().size())`.
2. Iterare per indice `for (std::size_t i = 0; i < program.layers().size(); ++i)` modificando in-place `SampledLayerParameters& sampled = block.layers[i];`.
3. Per ` sampled.elements`, chiamare `sampled.elements.clear()` (che mantiene la `capacity()` allocata al frame precedente!).
4. Per `SampledImageParameters` e `SampledVideoParameters`, se `std::holds_alternative<SampledImageParameters>(sampled.payload)` è già vero, aggiornare i campi con `auto& img = std::get<SampledImageParameters>(sampled.payload); img.asset.assign(layer.asset.data(), layer.asset.size());` (che riusa la capacità della `std::string` senza allocare!).

### Passo 2 — Cursore O(1) per le tracce di animazione durante lo sweep sequenziale
In `src/compiler/typed_program_sampler.cpp:16-45`:
- Per tracce con $\le 8$ keyframe, una scansione lineare di 2–4 float contigui in L1 batte `std::upper_bound` per assenza di branch misprediction; per tracce lunghe (es. curve baked o `inline_mesh->animation`), verificare prima il segmento `last_index` (o l'indice diretto `static_cast<size_t>(frame_number - samples.front().frame)` quando i sample sono densi a passo 1 frame, come avviene nelle animazioni mesh baked di CM3D!).

---

## 3. Demolition Debt

Nessuno.

---

## 4. Verifica e Criteri di Chiusura

1. **Test Zero-Allocazioni Steady-State**:
   - In `tests/runtime/test_prepared_render_job_allocations.cpp`, verificare che `sample_into()` dal 2° frame in poi su una scena mista (Image + Video + Mesh animata + Particles) faccia **0 allocazioni heap**.
2. **Parità Output**:
   - `chronon3d_core_tests` e `chronon3d_render_graph_tests` 100% PASS.
