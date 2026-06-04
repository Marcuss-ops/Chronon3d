# Chronon3d Architecture Evolution Plan

Obiettivo: trasformare Chronon3d da una codebase dove ogni modifica tende a finire nel grafo, nella scena o nei layer, a una struttura modulare dove feature, preset, exporter, nodi, effetti e ottimizzazioni possono evolvere senza pestarsi i piedi.

Questo documento e' pensato come guida operativa per sviluppatori e agenti automatici. Le regole qui sotto servono a ridurre conflitti, regressioni e modifiche che si annullano tra loro.

---

## 1. Problema Attuale

Oggi molti interventi finiscono nei file centrali:

- `include/chronon3d/render_graph/render_graph_node.hpp`
- `src/render_graph/builder/graph_builder_pipeline.cpp`
- `src/render_graph/graph_executor.cpp`
- `src/render_graph/executor/executor.cpp`
- `src/render_graph/executor/internal.cpp`
- `src/scene/layer_builder.cpp`
- `include/chronon3d/scene/builders/layer_builder.hpp`
- `include/chronon3d/scene/builders/scene_builder.hpp`
- `include/chronon3d/scene/layer/layer.hpp`
- `src/specscene/*`

Questi file sono potenti perche' stanno al centro del motore. Proprio per questo sono pericolosi: una piccola modifica puo' cambiare semantica globale, cache key, ordering dei layer, dirty rect, frame dependency, ownership dei framebuffer o comportamento dell'executor.

Il risultato pratico e':

- piu' agenti modificano gli stessi file;
- una feature risolve un problema ma ne rompe un altro;
- i cambiamenti vengono sovrascritti o resi inutili da modifiche successive;
- il core diventa un "mappazzone" di eccezioni speciali;
- le feature non hanno un posto naturale dove vivere.

---

## 2. Principio Guida

Il core deve essere piccolo, stabile e protetto.

Le feature devono entrare tramite extension point.

Regola base:

```text
Non modificare graph, scene, specscene, layer, builder o executor per aggiungere una feature normale.
Prima creare o usare un punto di estensione locale.
Toccare il core solo quando cambia un contratto architetturale.
```

---

## 3. Zone Della Codebase

### 3.1 Zona Core Protetta

Questi file vanno trattati come "engine kernel". Si modificano solo con motivazione esplicita, test e review dedicata.

```text
include/chronon3d/render_graph/render_graph_node.hpp
include/chronon3d/render_graph/render_graph.hpp
include/chronon3d/render_graph/graph_executor.hpp
include/chronon3d/render_graph/compiler/*
src/render_graph/executor/*
src/render_graph/compiler/*
src/render_graph/builder/graph_builder_pipeline.cpp
src/render_graph/builder/graph_builder_layer_pipeline.cpp
src/render_graph/builder/passes/*
src/render_graph/graph_executor.cpp
src/render_graph/render_pipeline*.cpp

include/chronon3d/scene/*
include/chronon3d/scene/builders/*
include/chronon3d/scene/layer/*
src/scene/*

src/specscene/*
tests/render_graph/*
tests/scene/*
tests/specscene/*
```

Questi file definiscono i contratti di:

- descrizione della scena;
- conversione scene -> render graph;
- semantica dei layer;
- ordinamento e compositing;
- cache e frame dependency;
- dirty rect;
- lifetime dei framebuffer;
- execution plan;
- compiled graph.

### 3.2 Zona Feature

Questi sono i posti dove gli agenti dovrebbero lavorare di default:

```text
content/*
examples/*
apps/chronon3d_cli/commands/*
apps/chronon3d_cli/utils/*
include/chronon3d/render_graph/nodes/*
src/render_graph/nodes/*
include/chronon3d/effects/*
src/effects/*
src/backends/software/processors/*
src/backends/software/rasterizers/*
src/backends/assets/*
src/backends/text/*
src/video/*
tests/effects/*
tests/video/*
tests/assets/*
tests/cli/*
tests/golden/*
```

Questi file devono poter cambiare senza richiedere modifiche al core.

### 3.3 Zona Di Integrazione

Questa zona collega core e feature. Deve esistere proprio per evitare che ogni feature tocchi il core.

