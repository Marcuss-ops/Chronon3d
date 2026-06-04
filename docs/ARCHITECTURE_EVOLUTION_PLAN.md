# Chronon3d Architecture Evolution Plan

Stato aggiornato dopo `a3adf08`.

Questo documento ora contiene solo cio' che manca. Le fasi gia' completate sono state rimosse dal piano operativo:

- core ownership documentato in `docs/CORE_OWNERSHIP.md`;
- `GraphBuildPass`, `GraphBuildPipeline`, `GraphBuildRegistry`;
- pass del graph builder estratti;
- `ExtensionModule` / `ExtensionRegistry`;
- `GraphNodeRegistry`;
- `LayerCommand` / `LayerCommandRegistry`;
- command separati per motion preset, transform, shape e layer properties;
- `SceneValidator` / `SceneValidationRegistry`;
- moduli content per Minimalist, Text e 2.5D;
- registrazione content centralizzata.

Obiettivo residuo: completare la modularizzazione senza riaprire il mappazzone su graph, scene, layer ed executor.

---

## 1. Regola Operativa

I file core restano protetti. Le nuove feature devono usare registri, moduli, pass o nodi specifici.

Non modificare questi file senza motivazione esplicita:

```text
include/chronon3d/render_graph/render_graph_node.hpp
include/chronon3d/render_graph/render_graph.hpp
include/chronon3d/render_graph/graph_executor.hpp
src/render_graph/executor/*
src/render_graph/compiler/*
src/render_graph/builder/graph_builder_pipeline.cpp
src/render_graph/render_pipeline*.cpp
include/chronon3d/scene/*
src/scene/*
src/specscene/*
```

Se serve toccarli, scrivere prima:

```text
File:
Motivo:
Perche' non basta un extension point:
Rischi:
Test:
```

---

## 2. Mancanza Prioritaria: Estrarre `RenderGraphContext`

`RenderGraphContext` contiene ancora troppa logica inline in:

```text
include/chronon3d/render_graph/render_graph_node.hpp
```

Questo file resta uno dei punti piu' caldi perche' mescola:

- interfaccia base dei nodi;
- contesto di esecuzione;
- acquisizione framebuffer;
- helper cache;
- copy/adopt di framebuffer;
- contatori clear/copy.

### Creare

```text
include/chronon3d/render_graph/render_graph_context.hpp
src/render_graph/render_graph_context.cpp
include/chronon3d/render_graph/framebuffer_acquire.hpp
src/render_graph/framebuffer_acquire.cpp
```

### Spostare Fuori Da `render_graph_node.hpp`

```text
RenderGraphContext
RenderGraphContext::acquire_owned_fb(...)
RenderGraphContext::acquire_framebuffer(...)
RenderGraphContext::own_to_cache(...)
helper copy/adopt framebuffer
contatori clear/copy legati all'acquisizione
```

### Lasciare In `render_graph_node.hpp`

```text
RenderGraphNodeKind
CacheFramePolicy
RenderGraphNode base class
metodi virtuali dei nodi
metadata minimo del nodo
```

### Test Da Aggiungere

```text
tests/render_graph/test_render_graph_context.cpp
tests/render_graph/test_framebuffer_acquire.cpp
```

### Test Da Eseguire

```bash
cmake --build --preset linux-fast --target chronon3d_shared -j 4
ctest --test-dir build/chronon/linux-debug --output-on-failure -R "render_graph|cache"
```

---

## 3. Mancanza Prioritaria: Completare Split Executor

L'executor e' stato parzialmente modularizzato, ma resta ancora troppo concentrato tra:

```text
src/render_graph/executor/executor.cpp
src/render_graph/executor/internal.cpp
src/render_graph/executor/internal.hpp
```

Oggi l'area executor gestisce insieme:

- execution plan;
- input resolution;
- cache evaluation;
- dirty rect;
- tile pruning;
- telemetry;
- ownership framebuffer;
- lifetime risorse;
- TBB scheduling.

### Creare O Completare

