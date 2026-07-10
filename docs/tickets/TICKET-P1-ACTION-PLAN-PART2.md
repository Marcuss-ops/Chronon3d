# TICKET-P1-ACTION-PLAN-PART2 — Piano d'azione P1 #7–#12 (2026-07-02)

## Stato

6 nuovi P1 censiti sul commit `8976908a`. Tutti richiedono baseline verde prima dell'implementazione.
Feature Freeze V0.1 attivo (2026-06-29) — solo documentazione consentita in questa fase.

---

## P1 #7 — Il sistema asset resta globalmente mutabile

### Bug

`RenderRuntime` possiede correttamente un `AssetResolver` locale, ma continua a esportare API globali:

```cpp
set_process_wide_assets_root();
process_wide_assets_root();
process_wide_resolver();
```

L'implementazione usa:
- stringa globale
- mutex globale
- resolver statico lazy
- first-mount semantics

`RenderEngine::assets_root()` restituisce il valore globale, non quello posseduto dal proprio runtime.

**Scenario di conflitto:**

```cpp
Engine A → /assets/project-a
Engine B → /assets/project-b
// l'ultimo writer modifica il valore osservato dall'altro engine
```

Il resolver statico è ancora peggiore: una volta montato sul primo root, `has_mount()` impedisce di applicare root successivi.

### Azioni

1. [ ] Rimuovere progressivamente il fallback globale: `RenderRequest → RenderSession → RenderGraphContext → AssetResolver&`
2. [ ] Il deep code senza resolver deve fallire con un errore di dependency injection, non leggere uno stato di processo
3. [ ] Deprecare `set_process_wide_assets_root()`, `process_wide_assets_root()`, `process_wide_resolver()`
4. [ ] `RenderEngine::assets_root()` deve restituire il valore posseduto dal proprio runtime
5. [ ] Test: due engine con root diversi non interferiscono

### File interessati

- `src/assets/asset_resolver.cpp`
- `src/core/` (API globali)
- `include/chronon3d/` (header pubblici con API globali)
- `RenderEngine` implementation

### Categoria Feature Freeze

❌ Bloccato (refactor architetturale significativo). Richiede baseline verde.

---

## P1 #8 — Il text renderer resta un hotspot monolitico

### Bug

`text_run_processor.cpp` supera le 900 linee e gestisce nello stesso translation unit:

- font resolution
- scratch pooling
- bbox
- supersampling
- blur
- shadow
- stroke
- fill
- crossfade
- compositing
- diagnostics

`draw_text_run()` è orchestratore a otto stage, ma il tier renderer rimane una grande lambda catturante perché estrarlo richiederebbe troppi parametri — segnale che manca un context tipizzato.

### Allocazioni nel draw

Per ogni chiamata vengono ricreati:

```cpp
std::vector<FontFaceHandle> span_handles;
std::vector<BLFont> span_fonts;
std::vector<std::size_t> per_glyph_span_idx;
```

Vengono inoltre costruiti: 5 vector per blur tier, array indici glyph, superfici supersampled, possibili resize del blur buffer.

La risoluzione dei font e la creazione dei `BLFont` avvengono ancora nel draw, pur essendo dati largamente compilabili/pre-risolubili.

### Lifetime raw

`TextRunShape` conserva `FontEngine* engine;` e delega al layer la responsabilità di tenerlo vivo.

### Singleton cache ancora vivo

`shared_text_layout_cache()` è ancora presente (documentato come callsite da migrare).

### Intervento proposto

Introdurre:

```cpp
struct CompiledTextRun {
    SharedTextRunLayout layout;
    std::vector<ResolvedFontSpan> resolved_fonts;
    std::vector<uint32_t> glyph_to_span;
    BlurTierPlan blur_plan;
};
```

Il render hot path deve consumare un piano già compilato e usare soltanto scratch riutilizzabile.

### Azioni

1. [ ] Progettare `CompiledTextRun` con tutti i dati pre-risolti
2. [ ] Estrarre la lambda di draw in una funzione standalone con context tipizzato
3. [ ] Spostare font resolution + BLFont creation fuori dal draw path
4. [ ] Pre-compilare blur plan e glyph-to-span mapping
5. [ ] Sostituire `FontEngine*` raw in `TextRunShape` con riferimento posseduto
6. [ ] Eliminare `shared_text_layout_cache()` dopo migrazione callsite
7. [ ] Test: draw_text_run con CompiledTextRun pre-risolto produce output identico al percorso attuale

### File interessati

- `src/backends/software/processors/text_run/text_run_processor.cpp`
- `include/chronon3d/text/text_run_shape.hpp`
- `src/text/text_run.cpp` (shared_text_layout_cache)

### Categoria Feature Freeze

