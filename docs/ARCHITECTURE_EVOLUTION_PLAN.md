# Chronon3d Architecture Evolution Plan

Stato aggiornato: refactor completato, fasi 1-7 concluse. Questo documento contiene solo le priorita' residue.

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
src/render_graph/builder/graph_build_pipeline.cpp
src/render_graph/render_pipeline*.cpp
include/chronon3d/scene/model/*
include/chronon3d/scene/builders/*
include/chronon3d/scene/validation/*
include/chronon3d/specscene/model/*
src/scene/*
src/scene/model/*
src/specscene/model/*
src/specscene/parser/*
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

## 2. Priorita' Residue

### 2.1 Completato: Estrarre RenderGraphContext

`RenderGraphContext` e' stato separato da `render_graph_node.hpp`.

**Stato attuale:**

```text
include/chronon3d/render_graph/render_graph_context.hpp
include/chronon3d/render_graph/framebuffer_acquire.hpp
src/render_graph/framebuffer_acquire.cpp
```

`render_graph_node.hpp` deve restare limitato a:

```text
RenderGraphNodeKind
CacheFramePolicy
RenderGraphNode base class
metodi virtuali dei nodi
metadata minimo del nodo
```

**Test da mantenere:**

```text
tests/architecture/test_protected_core_contracts.cpp
tests/render_graph/executor/test_framebuffer_lifetime.cpp
```

---

### 2.2 Completato: Split Executor

L'executor e' stato modularizzato in file per responsabilita'. `executor.cpp` mantiene API pubblica, piano di esecuzione e orchestration; la logica di dominio vive nei file dedicati.

```text
src/render_graph/executor/executor.cpp
src/render_graph/executor/internal.cpp
src/render_graph/executor/execution_state.hpp
src/render_graph/executor/input_resolver.cpp
src/render_graph/executor/cache_evaluator.hpp
src/render_graph/executor/cache_evaluator.cpp
src/render_graph/executor/executor_levels.hpp
src/render_graph/executor/executor_levels.cpp
src/render_graph/executor/framebuffer_lifetime.hpp
src/render_graph/executor/framebuffer_lifetime.cpp
src/render_graph/executor/node_runner.hpp
src/render_graph/executor/node_runner.cpp
src/render_graph/executor/telemetry_emitter.hpp
src/render_graph/executor/telemetry_emitter.cpp
src/render_graph/executor/tile_pruning.hpp
src/render_graph/executor/tile_pruning.cpp
```

Ogni nuova logica executor deve restare in un file dedicato, non in `executor.cpp`.

`executor.cpp` deve fare solo:

```text
1. ottenere o costruire execution plan
2. creare ExecutionState
3. iterare i livelli
4. chiamare helper dedicati
5. restituire output framebuffer
```

`internal.cpp` e' vuoto di fatto e deve restare senza nuova logica.

**Test da aggiungere:**

```text
tests/render_graph/executor/test_input_resolver.cpp
tests/render_graph/executor/test_cache_evaluator.cpp
tests/render_graph/executor/test_tile_pruning.cpp
tests/render_graph/executor/test_framebuffer_lifetime.cpp
```

---

### 2.3 Rifinire Export Video CLI

La logica video/export e' stata alleggerita ma resta accoppiata al CLI.

**Creare:**

```text
apps/chronon3d_cli/commands/video/exporter.hpp
apps/chronon3d_cli/commands/video/exporter_registry.hpp
apps/chronon3d_cli/commands/video/exporter_registry.cpp
apps/chronon3d_cli/commands/video/exporters/pipe_exporter.cpp
apps/chronon3d_cli/commands/video/exporters/chunked_exporter.cpp
apps/chronon3d_cli/commands/video/exporters/native_exporter.cpp
```

**Interfaccia target:**

```cpp
class VideoExporter {
public:
    virtual ~VideoExporter() = default;
    virtual std::string_view id() const = 0;
    virtual int export_video(const VideoExportJob& job) = 0;
};
```

**Bug da risolvere:** In pipe export, errori/cancel/render null non devono registrare success=true.

---

### 2.4 Hardening Dynamic Module Loading

`ExtensionModule` ed `ExtensionLoader` esistono. Il lavoro residuo e' hardening runtime.

**Test da creare:**

```text
tests/extension/test_extension_loader_failure_modes.cpp
tests/extension/test_extension_loader_abi_contract.cpp
```

**Contratto plugin:**

```cpp
extern "C" chronon3d::ExtensionModule* chronon3d_create_extension();
```

**Vincoli:**

- Gestione errori `dlopen` / `LoadLibrary`
- Ownership chiara del modulo
- Versione ABI
- Unload opzionale solo se sicuro

---

### 2.5 Test Contrattuali Core

**Test da creare:**

```text
tests/architecture/test_protected_core_contracts.cpp
tests/render_graph/builder/test_graph_snapshot.cpp
tests/render_graph/builder/test_graph_build_pass_order.cpp
tests/render_graph/executor/test_dirty_rect_contract.cpp
tests/render_graph/executor/test_cache_key_contract.cpp
tests/scene/test_layer_order_contract.cpp
tests/specscene/test_specscene_compile_contract.cpp
tests/extension/test_extension_registry.cpp
tests/extension/test_graph_node_registry.cpp
```

**Contratti minimi:**

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

## 3. Monitoraggio File Caldi

Mantenere una lista di file caldi e intervenire quando tornano a crescere:

```text
include/chronon3d/render_graph/render_graph_node.hpp
src/render_graph/builder/graph_builder_pipeline.cpp
src/render_graph/builder/graph_build_pipeline.cpp
src/render_graph/executor/executor.cpp
src/render_graph/executor/internal.cpp
src/scene/layer_builder.cpp
src/scene/model/layer.cpp
apps/chronon3d_cli/commands/video/video_export_pipe.cpp
apps/chronon3d_cli/commands/video/video_export_chunked.cpp
```

**Comando:**

```bash
git log --format= --name-only --all \
  | sed '/^$/d' \
  | sort \
  | uniq -c \
  | sort -nr \
  | sed -n '1,40p'
```

---

## 4. Checklist Per Nuove Modifiche

**Prima di iniziare:**

```text
[ ] La modifica puo' stare in content/, effects/, nodes/, assets/, video/ o CLI?
[ ] Esiste gia' un registry o command adatto?
[ ] Serve davvero toccare scene/layer/graph/executor?
[ ] Il comportamento e' locale o cambia un contratto globale?
```

**Durante:**

```text
[ ] Aggiungere file nuovi quando possibile.
[ ] Evitare file protetti.
[ ] Non mescolare refactor core e feature.
[ ] Aggiungere test nel dominio corretto.
```

**Prima di chiudere:**

```text
[ ] Build mirata passa.
[ ] Test mirati passano.
[ ] File protetti toccati? Motivazione scritta.
[ ] Nessuna modifica non correlata.
```

---

## 5. Ordine Consigliato

Priorita' residue:

```text
1. Estrarre RenderGraphContext da render_graph_node.hpp
2. Completare split executor e ridurre internal.cpp/internal.hpp
3. Rifinire export video CLI: exporter boundary, progress, failure reporting
4. Hardening ExtensionLoader: failure modes, ABI contract
5. Test contrattuali core per impedire regressioni
```

La metrica di successo:

```text
Una nuova feature deve poter essere implementata aggiungendo file locali,
senza modificare render_graph_node.hpp, graph_builder_pipeline.cpp,
layer_builder.cpp o executor internals.
```