Da creare o rafforzare:

```text
include/chronon3d/extension/
src/extension/
include/chronon3d/render_graph/builder/passes/
src/render_graph/builder/passes/
include/chronon3d/render_graph/registry/
src/render_graph/registry/
include/chronon3d/scene/registry/
src/scene/registry/
include/chronon3d/effects/effect_registry.hpp
src/effects/effect_registry.cpp
include/chronon3d/assets/asset_registry.hpp
```

---

## 4. Struttura Target

Struttura desiderata a medio termine:

```text
include/chronon3d/
  core/
  scene/
    model/
    builders/
    registry/
    validation/
  specscene/
    model/
    parser/
    compiler/
    validation/
  render_graph/
    model/
    nodes/
    builder/
      graph_build_context.hpp
      graph_build_pass.hpp
      graph_build_pipeline.hpp
      graph_build_registry.hpp
    compiler/
    executor/
    registry/
    validation/
  effects/
    registry/
    descriptors/
  backends/
  extension/
    extension_module.hpp
    extension_registry.hpp
    extension_context.hpp

src/
  scene/
  specscene/
  render_graph/
    builder/
      graph_build_context.cpp
      graph_build_pipeline.cpp
      graph_build_registry.cpp
      passes/
    compiler/
    executor/
    registry/
  extension/

content/
  Minimalist/
    minimalist_module.cpp
    minimalist_presets.cpp
    minimalist_assets.cpp

  register_content_modules.hpp
  register_content_modules.cpp
  register_minimalist_content.cpp
  register_text_content.cpp
  register_2d5_content.cpp

tests/
  architecture/
  render_graph/
  scene/
  extension/
```

Non serve fare tutto in una volta. La migrazione deve essere incrementale.

---

## 5. Nuovi Concetti Da Introdurre

### 5.1 `GraphBuildPass`

Il builder del grafo non deve essere un mega-file pieno di casi speciali. Deve orchestrare una sequenza di pass.

Nuovi file:

```text
include/chronon3d/render_graph/builder/graph_build_pass.hpp
include/chronon3d/render_graph/builder/graph_build_context.hpp
include/chronon3d/render_graph/builder/graph_build_pipeline.hpp
src/render_graph/builder/graph_build_pipeline.cpp
```

Interfaccia proposta:

```cpp
class GraphBuildPass {
public:
    virtual ~GraphBuildPass() = default;
    virtual std::string_view name() const = 0;
    virtual void run(GraphBuildContext& ctx, RenderGraph& graph) = 0;
};
```

Pass core iniziali:

```text
SourceBuildPass
LayerTransformPass
MaskBuildPass
EffectBuildPass
CompositeBuildPass
TrackMatteBuildPass
DepthOfFieldBuildPass
OutputBuildPass
```

File target:

```text
src/render_graph/builder/passes/source_build_pass.cpp
src/render_graph/builder/passes/layer_transform_pass.cpp
src/render_graph/builder/passes/mask_build_pass.cpp
src/render_graph/builder/passes/effect_build_pass.cpp
src/render_graph/builder/passes/composite_build_pass.cpp
src/render_graph/builder/passes/output_build_pass.cpp
```

Regola:

```text
Una nuova feature del grafo deve aggiungere un pass o un node specifico.
Non deve modificare direttamente graph_builder_pipeline.cpp salvo cambio dell'orchestrazione.
```

### 5.2 `GraphBuildRegistry`

Serve un registro dove feature e moduli possono aggiungere pass senza modificare il core builder.

Nuovi file:

```text
include/chronon3d/render_graph/builder/graph_build_registry.hpp
src/render_graph/builder/graph_build_registry.cpp
```

Responsabilita':

- registrare pass;
- ordinare pass per fase;
- impedire duplicati;
- fornire pipeline default;
- permettere moduli opzionali.

Esempio concettuale:

```cpp
registry.add_pass(GraphBuildPhase::Sources, make_source_pass());
registry.add_pass(GraphBuildPhase::Effects, make_effect_pass());
registry.add_pass(GraphBuildPhase::Output, make_output_pass());
```

### 5.3 `ExtensionModule`

Ogni pacchetto feature deve registrare cio' che offre.

Nuovi file:

```text
include/chronon3d/extension/extension_module.hpp
include/chronon3d/extension/extension_registry.hpp
include/chronon3d/extension/extension_context.hpp
src/extension/extension_registry.cpp
```

Interfaccia proposta:

```cpp
class ExtensionModule {
public:
    virtual ~ExtensionModule() = default;
    virtual std::string_view id() const = 0;
    virtual void register_with(ExtensionRegistry& registry) = 0;
};
```

Un modulo puo' registrare:

- composizioni;
- preset;
- asset provider;
- effect descriptors;
- shape descriptors;
- graph build pass;
- render graph nodes;
- CLI commands;
- exporters.

### 5.4 Registri Separati Per Dominio

Evitare un unico registro globale enorme.

Registri consigliati:

```text
CompositionRegistry       gia' presente
EffectRegistry            gia' presente
ShapeRegistry             presente in src/registry
AssetRegistry             gia' presente
GraphNodeRegistry         da creare
GraphBuildRegistry        da creare
SceneValidationRegistry   da creare
ExporterRegistry          da creare per CLI/video
```

Nuovi file:

```text
include/chronon3d/render_graph/registry/graph_node_registry.hpp
src/render_graph/registry/graph_node_registry.cpp
include/chronon3d/scene/validation/scene_validator.hpp
src/scene/validation/scene_validator.cpp
apps/chronon3d_cli/commands/video/exporter_registry.hpp
apps/chronon3d_cli/commands/video/exporter_registry.cpp
```

---

## 6. File Da Spezzare

### 6.1 `render_graph_node.hpp`

Problema:

- contiene troppa logica inline;
- mescola contesto, allocazione framebuffer, cache helpers, telemetry counters e definizione base dei nodi;
- ogni modifica al contesto costringe rebuild ampi;
- e' tra i file piu' ritoccati.

Migrazione proposta:

```text
include/chronon3d/render_graph/render_graph_context.hpp
include/chronon3d/render_graph/render_graph_node.hpp
include/chronon3d/render_graph/framebuffer_acquire.hpp
src/render_graph/framebuffer_acquire.cpp
```

Spostare fuori:

- `RenderGraphContext`;
- helper `acquire_owned_fb`;
- helper `acquire_framebuffer`;
- helper `own_to_cache`;
- contatori clear/copy.

Lasciare in `render_graph_node.hpp` solo:

- interfaccia `RenderGraphNode`;
- enum piccoli;
- metodi virtuali;
- metadata essenziali.

### 6.2 `graph_builder_pipeline.cpp`

Problema:

- diventa il magnete di tutte le feature;
- contiene conoscenza di scene, layer, sources, transforms, effects, output;
- ogni nuovo comportamento rischia di toccarlo.

Migrazione:

```text
src/render_graph/builder/graph_builder_pipeline.cpp
```

deve diventare solo:

```cpp
auto pipeline = GraphBuildPipeline::from_registry(registry);
return pipeline.build(scene, ctx);
```

La logica reale va nei pass:

```text
src/render_graph/builder/passes/*
```

### 6.3 `executor.cpp` e `internal.cpp`

Problema:

- gestione cache;
- gestione dirty rect;
- pruning;
- telemetry;
- ownership framebuffer;
- scheduling TBB;
- lifetime resource.

Migrazione consigliata:

```text
src/render_graph/executor/execution_plan.cpp
src/render_graph/executor/input_resolver.cpp
src/render_graph/executor/cache_evaluator.cpp
src/render_graph/executor/node_runner.cpp
src/render_graph/executor/telemetry_emitter.cpp
src/render_graph/executor/framebuffer_lifetime.cpp
src/render_graph/executor/tile_pruning.cpp
```

Header interni:

```text
src/render_graph/executor/execution_state.hpp
src/render_graph/executor/pre_resolved_node.hpp
src/render_graph/executor/cache_eval_result.hpp
```

Regola:

```text
executor.cpp orchestra.
I dettagli vivono in file piccoli e testabili.
```

### 6.4 Scene e Layer Builder

Problema:

- `SceneBuilder` e `LayerBuilder` tendono a diventare una DSL monolitica;
- ogni nuovo tipo visual/effect/source viene aggiunto come metodo diretto;
- agenti diversi aggiungono metodi simili o incompatibili.

