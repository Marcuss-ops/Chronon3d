# Core Ownership — Chronon3d

Questo documento definisce le zone architetturali, i contratti fondamentali e le regole operative per prevenire la crescita incontrollata del motore.

Fare riferimento a `docs/ARCHITECTURE_EVOLUTION_PLAN.md` per il contesto architetturale completo.

---

## 1. Zone Architetturali

Il motore è suddiviso in quattro zone operative, più una zona sperimentale isolata.

### A. Core Zone — Contratti e invarianti fondamentali

La Core Zone contiene esclusivamente contratti, interfacce pubbliche e invarianti architetturali.
**Non** contiene implementazioni di feature, preset o contenuti specifici.

**Directory e contratti fondamentali:**

| Contratto | Header / Directory |
|---|---|
| **Scene** | `include/chronon3d/scene/model/core/scene.hpp` |
| **Layer** | `include/chronon3d/scene/model/layer/layer.hpp` |
| **Camera base** | `include/chronon3d/scene/model/camera/camera.hpp` |
| **CameraProjection** | `include/chronon3d/scene/camera/camera_projection.hpp` |
| **CameraRig** | `include/chronon3d/scene/model/camera/camera_rig.hpp` |
| **TransformResolver** | `include/chronon3d/scene/model/core/transform_resolver.hpp` |
| **RenderGraph** | `include/chronon3d/render_graph/render_graph.hpp` |
| **RenderGraphNode** | `include/chronon3d/render_graph/nodes/render_graph_node.hpp` |
| **GraphExecutor public contract** | `include/chronon3d/render_graph/executor/graph_executor.hpp` |
| **FrameContext** | `include/chronon3d/render_graph/render_graph_context.hpp` |
| **CachePolicy** | `include/chronon3d/render_graph/core/cache_policy.hpp` |
| **Framebuffer** | `include/chronon3d/core/memory/framebuffer.hpp` |
| **Composition** | `include/chronon3d/api/composition.hpp` → `include/chronon3d/timeline/composition.hpp` |
| **Registri canonici** | `include/chronon3d/effects/effect_catalog.hpp`, `include/chronon3d/assets/asset_registry.hpp`, `src/registry/shape_registry.cpp`, `src/registry/sampler_registry.cpp`, `src/registry/source_registry.cpp` |

**Regola:** Modificare un contratto Core solo con motivazione esplicita, test dedicati e review. Ogni modifica deve preservare la retrocompatibilità o documentare il breaking change.

**Non sono automaticamente Core:**
- camera presets (`camera_motion_presets.hpp`, `camera_rig_presets.hpp`, `camera_shot_profile.hpp`)
- camera shake (`camera_shake.hpp`)
- DoF (`dof.hpp`)
- debug overlay (`camera_debug_overlay.hpp`)
- validator (`scene_validator.hpp`, `camera_shot_validator.hpp`)
- effect specifici (`effect_stack.hpp`, `layer_effect.hpp`, `card3d_material.hpp`, `material_2_5d.hpp`)
- camera_2_5d e animated_camera_2_5d
- render node specifici (tutti i nodi in `include/chronon3d/render_graph/nodes/*` eccetto `render_graph_node.hpp`)
- content modules (`content/**`)
- builder pass interni (`src/render_graph/builder/passes/*`)
- executor interni (file in `src/render_graph/executor/` eccetto il public contract)

**⚠️ Gap policy/codice:** Alcuni contratti Core attualmente includono file che la policy dichiara non-core:

| Contratto Core | Dipende da | Tipo dipendenza |
|---|---|---|
| `scene.hpp` | `camera_2_5d.hpp` | `Camera2_5DRuntime` come membro di `Scene` |
| `layer.hpp` | `effect_stack.hpp`, `material_2_5d.hpp`, `card3d_material.hpp` | `EffectStack`, `Material2_5D`, `Card3DMaterial` come membri di `Layer` |
| `camera_rig.hpp` | `camera_2_5d.hpp`, `animated_camera_2_5d.hpp` | Metodo `evaluate()` restituisce `Camera2_5D` |
| `camera_projection.hpp` | `camera_2_5d.hpp` | `project_world_to_screen()` prende `Camera2_5D` come parametro |

