# TICKET-EFFECT-SCRATCH-WIRING-REGRESSION — processor context lost its session scratch

## Contesto

`SoftwareProcessorContext` espone tre workspace di sessione:

```cpp
DepthBufferPool*                          depth_buffer_pool{nullptr}; // mesh
renderer::DofScratchBuffers*              dof_scratch{nullptr};       // DOF
EffectScratchResources*                   effect_scratch{nullptr};    // scalar effects
```

Il contratto documentato (e riportato nell'audit 2026-09-09, N-17) è che i
dispatcher passino **sempre** `context.effect_scratch`, così gli effetti scalari
(radial/directional blur, stroke, offset) riusano il framebuffer di sessione e
non allocano heap per frame.

Quel contratto **non era vero**. La costruzione del contesto
(`backends::software::internal::make_processor_context`) non assegnava nessuno
dei tre puntatori, e nessun call-site li impostava dopo:

```
make_processor_context(services, extras)   ← non tocca effect_scratch
    ↑ chiamato da software_renderer.cpp e software_runtime_adapter.cpp
    ↑ entrambi costruiscono ProcessorSourceExtras senza questi campi
```

L'unica funzione che li popolava era il vecchio helper
`make_processor_context(SoftwareRenderer*)` in `software_processor_context.cpp`
(rimosso in `37dffbc0a`, "refactor: split runtime authorities"), che non aveva
più chiamanti già da TICKET-118.

### Impatto (produzione, non latente)

Poiché `effect_scratch == nullptr` su ogni path reale:

| Path | Effetto |
|---|---|
| `apply_radial_blur` | `std::make_unique<Framebuffer>(w, h)` per chiamata × frame (+ recenter buffer) |
| `apply_directional_blur` (h/v/generic) | idem |
| `apply_stroke` | 2 framebuffer heap per chiamata × frame |
| `apply_offset` | 1 framebuffer heap per chiamata × frame |
| mesh rasterization | `depth_buffer_pool == nullptr` → depth buffer allocato per frame |
| per-pixel DOF | `dof_scratch == nullptr` → scratch DOF allocato per frame |

Questo è esattamente il pattern di "fallback silenzioso" che Chronon vuole
eliminare, ed è incompatibile con il gate di steady-state
`framebuffer_allocations == 0`.

## Modifiche implementate

**File**: `src/backends/software/internal/software_processor_services.{hpp,cpp}`,
`src/backends/software/software_renderer.cpp`,
`src/runtime/software_runtime_adapter.cpp`, `tests/helpers/test_utils.hpp`,
`src/backends/software/processors/software_effect_processors.cpp`,
`src/backends/software/processors/effects/**`,
`include/chronon3d/backends/software/software_session_resources.hpp`,
`src/backends/software/software_backend.cpp`

1. **Wiring ripristinato**: `ProcessorSourceExtras` porta ora
   `effect_scratch` / `depth_buffer_pool` / `dof_scratch`; `make_processor_context`
   li propaga a `SoftwareProcessorContext`; i due call-site di produzione e
   l'helper di test li impostano da
   `renderer->software_session().software.*`.
2. **Contratto effetti irrigidito**: le quattro API scalari accettano ora
   `EffectScratchResources&` (riferimento obbligatorio, non più
   `EffectScratchResources* = nullptr`). Il ramo di fallback heap è stato
   rimosso da `radial_blur.cpp`, `directional_blur.cpp`, `stroke.cpp`,
   `fill_noise_offset.cpp`, insieme all'helper `warn_once_effect_scratch_missing`.
   Il percorso sbagliato (chiamata pool-less) non è più rappresentabile.
3. **Fail-loud**: `SoftwareBackend` asserisce che il contesto porti uno scratch
   prima di dereferenziarlo nei dispatcher.
4. **Test aggiornati**: ogni TU di test effetti possiede un
   `EffectScratchResources` riusabile e lo passa esplicitamente.

## Accettazione

| Gate | Eval |
|---|---|
| `framebuffer_allocations == 0` in steady-state con effetti attivi | da rieseguire su bench (i contatori sono già presenti) |
| Nessuna chiamata agli effetti senza scratch | garantita dal tipo (`EffectScratchResources&`) |
| Build + suite effetti/backend | vedere log di sessione |

## Nota di governance

Il finding N-17 dell'audit 2026-09-09 ("fallback corretto ma latente, i
dispatcher passano sempre scratch") era **doppio**: il fallback non era latente,
era live. La scelta precedente (warn-once) è stata superata dal contratto a
riferimento, che è ora possibile perché il wiring è stato ripristinato.