```text
src/render_graph/executor/execution_state.hpp
src/render_graph/executor/input_resolver.cpp
src/render_graph/executor/cache_evaluator.hpp
src/render_graph/executor/node_runner.hpp
src/render_graph/executor/telemetry_emitter.cpp
src/render_graph/executor/telemetry_emitter.hpp
src/render_graph/executor/tile_pruning.cpp
src/render_graph/executor/tile_pruning.hpp
src/render_graph/executor/framebuffer_lifetime.cpp
src/render_graph/executor/framebuffer_lifetime.hpp
```

Alcuni file esistono gia' (`cache_evaluator.cpp`, `node_runner.cpp`). Il lavoro residuo e' separare header, ownership e test.

### Target

`executor.cpp` deve fare solo:

```text
1. ottenere o costruire execution plan
2. creare ExecutionState
3. iterare i livelli
4. chiamare helper dedicati
5. restituire output framebuffer
```

`internal.cpp` deve svuotarsi progressivamente o essere eliminato.

### Test Da Aggiungere

```text
tests/render_graph/executor/test_input_resolver.cpp
tests/render_graph/executor/test_cache_evaluator.cpp
tests/render_graph/executor/test_tile_pruning.cpp
tests/render_graph/executor/test_framebuffer_lifetime.cpp
```

---

## 4. Mancanza Prioritaria: Exporter Registry

La logica video/export e' ancora troppo accoppiata al CLI.

Area attuale:

```text
apps/chronon3d_cli/commands/video/*
apps/chronon3d_cli/utils/video/*
src/video/*
```

Serve separare il concetto di exporter dalla command CLI.

### Creare

```text
apps/chronon3d_cli/commands/video/exporter.hpp
apps/chronon3d_cli/commands/video/exporter_registry.hpp
apps/chronon3d_cli/commands/video/exporter_registry.cpp
apps/chronon3d_cli/commands/video/exporters/pipe_exporter.cpp
apps/chronon3d_cli/commands/video/exporters/chunked_exporter.cpp
apps/chronon3d_cli/commands/video/exporters/native_exporter.cpp
```

### Interfaccia Target

```cpp
class VideoExporter {
public:
    virtual ~VideoExporter() = default;
    virtual std::string_view id() const = 0;
    virtual int export_video(const VideoExportJob& job) = 0;
};
```

### Separare

```text
CLI parsing
export job model
encoder backend
frame rendering loop
telemetry output
failure reporting
```

### Bug Da Risolvere In Questa Fase

In pipe export, errori/cancel/render null non devono registrare:

```text
success=true
frames_written=total
return 0
```

File da controllare:

```text
apps/chronon3d_cli/commands/video/video_export_pipe.cpp
apps/chronon3d_cli/commands/video/video_export_chunked.cpp
```

---

## 5. Mancanza: Dynamic Module Loading

`ExtensionModule` esiste, ma i moduli sono ancora linkati staticamente.

Serve caricare moduli esterni a runtime.

### Creare

```text
include/chronon3d/extension/extension_loader.hpp
src/extension/extension_loader.cpp
tests/extension/test_extension_loader.cpp
```

### API Target

```cpp
class ExtensionLoader {
public:
    bool load_library(const std::filesystem::path& path, ExtensionRegistry& registry);
};
```

### Contratto Plugin

Ogni shared library deve esporre una factory C ABI:

```cpp
extern "C" chronon3d::ExtensionModule* chronon3d_create_extension();
```

### Vincoli

- gestione errori `dlopen` / `LoadLibrary`;
- ownership chiara del modulo;
- versione ABI;
- unload opzionale solo se sicuro;
- test con mini plugin fittizio.

---

## 6. Mancanza: Scene/SpecScene Model Separation

La validazione scena ora esiste. Resta da separare meglio:

```text
scene model
scene builder DSL
scene validation
scene compilation
specscene parser/model/compiler
```

### Target Directory

```text
include/chronon3d/scene/model/
include/chronon3d/scene/builders/
include/chronon3d/scene/validation/
src/scene/model/
src/scene/builders/
src/scene/validation/

include/chronon3d/specscene/model/
include/chronon3d/specscene/parser/
include/chronon3d/specscene/compiler/
include/chronon3d/specscene/validation/
src/specscene/model/
src/specscene/parser/
src/specscene/compiler/
src/specscene/validation/
```

### Obiettivo

`SceneBuilder` e `LayerBuilder` devono essere facciate leggere. Le nuove operazioni devono vivere in command/registry, non come nuove funzioni monolitiche.