❌ Bloccato (refactor architetturale maggiore + nuova struttura dati). Richiede baseline verde.

---

## P1 #9 — RenderRuntime è diventato un service locator molto ampio

### Bug

`RenderRuntime` possiede correttamente molte risorse, ma le espone attraverso percorsi multipli:

- `RenderServices`, un bundle di puntatori
- accessor diretti: `node_cache()`, `executor()`, `scheduler()`
- alcuni accessi attraverso `SoftwareRenderer`
- altri accessi attraverso `RenderSession`

Questo moltiplica le possibili dipendenze e rende facile scegliere il percorso sbagliato.

### Costruzione fragile

```
construct runtime
construct renderer
construct backend
attach backend
construct pipeline
```

Il CLI ha già dimenticato `attach_backend()` — dimostra che l'oggetto può esistere in uno stato incompleto.

I due costruttori di `RenderEngine::Impl` duplicano quasi integralmente la costruzione dei servizi e del backend.

### Intervento proposto

`RenderRuntime` deve uscire dalla factory già valido:

```cpp
static Result<RenderRuntime, RuntimeBuildError>
RenderRuntime::create(RuntimeConfig);
```

Dopo la costruzione:

- `backend_attached() == true`
- servizi validi
- cataloghi congelati
- resolver configurato

Nessun `attach_backend()` pubblico durante il normale uso.

### Azioni

1. [ ] Introdurre `RenderRuntime::create(RuntimeConfig)` come unico costruttore pubblico
2. [ ] Unificare i due costruttori di `RenderEngine::Impl`
3. [ ] Rimuovere `attach_backend()` pubblico (diventa interno alla factory)
4. [ ] Ridurre la superficie di `RenderServices` — esporre solo interfacce tipizzate
5. [ ] Test: `RenderRuntime::create()` produce oggetto sempre valido (backend_attached + servizi pronti)
6. [ ] Test: due runtime indipendenti non condividono stato

### File interessati

- `include/chronon3d/runtime/` (RenderRuntime, RenderServices)
- `src/runtime/` (implementazione runtime)
- `RenderEngine` implementation
- CLI commands

### Categoria Feature Freeze

❌ Bloccato (refactor API pubblica). Richiede baseline verde.

---

## P1 #10 — Frame rate hardcoded nella pipeline

### Bug

`RenderPipeline::render_scene()` e `debug_graph()` passano sempre `30.0f`.

Anche l'API graph assegna 30 fps come default.

Questo può contaminare:

- animazioni sub-frame
- motion blur
- video decoding
- expression time
- temporal cache keys
- simulazioni stateful

### Intervento proposto

Il frame rate deve essere obbligatorio nel `RenderRequest`:

```cpp
struct RenderRequest {
    Frame frame;
    FrameRate frame_rate;   // obbligatorio
    Dimensions dimensions;
    RenderPolicy policy;
};
```

Nessun default nascosto nel core.

### Azioni

1. [ ] Aggiungere `FrameRate frame_rate` a `RenderRequest` (campo obbligatorio, nessun default)
2. [ ] Rimuovere l'hardcode `30.0f` da `RenderPipeline::render_scene()` e `debug_graph()`
3. [ ] Propagare `frame_rate` attraverso tutta la pipeline: animazioni, cache key, video, expressions
4. [ ] Rimuovere default 30 fps dall'API graph
5. [ ] Test: render con 24, 30, 60 fps producono output coerenti e deterministici
6. [ ] Test: specificare frame_rate diverso dall'animazione produce errore esplicito

### File interessati

- `src/render_graph/render_pipeline.cpp`  <!-- drift-allow: stale-ref -->
- `include/chronon3d/render_graph/render_request.hpp`  <!-- drift-allow: stale-ref -->
- `src/animations/animated_value.cpp`
- API graph

### Categoria Feature Freeze

❌ Bloccato (modifica API pubblica + refactor multi-modulo). Richiede baseline verde.

---

## P1 #11 — Timeline e composizione hanno troppi percorsi concorrenti

### Bug

Coesistono concettualmente:

```
Composition::evaluate()
compile_composition() + evaluate()
graph::render_composition_frame()
runtime::RenderPipeline::render_composition()
RenderEngine::render()
sdk::RenderEngine::render()
```

Questo rende difficile stabilire:

- chi possiede la sessione
- chi valuta la camera
- chi applica motion blur
- chi trasforma errori
- chi genera cache key

### Shared pointer non proprietario

`compile_composition()` conserva un `shared_ptr` non proprietario con deleter vuoto verso una reference fornita dal chiamante. Se il definition originale muore, il compiled object resta con un puntatore pendente.

### Fingerprint su memoria grezza

