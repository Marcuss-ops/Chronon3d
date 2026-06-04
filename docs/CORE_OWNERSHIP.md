# Core Ownership — Chronon3d

Questo documento elenca i file protetti del motore, le regole per toccarli e i test minimi richiesti.

Fare riferimento a `docs/ARCHITECTURE_EVOLUTION_PLAN.md` per il contesto architetturale completo.

---

## 1. File Protetti

Modificare questi file solo con motivazione esplicita, test dedicata e review.

### 1.1 Render Graph Core

```
include/chronon3d/render_graph/render_graph.hpp
include/chronon3d/render_graph/render_graph_node.hpp
include/chronon3d/render_graph/render_graph_hashing.hpp
include/chronon3d/render_graph/graph_executor.hpp
include/chronon3d/render_graph/graph_builder.hpp
include/chronon3d/render_graph/render_pipeline.hpp
include/chronon3d/render_graph/cache_policy.hpp
include/chronon3d/render_graph/scene_hasher.hpp
include/chronon3d/render_graph/render_graph_context.hpp    # Fase 1 — ancora in assestamento
include/chronon3d/render_graph/framebuffer_acquire.hpp     # Fase 1 — ancora in assestamento
include/chronon3d/render_graph/compiler/frame_graph_compiler.hpp
include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp
include/chronon3d/render_graph/compiler/frame_graph_compile_options.hpp
include/chronon3d/render_graph/optimizer/graph_optimizer.hpp

src/render_graph/render_graph.cpp
src/render_graph/render_graph_context.cpp
src/render_graph/framebuffer_acquire.cpp
src/render_graph/graph_profiler.cpp
src/render_graph/compiler/frame_graph_compiler.cpp
src/render_graph/optimizer/graph_optimizer.cpp
```

### 1.2 Builder — GraphBuildPass Pipeline

_Ristrutturato in Fasi 3-4. La pipeline orchestra pass indipendenti._

```
include/chronon3d/render_graph/graph_builder.hpp
include/chronon3d/render_graph/builder/graph_build_pass.hpp
include/chronon3d/render_graph/builder/graph_build_context.hpp
include/chronon3d/render_graph/builder/graph_build_pipeline.hpp
include/chronon3d/render_graph/builder/graph_build_registry.hpp
src/render_graph/builder/graph_build_pipeline.cpp
src/render_graph/builder/graph_build_registry.cpp
src/render_graph/builder/graph_builder_pipeline.cpp          # helper functions
src/render_graph/builder/graph_builder_layer_pipeline.cpp    # helper functions
src/render_graph/builder/passes/graph_builder_passes.hpp
src/render_graph/builder/passes/graph_builder_resolve_pass.cpp
src/render_graph/builder/passes/graph_builder_source_pass.cpp
src/render_graph/builder/passes/graph_builder_source_pass.hpp
src/render_graph/builder/passes/graph_builder_root_sources_pass.cpp
src/render_graph/builder/passes/graph_builder_layer_passes.cpp
src/render_graph/builder/passes/graph_builder_layer_passes.hpp
src/render_graph/builder/passes/graph_builder_layer_pipeline_pass.cpp
src/render_graph/builder/passes/graph_builder_lighting_passes.cpp
src/render_graph/builder/passes/graph_builder_lighting_passes.hpp
src/render_graph/builder/passes/graph_builder_output_pass.cpp
src/render_graph/builder/passes/graph_builder_validation_pass.cpp      # Fase 7
src/render_graph/builder/passes/graph_builder_validation_pass.hpp      # Fase 7
```

### 1.3 Executor

```
src/render_graph/executor/executor.cpp
src/render_graph/executor/internal.cpp
src/render_graph/executor/internal.hpp
src/render_graph/executor/execution_state.hpp
src/render_graph/executor/cache_evaluator.cpp
src/render_graph/executor/input_resolver.cpp
src/render_graph/executor/node_runner.cpp
src/render_graph/executor/telemetry_emitter.cpp
src/render_graph/executor/tile_pruning.cpp
```

### 1.4 Render Graph Pipeline

_Ristrutturato in Fasi 3-4. `scene.cpp` ora usa `GraphBuildPipeline::build_with_resolved()`._

