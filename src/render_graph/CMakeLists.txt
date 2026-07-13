# ==============================================================================
# src/render_graph/CMakeLists.txt — chronon3d_graph split into sub-targets
#

# ==============================================================================

# ── Core: base types, registry, utilities ───────────────────────────────────
# TICKET-011 cmake-boundary (OBJECT lift) — chronon3d_graph_* sub-libs are
# now OBJECT libraries; their compiled objects are aggregated into
# chronon3d_sdk_impl.a (see src/CMakeLists.txt).  Exposed through the
# `chronon3d_graph` INTERFACE umbrella which still re-discloses each
# sub-target name for CMake link-resolution semantics.
add_library(chronon3d_graph_core OBJECT
    render_graph.cpp
    framebuffer_acquire.cpp
    graph_profiler.cpp
    registry/graph_node_catalog.cpp
    layer_resolver.cpp
    core/scene_hasher.cpp
)
set_target_properties(chronon3d_graph_core PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_graph_core
    # PUBLIC: types from these appear in public headers (render_graph.hpp, graph_executor.hpp)
    PUBLIC
        chronon3d
        chronon3d_cache
        chronon3d_media_interface
        TBB::tbb       # tbb::task_arena in graph_executor.hpp
    # PRIVATE: only used in .cpp files, not exposed in public API
    PRIVATE
        fmt::fmt        hwy::hwy
    )
target_precompile_headers(chronon3d_graph_core PRIVATE
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <fmt/core.h>
)

# ── Nodes: graph node implementations ───────────────────────────────────────
add_library(chronon3d_graph_nodes OBJECT
    nodes/source_node.cpp
    nodes/multi_source_node.cpp
    nodes/transform_node.cpp
    nodes/transform_kernels.cpp
    nodes/transition_node.cpp
    nodes/effect_stack_node.cpp
    nodes/clear_node.cpp
    nodes/composite_node.cpp
    nodes/track_matte_node.cpp
    nodes/per_pixel_dof_node.cpp
    nodes/transition/transition_node_math.cpp
    nodes/TextRunNode.cpp   # PR 3 (TextAnimator V2 batched text node)
    # M1.5#1 — extract execute() into single-responsibility helpers
    # under nodes/text_run/.  Internal-only; not exposed via include/.
    nodes/text_run/text_run_execution.cpp
    nodes/text_run/text_run_transform.cpp
    nodes/text_run/text_run_diagnostics.cpp
)
set_target_properties(chronon3d_graph_nodes PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_graph_nodes
    PUBLIC
        chronon3d_graph_core
    PRIVATE
        fmt::fmt
        spdlog::spdlog_header_only
)
if(TARGET chronon3d_text_core)
    target_link_libraries(chronon3d_graph_nodes PUBLIC chronon3d_text_core)
endif()
target_precompile_headers(chronon3d_graph_nodes PRIVATE
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <fmt/core.h>
)

# ── Builder: graph construction ─────────────────────────────────────────────
add_library(chronon3d_graph_builder OBJECT
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
    builder/builder_layer_id.cpp
    builder/precomp_builder_service.cpp
)
set_target_properties(chronon3d_graph_builder PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_graph_builder PUBLIC
    chronon3d_graph_core
    chronon3d_graph_nodes
)
target_precompile_headers(chronon3d_graph_builder PRIVATE
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <fmt/core.h>
)

# ── Compiler: graph compilation & optimization ──────────────────────────────
add_library(chronon3d_graph_compiler OBJECT
    compiler/frame_graph_compiler.cpp
    compiler/frame_graph_builder.cpp    # FASE 16 - extracted private builders
    compiler/scene_binding_compiler.cpp
    optimizer/graph_optimizer.cpp
)
set_target_properties(chronon3d_graph_compiler PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_graph_compiler PUBLIC
    chronon3d_graph_core
    chronon3d_graph_nodes
)

