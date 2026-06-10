# ==============================================================================
# src/render_graph/CMakeLists.txt — chronon3d_graph (STATIC)
#
# Il cuore del pipeline di rendering: costruisce, analizza, ottimizza ed
# esegue il render graph.  Sottocomponenti:
#   builder/      ← costruzione grafo da layer/scena
#   executor/     ← esecuzione tile-based del grafo
#   pipeline/     ← integrazione scene + composizione
#   nodes/        ← implementazione nodi (source, transform, transition)
#   preflight/    ← analisi statica pre-render
#   optimizer/    ← folding/elisione passi ridondanti
#   compiler/     ← compilazione frame graph
#   registry/     ← registry nodi grafo
#
# Dipendenze (PUBLIC):
#   chronon3d               — tipi base
#   chronon3d_cache         — frame/node cache per memoizzazione
#   chronon3d_media_interface — interfaccia astratta provider video (Item 19)
#   fmt                     — formattazione
#   hwy                     — Highway SIMD (transform kernels)
#   tomlplusplus            — parsing TOML config
#   TBB                     — parallel_for, task graph
#
# Fornisce a: chronon3d_runtime, chronon3d_backend_software, chronon3d_scene,
#             chronon3d_extension
# ==============================================================================
add_library(chronon3d_graph STATIC
    builder/graph_builder_internal.cpp
    builder/graph_builder_pipeline.cpp
    builder/graph_builder_static_analysis.cpp
    builder/graph_builder_bbox.cpp
    builder/graph_builder_matte.cpp
    builder/graph_builder_layer_pipeline.cpp
    builder/graph_build_pipeline.cpp
    builder/passes/graph_builder_layer_passes.cpp
    builder/passes/graph_builder_lighting_passes.cpp
    builder/passes/graph_builder_source_pass.cpp
    builder/passes/graph_builder_resolve_pass.cpp
    builder/passes/graph_builder_root_sources_pass.cpp
    builder/passes/graph_builder_layer_pipeline_pass.cpp
    builder/passes/graph_builder_output_pass.cpp
    builder/passes/graph_builder_validation_pass.cpp
    builder/graph_build_registry.cpp
    registry/graph_node_registry.cpp
    executor/executor.cpp
    executor/input_resolver.cpp
    executor/cache_evaluator.cpp
    executor/tile_pruning.cpp
    executor/telemetry_emitter.cpp
    executor/node_runner.cpp
    executor/executor_levels.cpp
    executor/framebuffer_lifetime.cpp
    executor/executor_plan.cpp
    graph_profiler.cpp
    render_graph.cpp
    framebuffer_acquire.cpp
    pipeline/scene.cpp
    pipeline/scene_dirty.cpp
    pipeline/scene_tile_execution.cpp
    pipeline/tile_execution_policy.cpp
    pipeline/debug.cpp
    pipeline/composition.cpp
    preflight/preflight.cpp
    preflight/analysis.cpp
    preflight/analysis_graph.cpp
    preflight/analysis_diagnostics.cpp
    optimizer/graph_optimizer.cpp
    compiler/frame_graph_compiler.cpp
    nodes/source_node.cpp
    nodes/multi_source_node.cpp
    nodes/transform_node.cpp
    nodes/transform_kernels.cpp
    nodes/transition_node.cpp
    nodes/transition/transition_node_math.cpp
)
set_target_properties(chronon3d_graph PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_graph
    PUBLIC chronon3d chronon3d_cache chronon3d_media_interface
           fmt::fmt hwy::hwy tomlplusplus::tomlplusplus TBB::tbb
)
target_precompile_headers(chronon3d_graph PRIVATE
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <fmt/core.h>
)