Queste dipendenze sono **da risolvere** con refactor futuri (es. estrarre un'interfaccia `CameraProjectionSource` nel core, spostare `EffectStack` in Integration Zone tramite type-erasure o forward declaration). La policy è attiva: nessuna **nuova** dipendenza di questo tipo può essere aggiunta.

### B. Feature Zone — Lavoro di default

Effetti, render node, preset, exporter, media feature, camera preset, contenuti.

```
content/*
examples/*
apps/chronon3d_cli/commands/*
apps/chronon3d_cli/utils/*
include/chronon3d/render_graph/nodes/*        (eccetto render_graph_node.hpp)
src/render_graph/nodes/*
include/chronon3d/effects/*                    (eccetto effect_catalog.hpp)
src/effects/*                                  (eccetto effect_catalog.cpp)
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
tests/content/*
include/chronon3d/scene/camera/camera_motion_presets.hpp
include/chronon3d/scene/camera/camera_rig_presets.hpp
include/chronon3d/scene/camera/camera_shake.hpp
include/chronon3d/scene/camera/dof.hpp
include/chronon3d/scene/camera/camera_debug_overlay.hpp
include/chronon3d/scene/camera/camera_framing.hpp
include/chronon3d/scene/camera/camera_path_sampler.hpp
include/chronon3d/scene/camera/camera_2_5d.hpp
include/chronon3d/scene/camera/animated_camera_2_5d.hpp
include/chronon3d/scene/effects/*
include/chronon3d/scene/card3d_material.hpp
include/chronon3d/scene/card3d_params.hpp
include/chronon3d/scene/material_2_5d.hpp
```

### C. Integration Zone — Extension point e composizione tra moduli

Registry, resolver, sampler, extension point e composizione tra moduli.

```
include/chronon3d/extension/*
src/extension/*
include/chronon3d/render_graph/registry/*
src/render_graph/registry/*
include/chronon3d/scene/registry/*
src/scene/registry/*
include/chronon3d/scene/builders/commands/*
src/scene/builders/commands/*
include/chronon3d/scene/builders/layer_command.hpp
include/chronon3d/scene/builders/layer_command_registry.hpp
include/chronon3d/scene/validation/*
src/scene/validation/*
include/chronon3d/scene/builders/scene_builder.hpp
include/chronon3d/scene/builders/layer_builder.hpp
src/scene/builders/*
src/scene/camera/camera_rig_builder.hpp
content/register_content_modules.*
content/register_*_content.cpp
```

### D. Diagnostics Zone — Telemetria, debug e profiling

Telemetry, debug overlay, validator, benchmark, profiling, visual test, calibration e dump.

```
src/runtime/telemetry/*
include/chronon3d/runtime/telemetry/*
src/core/profiling.cpp
src/core/benchmark_report.cpp
src/render_graph/graph_profiler.cpp
src/render_graph/executor/telemetry_emitter.cpp
src/render_graph/executor/telemetry_emitter.hpp
src/cache/cache_diagnostics.cpp
src/cache/cache_diagnostics_format.cpp
src/cache/lru_log.cpp
src/render_graph/pipeline/debug.cpp
tests/visual/*
tests/renderer_tests.cmake
tests/visual_tests.cmake
tests/gradient_visual_tests.cmake
tests/breathing_golden_tests.cmake
tests/deterministic_tests.cmake
tools/telemetry_dashboard/*
tools/perf/*
tools/compare_benchmarks.py
tools/compare_pngs.py
tools/visual_quality_suite.py
```

**Regola:** Diagnostics non deve essere linkato nel core per default. Deve essere compilabile e attivabile tramite feature flag (`CHRONON3D_ENABLE_DIAGNOSTICS`, `CHRONON3D_ENABLE_TELEMETRY`).

### E. Experimental Zone — V3 tile-first

```
experimental/**                   (directory dedicata, da creare)
oppure feature branch V3 isolato
```

**Regola assoluta:** Nessun componente sperimentale può diventare dipendenza del core stabile. Il codice sperimentale vive nel proprio perimetro e viene promosso solo dopo:
1. Test di equivalenza completi
2. Review architetturale
3. Approvazione esplicita della migrazione
4. Rimozione del percorso legacy sostituito

---

## 2. Regole Anti-Crescita

### 2.1 Regola Anti-Duplicazione

Ogni nuova feature deve entrare attraverso un registry, resolver o sampler comune.
Non può replicare la stessa logica in più moduli.

**Esempio corretto:**
```
Nuovo effetto "Vignette" → registrato in effect_catalog
                         → usa sampler_registry esistente
                         → nessuna duplicazione di blend/transform
```

**Esempio da evitare:**
```
Modulo A: implementa blend mode "overlay" internamente
Modulo B: reimplementa blend mode "overlay" con piccole varianti
Modulo C: terza implementazione per esportazione video
```

### 2.2 Regola V3 — Dichiarazione obbligatoria

Ogni nuovo componente V3 deve dichiarare esplicitamente:

1. **Componente V2 sostituito** — file/percorso esatto
2. **Adapter temporaneo necessario** — wrapper che permette la coesistenza
3. **Criterio di rimozione** — condizione oggettiva per eliminare il legacy
4. **Test di equivalenza** — test che garantisce output identico V2 ↔ V3
5. **Data o milestone di eliminazione** — scadenza per rimuovere il percorso legacy

**Nessuna duplicazione V2/V3 permanente.** Ogni componente V3 deve avere un piano di eliminazione del corrispondente V2.

**Template:**
```markdown
### Componente V3: <nome>
- **Sostituisce:** <percorso V2>
- **Adapter:** <percorso adapter temporaneo>
- **Criterio rimozione:** <es. "quando tutti i nodi usano il nuovo percorso">
- **Test equivalenza:** <percorso test>
- **Deadline eliminazione:** <milestone o data>
```

### 2.3 Regola Content

Content e Diagnostics:
- Non devono essere linkati nel core per default
- Non devono introdurre dipendenze nel core
- Non devono modificare executor o scene model per una singola composizione
- Devono usare extension point esistenti (ExtensionModule, GraphNodeRegistry) per registrarsi

### 2.4 Regola Content Module Registration

La registrazione dei content module (`content/register_content_modules.cpp`) reference solo factory della propria libreria. Non aggiungere riferimenti cross-module. Ogni modulo espone la propria factory e il registry centrale le orchestra.

### 2.5 Budget di Crescita — Per ogni nuovo modulo

Ogni nuovo modulo deve soddisfare TUTTI i requisiti:

- [ ] **Una responsabilità** — singolo scopo chiaro e documentato
- [ ] **Un target CMake** — compilazione isolata e testabile
- [ ] **Un registry/extension point esistente** — nessun nuovo meccanismo di plug-in ad-hoc
- [ ] **Test mirati** — copertura della responsabilità primaria
- [ ] **Nessuna dipendenza pesante senza feature flag** — dipendenze opzionali dietro `#ifdef` o CMake option
- [ ] **Nessun nuovo singleton globale** — stato confinato al modulo o passato esplicitamente
- [ ] **Nessuna nuova cache** se può usare la primitiva `LruCache` comune (`src/cache/`)

---

## 3. Regole Per Agenti

### 3.1 Prima Scelta: Feature Zone

Se il task riguarda preset, contenuto, asset, exporter, effetto, render node specifico, test o CLI, lavorare in **Feature Zone**.

### 3.2 Core Solo Con Permesso

Prima di modificare un contratto Core, scrivere:

```
Voglio modificare: <contratto>
Motivo: <perche' serve>
Alternativa locale provata: <perche' non basta>
Rischi: <cache/layer/order/dirty rect/frame dependency/etc>
Test da eseguire: <lista>
```

### 3.3 Una Feature, Un Modulo

Ogni feature nuova deve avere una casa chiara in Feature Zone.

**Esempio corretto:**
```
content/Minimalist/minimalist_module.cpp
content/Minimalist/minimalist_presets.cpp
tests/content/test_minimalist_presets.cpp
```

**Esempio da evitare:**
```
content/Minimalist/*
src/render_graph/builder/graph_builder_pipeline.cpp   ← non toccare
include/chronon3d/scene/builders/layer_builder.hpp    ← non toccare
src/render_graph/executor/internal.cpp                ← non toccare
```

### 3.4 Mai Mischiare Refactor Core E Feature

Separare sempre:
1. Refactor senza cambio comportamento
2. Test di equivalenza
3. Feature nuova
4. Test della feature

### 3.5 Ownership Per Tipo Di Agente

| Agente | Può toccare | Non deve toccare |
|--------|------------|-----------------|
| **Core Graph** | render_graph core, executor, compiler, cache contracts | scene, content, effects |
| **Scene/Layer** | scene model, layer model, builders, specscene | render_graph executor, content |
| **Feature** | content, presets, effects, assets, CLI, render nodes specifici | core graph, scene core |
| **Performance** | backend software, framebuffer pool, SIMD, video path | scene, content, graph contracts |
| **Test** | test ovunque (solo aggiunta, no modifica core behavior) | core behavior |
| **Diagnostics** | telemetry, profiling, benchmark, debug overlay, validator | core graph, scene model |

Un agente non deve invadere l'area di un altro senza dichiararlo.

---

## 4. Test Minimi Per Toccare Il Core

### Build obbligatoria

```bash
# Workflow veloce (sub‑30 s incrementale): vedi docs/FAST_BUILD.md
./build-fast.sh
```

Per build di release / verifica deterministica (preset `linux-release`, niente sloppiness/ccache):

```bash
cmake --preset linux-release
cmake --build build/chronon/linux-release -j$(nproc)
```

### Test obbligatori (quando disponibili)

```bash
ctest --test-dir build/chronon/linux-debug --output-on-failure -R "render_graph|scene|specscene|cache"
```

### Contratti Da Coprire

| Contratto | Test |
|-----------|------|
| Stesso scene input produce stesso graph hash | `test_graph_snapshot.cpp` |
| Layer order stabile | `test_layer_order_contract.cpp` |
| Cache key include input hash, frame policy, tile info | `test_cache_key_contract.cpp` |
| Dirty rect non salta pixel necessari | `test_dirty_rect_contract.cpp` |
| Framebuffer arena non leak-a wrapper | `test_framebuffer_lifetime.cpp` |
| Executor non rilascia input prima dell'ultimo consumer | `test_framebuffer_lifetime.cpp` |
| SpecScene compile non cambia semantica dei layer | `test_specscene_compile_contract.cpp` |

---

## 5. Checklist PR Per Contratti Core

- [ ] Motivazione scritta nel commit message
- [ ] Alternativa locale valutata e documentata
- [ ] Build mirata passa (`./build-fast.sh` oppure `cmake --build --preset linux-fast-dev`)
- [ ] Test mirati passano (`ctest -R "render_graph|scene|specscene|cache"`)
- [ ] Nessuna modifica non correlata nello stesso commit
- [ ] Refactor e feature separati in commit distinti

---

## 6. File Storicamente Caldi

Monitorare questi file — se continuano a salire in `git log`, estrarre un extension point:

```
include/chronon3d/render_graph/nodes/render_graph_node.hpp
include/chronon3d/render_graph/nodes/basic_nodes.hpp
include/chronon3d/render_graph/builder/graph_builder.hpp
include/chronon3d/render_graph/core/render_graph_hashing.hpp
src/render_graph/builder/graph_builder_pipeline.cpp
src/render_graph/builder/graph_builder_layer_pipeline.cpp
src/render_graph/executor/executor.cpp
src/render_graph/executor/internal.cpp
src/scene/layer_builder.cpp
include/chronon3d/scene/builders/layer_builder.hpp
include/chronon3d/scene/builders/scene_builder.hpp
apps/chronon3d_cli/commands/video/video_export_pipe.cpp
src/cache/framebuffer_pool.cpp
```

Comando per verificare:

```bash
git log --format= --name-only --all \
  | sed '/^$/d' \
  | sort \
  | uniq -c \
  | sort -nr \
  | sed -n '1,40p'
```
