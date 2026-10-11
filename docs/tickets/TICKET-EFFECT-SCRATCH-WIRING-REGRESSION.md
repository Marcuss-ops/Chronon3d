# TICKET-EFFECT-SCRATCH-WIRING-REGRESSION — processor context lost its session scratch

Status: `DONE` (2026-10-11, Linux CPU same-tree evidence)

## Verifica — 2026-10-11

- Re-run diretto del binario corrente
  `.tmp/chronon-builds/linux-fast-dev/tests/chronon3d_effects_tests`: **276/276
  casi e 5.532.873/5.532.873 asserzioni PASS**, exit 0. I due messaggi
  `[native-blur] ... native surfaces are not supported` appartengono ai test
  del percorso nativo non supportato dal backend software; non hanno causato
  skip o fallimenti.
- Il target `chronon3d_effects_tests` è stato ricostruito nel preset
  `linux-fast-dev`; CTest registrato passa `1/1` (8,08 s). La suite include i
  test effects/context e i casi CPU di radial/directional blur, stroke e
  offset con `EffectScratchResources`.
- Aggiunta una regressione d'integrazione a
  `tests/runtime/test_prepared_render_job.cpp`: il `RenderEngine` CPU renderizza
  la stessa composizione con e senza `RadialBlur`; l'hash prova che l'effetto è
  attivo. Dopo due frame di warm-up, 30 frame consecutivi lasciano invariati
  `framebuffer_allocations` e `framebuffer_bytes_allocated`.
- Build `chronon3d_core_tests` riuscita nel preset `linux-fast-dev`; il caso
  mirato passa `37/37` assertion. La suite effetti CPU passa inoltre `1/1`
  CTest. Il criterio di allocazione steady-state è quindi verificato sul
  percorso software end-to-end, senza GPU.

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

Prima della correzione, quel contratto **non era vero**. La costruzione del
contesto (`backends::software::internal::make_processor_context`) non assegnava
nessuno dei tre puntatori, e nessun call-site li impostava dopo:

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

Poiché `effect_scratch == nullptr` su ogni path reale prima della correzione:

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
| Delta di `framebuffer_allocations` e `framebuffer_bytes_allocated` dopo warm-up, con effetto attivo | PASS: invariato per 30 frame nel test d'integrazione Linux CPU; il contatore include le allocazioni precedenti al warm-up |
| Nessuna chiamata agli effetti senza scratch | PASS: API con `EffectScratchResources&`; il test d'integrazione renderizza e verifica l'effetto attivo |
| Build + suite effetti/runtime | PASS: `chronon3d_effects_tests` CTest 1/1; caso `PreparedRenderJob` 37/37 assertion |

## Nota di governance

Il finding N-17 dell'audit 2026-09-09 ("fallback corretto ma latente, i
dispatcher passano sempre scratch") era **doppio**: il fallback non era latente,
era live. La scelta precedente (warn-once) è stata superata dal contratto a
riferimento, che è ora possibile perché il wiring è stato ripristinato.