### Creare Quando Serve

```text
include/chronon3d/scene/builders/scene_command.hpp
include/chronon3d/scene/builders/scene_command_registry.hpp
src/scene/builders/scene_command_registry.cpp
src/scene/builders/commands/camera_commands.cpp
src/scene/builders/commands/effect_commands.cpp
src/scene/builders/commands/image_commands.cpp
src/scene/builders/commands/text_commands.cpp
```

Nota: non creare questi file solo per simmetria. Crearli quando una nuova feature rischia di gonfiare `SceneBuilder` o `LayerBuilder`.

---

## 7. Mancanza: Test Contrattuali Core

Ora esistono test per `SceneValidator`, ma mancano test contrattuali per impedire regressioni sulle zone protette.

### Creare

```text
tests/architecture/test_protected_core_contracts.cpp
tests/render_graph/builder/test_graph_snapshot.cpp
tests/render_graph/builder/test_graph_build_pass_order.cpp
tests/render_graph/executor/test_dirty_rect_contract.cpp
tests/render_graph/executor/test_cache_key_contract.cpp
tests/render_graph/executor/test_framebuffer_lifetime.cpp
tests/scene/test_layer_order_contract.cpp
tests/specscene/test_specscene_compile_contract.cpp
tests/extension/test_extension_registry.cpp
tests/extension/test_graph_node_registry.cpp
```

### Contratti Minimi

```text
stessa scene -> stesso graph hash
ordine layer stabile
cache key include input hash, frame policy e tile info
dirty rect non perde pixel necessari
arena framebuffer non leak-a wrapper
executor non rilascia input prima dell'ultimo consumer
specscene compile mantiene semantica layer
extension registry non registra duplicati silenziosamente
```

---

## 8. Mancanza: Monitoraggio File Caldi

Mantenere una lista di file caldi e intervenire quando tornano a crescere.

### File Da Monitorare

```text
include/chronon3d/render_graph/render_graph_node.hpp
src/render_graph/builder/graph_builder_pipeline.cpp
src/render_graph/executor/executor.cpp
src/render_graph/executor/internal.cpp
src/render_graph/executor/internal.hpp
src/scene/layer_builder.cpp
include/chronon3d/scene/builders/layer_builder.hpp
include/chronon3d/scene/builders/scene_builder.hpp
apps/chronon3d_cli/commands/video/video_export_pipe.cpp
apps/chronon3d_cli/commands/video/video_export_chunked.cpp
src/cache/framebuffer_pool.cpp
```

### Comando

```bash
git log --format= --name-only --all \
  | sed '/^$/d' \
  | sort \
  | uniq -c \
  | sort -nr \
  | sed -n '1,40p'
```

Se un file core continua a salire, non aggiungere altra logica li'. Estrarre un extension point.

---

## 9. Checklist Per Nuove Modifiche

Prima di iniziare:

```text
[ ] La modifica puo' stare in content/, effects/, nodes/, assets/, video/ o CLI?
[ ] Esiste gia' un registry o command adatto?
[ ] Serve davvero toccare scene/layer/graph/executor?
[ ] Il comportamento e' locale o cambia un contratto globale?
```

Durante:

```text
[ ] Aggiungere file nuovi quando possibile.
[ ] Evitare file protetti.
[ ] Non mescolare refactor core e feature.
[ ] Aggiungere test nel dominio corretto.
```

Prima di chiudere:

```text
[ ] Build mirata passa.
[ ] Test mirati passano.
[ ] File protetti toccati? Motivazione scritta.
[ ] Nessuna modifica non correlata.
```

---

## 10. Ordine Consigliato

Priorita' concreta:

```text
1. Estrarre RenderGraphContext da render_graph_node.hpp
2. Completare split executor e ridurre internal.cpp/internal.hpp
3. Correggere export pipe success/failure telemetry
4. Creare ExporterRegistry
5. Aggiungere test contrattuali core
6. Aggiungere ExtensionLoader per moduli dinamici
7. Separare ulteriormente Scene/SpecScene solo quando serve
```

La metrica di successo resta:

```text
Una nuova feature deve poter essere implementata aggiungendo file locali,
senza modificare render_graph_node.hpp, graph_builder_pipeline.cpp,
layer_builder.cpp o executor internals.
```