# ── Executor: graph execution ───────────────────────────────────────────────
add_library(chronon3d_graph_executor OBJECT
    executor/executor.cpp
    executor/input_resolver.cpp
    executor/cache_evaluator.cpp
    executor/tile_pruning.cpp
    executor/node_runner.cpp
    executor/node_state_commit.cpp     # P1 step 3 — extract commit_node_state (5 per-node state-slot writes from cache_eval + bbox; byte-equivalent to inline pattern at Site 1 cache-hit fast-path + Site 3 main tail in node_runner.cpp; Site 2 tile-skip stays inline per Cat-3 anti-dup)
    executor/text_bbox_reconcile.cpp   # P0 #1 — extracted post-render alpha_bbox reconciliation helper (replaces duplicate TU-local scan in node_runner.cpp + closes static-bool race)
    executor/node_skip_policy.cpp      # P1 step 1/3 — unified commit_transparent_skip (replaces 2 duplicate 64×10 transparent skip blocks in node_runner.cpp; SkipReason enum drives the Clear-name branch)
    executor/level_timings.cpp          # P1 refactor — per-level metrics accumulator + roll_up() helper (replaces 7-vector declaration + 60-line if-block in executor_levels.cpp; forward-declared RenderCounters, src-local header)
    executor/executor_levels.cpp
    executor/framebuffer_lifetime.cpp
    # ── TICKET-009 close-out (post-RenderGraphContext OBJECT-lift) —
    # graph topological-plan cache (thread-safe, shared between
    # SoftwareRenderer and RenderRuntime).  Compiled directly into the
    # executor sub-target to avoid a graph_executor → runtime → graph
    # link chain.  Public surface still lives at
)
if(CHRONON3D_ENABLE_TELEMETRY)
    target_sources(chronon3d_graph_executor PRIVATE executor/telemetry_emitter.cpp)
endif()
set_target_properties(chronon3d_graph_executor PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_graph_executor PUBLIC
    chronon3d_graph_core
    chronon3d_graph_nodes
    chronon3d_graph_compiler
)
target_precompile_headers(chronon3d_graph_executor PRIVATE
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <fmt/core.h>
)

# TICKET-PREFLIGHT-PATH-EXISTENCE-MAP-EXISTS-BODY: dedicated STATIC
# library for preflight/path_existence_map.cpp (the O(1) path existence
# cache with TTL + shared_lock/unique_lock + mutable-mutex idiom).
# Promoted out of the chronon3d_graph_preflight DIAGNOSTICS-gated
# target_sources block so BOTH chronon3d_graph_preflight (which uses
# PathExistenceMap* in analysis_diagnostics.cpp:82) AND chronon3d_assets
# (which uses PathExistenceMap* in render_preflight_validate.cpp's
# cache->exists(...) call) can link to the .a archive without the
# OBJECT-library multiple-definition anti-pattern.  STATIC (not OBJECT)
# is the correct choice: the linker pulls in only the .o files actually
# referenced, so the .a archive can safely appear in the link line of
# multiple consumers without duplication.  Per AGENTS.md Cat-3
# minimal-surface + the project's STATIC library convention
# (chronon3d_cli_*, etc.).  Was previously buried inside the
# DIAGNOSTICS-gated block, which was the actual rot: the consumer
# render_preflight_validate.cpp doesn't gate on DIAGNOSTICS, so the
# build always failed with `undefined reference to
# PathExistenceMap::exists` in non-DIAGNOSTICS configurations.
add_library(chronon3d_path_cache STATIC
    preflight/path_existence_map.cpp
)
target_link_libraries(chronon3d_path_cache PUBLIC chronon3d)
set_target_properties(chronon3d_path_cache PROPERTIES UNITY_BUILD OFF)