Nello stesso file il fingerprint della composizione viene calcolato sulla memoria grezza di tipi non banali come `CompositionSpec` e `CameraDescriptor`, includendo potenzialmente puntatori, padding e layout della STL.

### Intervento proposto

Un solo percorso canonico:

```
CompositionDefinition
→ CompositionCompiler
→ CompiledComposition
→ CompositionEvaluator + RenderSession
→ EvaluatedFrame
→ FrameGraphCompiler
→ GraphExecutor
→ RenderOutput
```

`CompiledComposition` deve possedere realmente la definizione immutabile necessaria.

### Azioni

1. [ ] Unificare tutti i percorsi in un'unica pipeline canonica documentata
2. [ ] `CompiledComposition` possiede OWN copy della definizione (no shared_ptr non proprietario)
3. [ ] Sostituire fingerprint su memoria grezza con hash deterministico per campo
4. [ ] Deprecare `Composition::evaluate()` e gli altri percorsi concorrenti
5. [ ] Test: compiled composition sopravvive alla distruzione del definition originale
6. [ ] Test: fingerprint identico su due macchine diverse per la stessa composizione

### File interessati

- `src/animation_compositions.cpp`  <!-- drift-allow: stale-ref -->
- `include/chronon3d/animation_compositions.hpp`  <!-- drift-allow: stale-ref -->
- `content/animation_compositions.cpp`
- `src/render_graph/`
- `src/runtime/`

### Categoria Feature Freeze

❌ Bloccato (refactor architetturale maggiore + cambio API). Richiede baseline verde.

---

## P1 #12 — CMake e packaging sono eccessivamente delicati

### Bug

L'SDK usa:

- numerosi OBJECT target
- un archivio statico aggregato
- un merge custom tramite `ar`
- differenze tra build interface e install interface
- manifest e registry separati

La documentazione segnala che con CMake 3.25 l'archivio statico conterrebbe soltanto il marker object senza il workaround manuale.

Diversi target espongono `include_private` tramite `INTERFACE`, rendendo quei file dipendenze transitive dei consumer interni. Questo spiega la continua rotazione tra:

```
private header → public header → internal header → nuovamente public per install consumer
```

### Intervento proposto

- baseline minima CMake 3.27 o superiore
- eliminazione del merge manuale con `ar`
- nessun `include_private` in `INTERFACE`
- public manifest generato dal registry, non mantenuto separatamente
- test di installazione su un progetto esterno reale

### Azioni

1. [ ] Alzare `cmake_minimum_required` a 3.27
2. [ ] Eliminare il merge manuale `ar` in `cmake/sdk_archive_merge.cmake`
3. [ ] Rimuovere tutti gli `include_private` da target `INTERFACE`
4. [ ] Generare public manifest dal registry (non file separato mantenuto a mano)
5. [ ] Verifica: `tools/install_consumer_test.sh` passa
6. [ ] Verifica: progetto esterno reale compila e linka l'SDK senza header interni

### File interessati

- `cmake/sdk_archive_merge.cmake`
- `cmake/Chronon3DPublicHeaders.cmake`
- `cmake/Chronon3DRegistry.cmake`
- `CMakeLists.txt` (root e src/)
- Vari `CMakeLists.txt` con `INTERFACE include_private`

### Categoria Feature Freeze

⚠️ Parzialmente consentito. Fix di build (categoria 1). Richiede comunque baseline verde per la parte di refactor CMake più ampia.

---

## Ordine di esecuzione consigliato (P1 #7–#12)

| Step | P1 | Azione | Categoria Freeze |
|------|-----|--------|-----------------|
| 1 | #12 | CMake minimum 3.27 + eliminazione merge ar | ⚠️ Consentito (build-fix) |
| 2 | #12 | Rimozione include_private da INTERFACE | ⚠️ Consentito (build-fix) |
| 3 | #12 | Public manifest generato da registry | ⚠️ Consentito (build-fix) |
| 4 | #12 | Verifica install_consumer_test.sh | ⚠️ Consentito |
| 5 | #7 | Deprecare API globali asset resolver | ❌ Post-baseline |
| 6 | #10 | Aggiungere FrameRate a RenderRequest | ❌ Post-baseline |
| 7 | #7 | Dependency injection AssetResolver nella pipeline | ❌ Post-baseline |
| 8 | #11 | Unificare percorsi timeline/composizione | ❌ Post-baseline |
| 9 | #8 | Introdurre CompiledTextRun + refactor draw_text_run | ❌ Post-baseline |
| 10 | #9 | RenderRuntime::create(RuntimeConfig) + rimozione attach_backend | ❌ Post-baseline |
| 11 | #10 | Propagare frame_rate in tutta la pipeline | ❌ Post-baseline |
| 12 | #11 | CompiledComposition con OWN copy + hash deterministico | ❌ Post-baseline |