Migrazione:

```text
include/chronon3d/scene/builders/scene_builder.hpp
include/chronon3d/scene/builders/layer_builder.hpp
src/scene/layer_builder.cpp
```

devono diventare facciate leggere.

Nuovi file:

```text
include/chronon3d/scene/builders/layer_command.hpp
include/chronon3d/scene/builders/layer_command_registry.hpp
src/scene/builders/layer_command_registry.cpp
include/chronon3d/scene/builders/scene_command.hpp
include/chronon3d/scene/builders/scene_command_registry.hpp
src/scene/builders/scene_command_registry.cpp
```

Feature specifiche:

```text
src/scene/builders/commands/text_commands.cpp
src/scene/builders/commands/image_commands.cpp
src/scene/builders/commands/shape_commands.cpp
src/scene/builders/commands/effect_commands.cpp
src/scene/builders/commands/camera_commands.cpp
```

---

## 7. Regole Per Agenti

Ogni agente deve seguire queste regole prima di modificare codice.

### 7.1 Prima Scelta: File Locali

Se il task riguarda:

- preset;
- contenuto;
- asset;
- exporter;
- effetto;
- render node specifico;
- test;
- CLI;

allora l'agente deve lavorare in file locali.

Esempi:

```text
content/Minimalist/*
src/backends/assets/*
src/video/*
apps/chronon3d_cli/commands/video/*
include/chronon3d/render_graph/nodes/*
src/render_graph/nodes/*
tests/<domain>/*
```

### 7.2 Core Solo Con Permesso

Prima di modificare un file protetto, l'agente deve fermarsi e scrivere:

```text
Voglio modificare: <file>
Motivo: <perche' serve>
Alternativa locale provata: <perche' non basta>
Rischi: <cache/layer/order/dirty rect/frame dependency/etc>
Test da eseguire: <lista>
```

### 7.3 Una Feature, Un Modulo

Una feature nuova deve avere una casa chiara.

Esempio buono:

```text
content/Minimalist/minimalist_module.cpp
content/Minimalist/minimalist_image_presets.cpp
tests/content/test_minimalist_presets.cpp
```

Esempio da evitare:

```text
content/Minimalist/*
src/render_graph/builder/graph_builder_pipeline.cpp
include/chronon3d/scene/builders/layer_builder.hpp
src/render_graph/executor/internal.cpp
```

### 7.4 Mai Mischiare Refactor Core E Feature

Non fare nello stesso cambio:

- nuova feature visuale;
- modifica cache;
- modifica executor;
- modifica layer ordering;
- modifica telemetry.

Separare sempre:

```text
1. refactor senza cambio comportamento
2. test di equivalenza
3. feature
4. test feature
```

---

## 8. Piano Di Migrazione

### Fase 0: Protezione Subito

Creare un file:

```text
docs/CORE_OWNERSHIP.md
```

Contenuto:

- lista file protetti;
- regole per agenti;
- test minimi;
- checklist PR.

Aggiungere in `CONTRIBUTING.md` un link a:

```text
docs/ARCHITECTURE_EVOLUTION_PLAN.md
docs/CORE_OWNERSHIP.md
```

### Fase 1: Estrarre `RenderGraphContext`

Creare:

```text
include/chronon3d/render_graph/render_graph_context.hpp
src/render_graph/render_graph_context.cpp
include/chronon3d/render_graph/framebuffer_acquire.hpp
src/render_graph/framebuffer_acquire.cpp
```

Spostare da:

```text
include/chronon3d/render_graph/render_graph_node.hpp
```

a nuovi file:

- `RenderGraphContext`;
- acquisizione framebuffer;
- copy/adopt helpers.

Test:

```text
tests/render_graph/test_render_graph_context.cpp
tests/cache/test_framebuffer_pool.cpp
tests/render_graph/pipeline/*
```

### Fase 2: Spezzare Executor

Creare:

```text
src/render_graph/executor/execution_state.hpp
src/render_graph/executor/input_resolver.cpp
src/render_graph/executor/cache_evaluator.cpp
src/render_graph/executor/node_runner.cpp
src/render_graph/executor/telemetry_emitter.cpp
src/render_graph/executor/tile_pruning.cpp
```