# ── Preflight: validation & analysis before render ──────────────────────────
add_library(chronon3d_graph_preflight OBJECT
    preflight/preflight.cpp
    preflight/analysis.cpp
    preflight/analysis_graph.cpp
)
# TICKET-PREFLIGHT-PATH-EXISTENCE-MAP-EXISTS-BODY: only analysis_diagnostics.cpp
# is DIAGNOSTICS-gated here.  path_existence_map.cpp has been promoted to
# the dedicated chronon3d_path_cache STATIC library (above) so it is
# always available — render_preflight_validate.cpp in chronon3d_assets
# consumes it unconditionally, NOT just under CHRONON3D_ENABLE_DIAGNOSTICS.
if(CHRONON3D_ENABLE_DIAGNOSTICS)
    target_sources(chronon3d_graph_preflight PRIVATE preflight/analysis_diagnostics.cpp)
endif()
set_target_properties(chronon3d_graph_preflight PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_graph_preflight PUBLIC
    chronon3d_graph_core
    chronon3d_graph_builder
    chronon3d_graph_compiler
    chronon3d_path_cache  # TICKET-PREFLIGHT-PATH-EXISTENCE-MAP-EXISTS-BODY: analysis_diagnostics.cpp:82 consumes PathExistenceMap*
)

# ── Cache: scene program cache + compiled graph cache ───────────────────────
add_library(chronon3d_graph_cache OBJECT
    cache/scene_program_cache.cpp
    cache/scene_program_store.cpp
    cache/compiled_graph_cache.cpp
    cache/precomp_node_execute.cpp
)
set_target_properties(chronon3d_graph_cache PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_graph_cache PUBLIC
    chronon3d_graph_core
    chronon3d_graph_compiler
)

# ── Pipeline: scene orchestration, dirty rects, tile execution ───────────────
add_library(chronon3d_graph_pipeline OBJECT
    pipeline/scene.cpp
    pipeline/scene_context_setup.cpp    # Azione 19 - Phase 0 helper extracted
    pipeline/scene_reuse_coordinator.cpp # Azione 19 - Phases 1+2+3 coordinator extracted
    pipeline/scene_dirty.cpp
    pipeline/dirty/scroll_optimization.cpp
    pipeline/dirty/layer_bbox_collector.cpp
    pipeline/dirty/root_bbox_collector.cpp
    pipeline/scene_tile_execution.cpp
    pipeline/tile_execution_policy.cpp
    pipeline/camera_change_policy.cpp
    pipeline/dirty_safety_policy.cpp
    pipeline/dirty_telemetry_reporter.cpp
    pipeline/tile_execution_coordinator.cpp
    pipeline/frame_timing_recorder.cpp
    pipeline/scene_fingerprint.cpp
    pipeline/frame_reuse_policy.cpp
    pipeline/pool_preallocation.cpp
    pipeline/graph_cache_coordinator.cpp
    pipeline/frame_state_commit.cpp
    pipeline/scene_refresh.cpp
    pipeline/refresh/layer_item.cpp
    pipeline/refresh/source.cpp
    pipeline/refresh/multi_source.cpp
    pipeline/refresh/effect_stack.cpp
    pipeline/refresh/transform.cpp
    pipeline/debug.cpp
    pipeline/composition.cpp
    pipeline/register_pipeline_nodes.cpp
)
set_target_properties(chronon3d_graph_pipeline PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_graph_pipeline
    PUBLIC
        chronon3d_graph_core
        chronon3d_graph_builder
        chronon3d_graph_executor
        chronon3d_graph_compiler
        chronon3d_graph_preflight
        chronon3d_graph_cache
    PRIVATE
        chronon3d_extension
        # chronon3d_content removed — the host application (CLI, tests) is
        # responsible for linking chronon3d_content and calling
        # register_content_modules().  The graph pipeline calls
        # register_content_modules() via a forward declaration; the symbol
        # is resolved at final link time by the host executable.
)
if(CHRONON3D_BUILD_CONTENT)
    target_link_libraries(chronon3d_graph_pipeline PRIVATE chronon3d_content)   # <-- register_content_modules() impl
endif()
target_precompile_headers(chronon3d_graph_pipeline PRIVATE
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <fmt/core.h>
)