```
src/render_graph/pipeline/scene.cpp
src/render_graph/pipeline/composition.cpp
src/render_graph/pipeline/scene_dirty.cpp
src/render_graph/pipeline/debug.cpp
src/render_graph/pipeline/scene_internal.hpp
src/render_graph/pipeline/helpers.hpp
```

### 1.5 Scene & Layer

```
include/chronon3d/scene/scene.hpp
include/chronon3d/scene/layer/layer.hpp
include/chronon3d/scene/layer/layer_hierarchy.hpp
include/chronon3d/scene/layer/resolved_types.hpp
include/chronon3d/scene/layer/render_node.hpp
include/chronon3d/scene/layer/depth_role.hpp
include/chronon3d/scene/layer/transition.hpp
include/chronon3d/scene/layer/track_matte.hpp
include/chronon3d/scene/camera/camera.hpp
include/chronon3d/scene/camera/camera_projection.hpp
include/chronon3d/scene/camera/camera_rig.hpp
include/chronon3d/scene/camera/camera_motion_presets.hpp
include/chronon3d/scene/camera/camera_framing.hpp
include/chronon3d/scene/camera/camera_path_sampler.hpp
include/chronon3d/scene/camera/camera_rig_builder.hpp
include/chronon3d/scene/camera/camera_rig_presets.hpp
include/chronon3d/scene/camera/camera_shot_profile.hpp
include/chronon3d/scene/camera/camera_shot_validator.hpp
include/chronon3d/scene/camera/camera_2_5d.hpp
include/chronon3d/scene/camera/animated_camera_2_5d.hpp
include/chronon3d/scene/camera/camera_debug_overlay.hpp
include/chronon3d/scene/camera/camera_shake.hpp
include/chronon3d/scene/camera/dof.hpp
include/chronon3d/scene/builders/scene_builder.hpp
include/chronon3d/scene/builders/layer_builder.hpp
include/chronon3d/scene/mask/mask.hpp
include/chronon3d/scene/mask/mask_utils.hpp
include/chronon3d/scene/transform/transform_resolver.hpp
include/chronon3d/scene/transform/transform_3d.hpp
include/chronon3d/scene/transform/path.hpp
include/chronon3d/scene/shape.hpp
include/chronon3d/scene/fill.hpp
include/chronon3d/scene/material_2_5d.hpp
include/chronon3d/scene/card3d_material.hpp
include/chronon3d/scene/card3d_params.hpp
include/chronon3d/scene/effects/effect_stack.hpp
include/chronon3d/scene/effects/layer_effect.hpp
include/chronon3d/scene/render_node_factory.hpp
include/chronon3d/scene/render_runtime.hpp

src/scene/layer_builder.cpp
src/scene/scene_builder.cpp
src/scene/layer.cpp
src/scene/layer_builder_effects.cpp
src/scene/render_node_factory.cpp
src/scene/camera_api.cpp
src/scene/dark_grid_background.cpp
src/scene/mask/mask_utils.cpp
src/scene/camera/camera_rig.cpp
src/scene/camera/camera_projection.cpp
src/scene/camera/camera_shot_validator.cpp
src/scene/camera/camera_motion_presets.cpp
src/scene/camera/camera_framing.cpp
src/scene/camera/camera_path_sampler.cpp
src/scene/camera/camera_rig_presets.cpp
src/scene/camera/camera_debug_overlay.cpp
src/scene/transform/transform_resolver.cpp
```

### 1.5a Scene Builders — LayerCommand System

_Creato in Fase 6. Modificare solo con motivazione esplicita._

```
include/chronon3d/scene/builders/layer_command.hpp
include/chronon3d/scene/builders/layer_command_registry.hpp
include/chronon3d/scene/builders/commands/motion_preset_commands.hpp
src/scene/builders/layer_command_registry.cpp
src/scene/builders/commands/motion_preset_commands.cpp
src/scene/builders/commands/motion_preset_methods.cpp
```

### 1.5b Scene Validation

_Creato in Fase 7. Modificare solo con motivazione esplicita._

```
include/chronon3d/scene/validation/scene_validator.hpp
include/chronon3d/scene/validation/scene_validation_registry.hpp
src/scene/validation/scene_validator.cpp
src/scene/validation/scene_validation_registry.cpp
```