`executor.cpp` deve solo:

- ottenere execution plan;
- creare state;
- iterare livelli;
- chiamare helper.

Test:

```text
tests/render_graph/executor/test_cache_evaluator.cpp
tests/render_graph/executor/test_input_resolver.cpp
tests/render_graph/executor/test_tile_pruning.cpp
```

### Fase 3: Introdurre `GraphBuildPass`

✅ **Completata.**

Creati:

```text
include/chronon3d/render_graph/builder/graph_build_pass.hpp
include/chronon3d/render_graph/builder/graph_build_context.hpp
include/chronon3d/render_graph/builder/graph_build_pipeline.hpp
include/chronon3d/render_graph/builder/graph_build_registry.hpp
src/render_graph/builder/graph_build_pipeline.cpp
src/render_graph/builder/graph_build_registry.cpp
```

`GraphBuildPass` interfaccia astratta con `name()`, `run()`, `phase()`.
`GraphBuildPipeline::build()` orchestra i pass registrati.
`GraphBuildRegistry` fornisce pass di default.

Aggiunto anche `GraphBuildPipeline::build_with_resolved()` per evitare doppia chiamata a `resolve_layers()` in `scene.cpp`.

Test:

```text
tests/render_graph/builder/test_graph_build_pipeline.cpp
tests/render_graph/builder/test_graph_build_pass_order.cpp
```

### Fase 4: Estrarre Pass Dal Builder

✅ **Completata.**

Estratti 6 pass dal monolitico `graph_builder_pipeline.cpp`:

```text
src/render_graph/builder/passes/graph_builder_resolve_pass.cpp
src/render_graph/builder/passes/graph_builder_source_pass.cpp
src/render_graph/builder/passes/graph_builder_root_sources_pass.cpp
src/render_graph/builder/passes/graph_builder_layer_passes.cpp
src/render_graph/builder/passes/graph_builder_layer_pipeline_pass.cpp
src/render_graph/builder/passes/graph_builder_lighting_passes.cpp
src/render_graph/builder/passes/graph_builder_output_pass.cpp
src/render_graph/builder/passes/graph_builder_passes.hpp
```

Inoltre rimosso `build_graph()` (~300 righe dead code) da `graph_builder_pipeline.cpp`.

Ogni estrazione non ha cambiato output graph.

### Fase 5: Registry Estensioni

✅ **Completata.**

Creati:

```text
include/chronon3d/extension/extension_module.hpp
include/chronon3d/extension/extension_registry.hpp
src/extension/extension_registry.cpp
include/chronon3d/render_graph/registry/graph_node_registry.hpp
src/render_graph/registry/graph_node_registry.cpp
```

`ExtensionModule` interfaccia astratta con `id()`, `register_with()`.
`ExtensionRegistry` singleton con `add_module()` / `initialize_all()`.
`GraphNodeRegistry` per registri domain-specific.

### Fase 6: LayerCommand + Motion Presets

✅ **Completata.**

`layer_builder.cpp` ridotto da 815 → 569 righe (−30%).

Creati:

```text
include/chronon3d/scene/builders/layer_command.hpp
include/chronon3d/scene/builders/layer_command_registry.hpp
include/chronon3d/scene/builders/commands/motion_preset_commands.hpp
src/scene/builders/layer_command_registry.cpp
src/scene/builders/commands/motion_preset_commands.cpp
src/scene/builders/commands/motion_preset_methods.cpp
```

`LayerCommand` interfaccia astratta con `const apply()`.
`LayerCommandRegistry` singleton (auto-registra 15 comandi built-in nel costruttore).
15 comandi parameterless per extension modules (slide_in, soft_pop, float_idle, ecc.).

### Fase 7: Scene Validation

✅ **Completata.**

Creati:

```text
include/chronon3d/scene/validation/scene_validator.hpp
include/chronon3d/scene/validation/scene_validation_registry.hpp
src/scene/validation/scene_validator.cpp
src/scene/validation/scene_validation_registry.cpp
src/render_graph/builder/passes/graph_builder_validation_pass.cpp
src/render_graph/builder/passes/graph_builder_validation_pass.hpp
```