# ── Umbrella INTERFACE: all sub-targets bundled for downstream consumers ─────
add_library(chronon3d_graph INTERFACE)
target_link_libraries(chronon3d_graph INTERFACE
    chronon3d_graph_core
    chronon3d_graph_nodes
    chronon3d_graph_builder
    chronon3d_graph_executor
    chronon3d_graph_compiler
    chronon3d_graph_preflight
    chronon3d_graph_pipeline
    chronon3d_graph_cache
)
# TICKET-011 cmake-boundary (Branch C fix): OBJECT → INTERFACE propagation.
# CMake 3.12+ `target_link_libraries` does NOT transitively propagate .o
# files through INTERFACE targets.  We must explicitly bind each OBJECT
# library's compiled objects via `$<TARGET_OBJECTS>` so downstream
# consumers (CLI, tests) resolve all symbols at link time.
target_sources(chronon3d_graph INTERFACE
    $<TARGET_OBJECTS:chronon3d_graph_core>
    $<TARGET_OBJECTS:chronon3d_graph_nodes>
    $<TARGET_OBJECTS:chronon3d_graph_builder>
    $<TARGET_OBJECTS:chronon3d_graph_executor>
    $<TARGET_OBJECTS:chronon3d_graph_compiler>
    $<TARGET_OBJECTS:chronon3d_graph_preflight>
    $<TARGET_OBJECTS:chronon3d_graph_pipeline>
    $<TARGET_OBJECTS:chronon3d_graph_cache>
)


# P1-E Pass E Commit #4 — software internal headers (software_renderer.hpp,
# software_backend.hpp) FULL-included by:
#   - `basic_nodes_common.hpp` (in render_graph/include_private/) →
#     ALL .cpp files in `chronon3d_graph_nodes`
#   - `graph_builder_layer_pipeline_pass.cpp` → `chronon3d_graph_builder`
#   - `preflight.cpp` → `chronon3d_graph_preflight`
# These sub-targets need explicit PRIVATE access to
# `src/backends/software/include_private/` because
# `chronon3d_graph_core` does NOT PUBLIC-link `chronon3d_backend_software`
# (see graph_core's PUBLIC block); transitive INTERFACE propagation across
# the umbrella does not auto-resolve, so we bind explicitly.  Other
# render_graph sub-targets only fwd-decl SoftwareRenderer / consume
# PIMPL pointers (no full include), so they do not need this wiring.
foreach(_rg_sw_consumer
        chronon3d_graph_nodes
        chronon3d_graph_builder
        chronon3d_graph_preflight)
    target_include_directories(${_rg_sw_consumer} PRIVATE ${CMAKE_SOURCE_DIR}/src/backends/software/include_private)
endforeach()

# TICKET-AE-PARITY-FLOOR-DASHBOARD — internal-only header at
# src/render_graph/nodes/detail/projection_helpers.hpp is FULL-included by  # drift-allow: stale-ref
# source_node.cpp + multi_source_node.cpp via the public include path
# <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>.  Per
# AGENTS.md "non esporre headers interni come public API", we do NOT move
# the header to include/chronon3d/; instead, expose ONLY the
# src/render_graph/nodes/ subtree (the minimum needed) as PRIVATE to the
# chronon3d_graph_nodes target (the only target that contains the two
# .cpp files referencing the detail header).  Mirrors the existing
# `src/backends/software/include_private` pattern above (also narrow,
# one subdir) — code-reviewer-minimax-m3 round-1 flagged the broader
# `${CMAKE_SOURCE_DIR}/src` as over-exposed; narrowing to the
# `src/render_graph/nodes` subtree minimizes accidental coupling while
# keeping the same internal-header access pattern.
target_include_directories(chronon3d_graph_nodes PRIVATE ${CMAKE_SOURCE_DIR}/src/render_graph/nodes)
# Backward direction — runtime headers (Commit #3) are FULL-included by
# `software_renderer.hpp` (Compile-time dependency).  chronon3d_backend_software
# PUBLIC-links `chronon3d_runtime` (Commit #3 pre-emptive edit) so
# runtime's INTERFACE include_private reaches software's compilation.
# No additional listing needed here.