### 1.6 SpecScene

```
src/specscene/specscene.cpp
src/specscene/specscene_parsers.cpp
src/specscene/specscene_parsers.hpp
```

### 1.7 Registries (Core)

```
include/chronon3d/effects/effect_registry.hpp
src/effects/effect_registry.cpp
include/chronon3d/assets/asset_registry.hpp
src/registry/shape_registry.cpp
src/registry/sampler_registry.cpp
src/registry/source_registry.cpp
```

### 1.8 Extension System

_Creato in Fase 5. Permette ai moduli di registrare features senza toccare il core._

```
include/chronon3d/extension/extension_module.hpp
include/chronon3d/extension/extension_registry.hpp
src/extension/extension_registry.cpp
include/chronon3d/render_graph/registry/graph_node_registry.hpp
src/render_graph/registry/graph_node_registry.cpp
```

### 1.8a Content Module Registration

_Creato in Fase 8. Wiring per i content ExtensionModules (Minimalist, Text, 2D5)._ Modificare solo con motivazione esplicita — questi file controllano l'ordine di inizializzazione a runtime.

```
content/register_content_modules.hpp
content/register_content_modules.cpp
content/register_minimalist_content.cpp
content/register_text_content.cpp
content/register_2d5_content.cpp
```

**Regola:** Ogni file di registrazione per-modulo reference solo la factory della propria libreria content. Non aggiungere riferimenti cross-module qui.

```
register_minimalist_content()  →  content_anims
register_text_content()        →  content_text
register_two_point_five_d_content()  →  content_2d5
```

### 1.9 Cache & Resources

```
src/cache/framebuffer_pool.cpp
src/cache/node_cache.cpp
src/cache/frame_cache.cpp
src/cache/video_frame_cache.cpp
src/cache/persistent_bake_cache.cpp
src/cache/disk_node_cache.cpp
```

---

## 2. Zone Di Lavoro

### Core Protetto (sopra)
Modificare solo con motivazione esplicita. Nessuna feature diretta.

### Feature Zone (lavorare qui di default)
```
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
tests/content/*
```

### Integration Zone (extension point)
```
include/chronon3d/extension/*
src/extension/*
include/chronon3d/render_graph/builder/passes/*
src/render_graph/builder/passes/*
include/chronon3d/render_graph/registry/*
src/render_graph/registry/*
include/chronon3d/scene/registry/*
src/scene/registry/*
include/chronon3d/scene/builders/commands/*
src/scene/builders/commands/*
include/chronon3d/scene/validation/*
src/scene/validation/*
content/register_content_modules.*
content/register_*_content.cpp
```

---

## 3. Regole Per Agenti

### 3.1 Prima Scelta: File Locali

Se il task riguarda preset, contenuto, asset, exporter, effetto, render node specifico, test o CLI, lavorare in **Feature Zone**.

### 3.2 Core Solo Con Permesso

Prima di modificare un file protetto, scrivere:

```
Voglio modificare: <file>
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

Un agente non deve invadere l'area di un altro senza dichiararlo.

---

## 4. Test Minimi Per Toccare Il Core

### Build obbligatoria

```bash
cmake --build --preset linux-fast --target chronon3d_shared -j 4
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

## 5. Checklist PR Per File Protetti

- [ ] Motivazione scritta nel commit message
- [ ] Alternativa locale valutata e documentata
- [ ] Build mirata passa (`cmake --build --preset linux-fast`)
- [ ] Test mirati passano (`ctest -R "render_graph|scene|specscene|cache"`)
- [ ] Nessuna modifica non correlata nello stesso commit
- [ ] Refactor e feature separati in commit distinti

---

## 6. File Storicamente Caldi

Monitorare questi file — se continuano a salire in `git log`, estrarre un extension point:

```
include/chronon3d/render_graph/render_graph_node.hpp
include/chronon3d/render_graph/nodes/basic_nodes.hpp
include/chronon3d/render_graph/graph_builder.hpp
include/chronon3d/render_graph/render_graph_hashing.hpp
src/render_graph/builder/graph_builder_pipeline.cpp
src/render_graph/builder/graph_builder_layer_pipeline.cpp
src/render_graph/graph_executor.cpp
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