`SceneValidator` con 6 regole built-in:
- `layer.duplicate_name` — nomi layer duplicati
- `layer.missing_parent` — riferimenti a parent inesistenti
- `layer.zero_duration` — layer con durata zero
- `layer.track_matte_missing_source` — track matte senza source
- `camera.missing_target` — camera target inesistente
- `layer.circular_parent` — gerarchia parent circolare

`SceneValidationRegistry` singleton per regole estensibili.
`ValidationPass` wired nella fase `PreResolve` del pipeline (primo pass default).

`build_graph()` (~300 righe dead code) rimosso definitivamente.
`scene.cpp` ora usa `GraphBuildPipeline::build_with_resolved()`.

### Fase 8: ExtensionModule Content Modules

✅ **Completata.**

Ogni package di content registra le proprie composizioni tramite `ExtensionModule`, eliminando i macro statici `CHRONON_REGISTER_COMPOSITION` distribuiti nei file content.

Creati:

```text
content/Minimalist/minimalist_module.cpp      # MinimalistModule — 30 composizioni
content/text/text_module.cpp                  # TextModule — ~45 composizioni
content/2d5/two_point_five_d_module.cpp       # TwoPointFiveDModule — 19 composizioni

content/register_content_modules.hpp          # Dichiarazioni (per-module + umbrella)
content/register_content_modules.cpp          # Umbrella: delega ai 3 per-module
content/register_minimalist_content.cpp       # Solo create_minimalist_module()
content/register_text_content.cpp             # Solo create_text_module()
content/register_2d5_content.cpp              # Solo create_two_point_five_d_module()
```

**Design per-module:** Ogni file di registrazione reference solo la factory della propria libreria content, evitando errori di linking quando un test target (es. `animation_tests`) linka solo `content_anims`.

```text
register_minimalist_content()  →  richiede chronon3d_content_anims
register_text_content()        →  richiede chronon3d_content_text
register_two_point_five_d_content()  →  richiede chronon3d_content_2d5
register_content_modules()     →  delega ai 3 sopra + initialize_all()
```

**Wiring negli entry point:** `register_content_modules()` viene chiamato PRIMA della costruzione di `CompositionRegistry`, così le composizioni moduli sono nel static vector `builtin_composition_entries()` quando `populate_registered_compositions()` copia.

```text
apps/chronon3d_cli/main.cpp                     →  register_content_modules()
src/c_api/chronon3d_c_api.cpp                   →  register_content_modules()
tests/test_main.cpp                              →  per-module (guardato da compile defs)
```

**Modifiche correlate:**
- `extension_registry.hpp/.cpp` — aggiunto `register_composition()` e `has_module()` methods
- Rimossi `CHRONON_REGISTER_COMPOSITION` da ~15 content files
- `content/CMakeLists.txt` — `chronon3d_content_modules` library con tutti i file di registrazione
- `CMakeLists.txt` — compilazione diretta in `chronon3d_shared` sotto `CHRONON3D_BUILD_CONTENT`
- `tests/CMakeLists.txt` — per-module sources con compile defs granulari (`CHRONON3D_HAS_CONTENT_MINIMALIST`, `CHRONON3D_HAS_CONTENT_TEXT`, `CHRONON3D_HAS_CONTENT_2D5`)

Test:

```text
tests/test_c_api       — verifica composizioni registrate via C API
tests/animation_tests  — verifica MinimalistModule standalone
```

---

## 9. Fasi Future (Preview)

### Fase 9: Dynamic Module Loading

Obiettivo: caricare moduli `ExtensionModule` da shared library esterne (.so/.dll) al runtime.

```text
include/chronon3d/extension/extension_loader.hpp
src/extension/extension_loader.cpp
```

### Fase 10: Exporter Registry

Separare la logica di export (PNG, EXR, FFmpeg) in un registry estensibile.

```text
apps/chronon3d_cli/commands/video/exporter_registry.hpp
apps/chronon3d_cli/commands/video/exporter_registry.cpp
```

---

## 10. Test Minimi Per Toccare Il Core

Se una modifica tocca graph/scene/layer/executor, deve eseguire almeno:

```bash
cmake --build --preset linux-fast --target chronon3d_shared -j 4
```

E, quando disponibili:

```bash
ctest --test-dir build/chronon/linux-debug --output-on-failure -R "render_graph|scene|specscene|cache"
```

Test da creare o rafforzare:

```text
tests/architecture/test_protected_core_contracts.cpp
tests/render_graph/builder/test_graph_snapshot.cpp
tests/render_graph/executor/test_framebuffer_lifetime.cpp
tests/render_graph/executor/test_dirty_rect_contract.cpp
tests/render_graph/executor/test_cache_key_contract.cpp
tests/scene/test_layer_order_contract.cpp
tests/specscene/test_specscene_compile_contract.cpp
```

Contratti da coprire:

- stesso scene input produce stesso graph hash;
- layer order stabile;
- cache key include input hash, frame policy, tile info;
- dirty rect non salta pixel necessari;
- framebuffer arena non leak-a wrapper;
- executor non rilascia input prima dell'ultimo consumer;
- specscene compile non cambia semantica dei layer.

---

## 11. Checklist Per Nuova Feature

Prima di iniziare:

```text
[ ] La feature puo' stare in content/, effects/, nodes/, assets/, video/ o CLI?
[ ] Esiste un registry adatto?
[ ] Serve davvero toccare scene/layer/graph?
[ ] Il comportamento e' locale o cambia un contratto globale?
```

Durante:

```text
[ ] Aggiungere file nuovi quando possibile.
[ ] Evitare modifiche a file protetti.
[ ] Non mescolare refactor core e feature.
[ ] Aggiungere test nel dominio della feature.
```

Prima di chiudere:

```text
[ ] Build mirata passa.
[ ] Test mirati passano.
[ ] File protetti toccati? Motivazione scritta.
[ ] Nessuna modifica non correlata.
```

---

## 12. Regola Di Ownership Per Agenti

Assegnare agenti per area:

```text
Core Graph Agent
  Può toccare render_graph core, executor, compiler, cache contracts.

Scene/Layer Agent
  Può toccare scene model, layer model, builders, specscene compile.

Feature Agent
  Può toccare content, presets, effects, assets, CLI, render nodes specifici.

Performance Agent
  Può toccare backend software, framebuffer pool, SIMD, video path.

Test Agent
  Può aggiungere test ovunque, ma non cambiare core behavior.
```

Regola:

```text
Un agente non deve invadere l'area di un altro senza dichiararlo.
```

---

## 13. File Da Monitorare

Questi file sono storicamente caldi e vanno monitorati:

```text
include/chronon3d/render_graph/render_graph_node.hpp
include/chronon3d/render_graph/nodes/basic_nodes.hpp
include/chronon3d/render_graph/render_graph_hashing.hpp
src/render_graph/builder/graph_builder_pipeline.cpp
src/render_graph/graph_executor.cpp
src/render_graph/executor/executor.cpp
src/render_graph/executor/internal.cpp
src/scene/layer_builder.cpp
include/chronon3d/scene/builders/layer_builder.hpp
include/chronon3d/scene/builders/scene_builder.hpp
apps/chronon3d_cli/commands/video/video_export_pipe.cpp
src/cache/framebuffer_pool.cpp
```

Ogni settimana o prima di una release:

```bash
git log --format= --name-only --all \
  | sed '/^$/d' \
  | sort \
  | uniq -c \
  | sort -nr \
  | sed -n '1,40p'
```

Se un file core continua a salire, estrarre un extension point.

---

## 14. Direzione Finale

Chronon3d deve arrivare a questa forma:

```text
Scene DSL -> Scene Model -> Validation -> Graph Build Passes -> Render Graph -> Compiler -> Executor -> Backend
```

Ogni freccia e' un contratto. Ogni contratto deve essere testato.

Le feature devono agganciarsi a:

```text
registries
build passes
nodes
processors
exporters
content modules
```

Non direttamente a:

```text
graph core
scene core
layer core
executor internals
```

La metrica di successo e':

```text
Una nuova feature utile deve poter essere implementata aggiungendo 1-3 file locali,
senza modificare render_graph_node.hpp, graph_builder_pipeline.cpp o layer_builder.cpp.
```

