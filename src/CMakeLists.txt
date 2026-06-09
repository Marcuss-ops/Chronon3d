# ==============================================================================
# TARGETS DEFINITION (Ordinati per Grafo di Dipendenza Lineare)
# ==============================================================================

# ==============================================================================
# CENTRALIZED EXTERNAL LINK FLAGS
# These are bare linker flags that don't have a CMake imported target. Keep
# all references to a given system library routed through these variables so
# the dependency set lives in one place (and the README build instructions
# stay in sync with the actual link command).
# ==============================================================================
set(CHRONON3D_FFMPEG_LIBS_LIGHT "-lswscale;-lavutil")      # swscale + pixel utils (frame_converter.cpp)
set(CHRONON3D_FFMPEG_LIBS_FULL  "-lswscale;-lavcodec;-lavformat;-lavutil")  # full mux/pipe path (cli_video)

# 1. Core Impl
add_library(chronon3d_core_impl STATIC
    core/profiling.cpp
    core/benchmark_report.cpp
    core/system_metrics.cpp
    core/cancellation_token.cpp
    core/framebuffer_clear_parallel.cpp
)
target_include_directories(chronon3d_core_impl PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/deps/include)
target_link_libraries(chronon3d_core_impl PUBLIC nlohmann_json::nlohmann_json fmt::fmt spdlog::spdlog_header_only)

# 2. Base Interface
add_library(chronon3d INTERFACE)
target_include_directories(chronon3d INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/include>
)
target_compile_definitions(chronon3d INTERFACE 
    CHRONON3D_GIT_COMMIT="${CHRONON3D_GIT_COMMIT}"
    $<$<BOOL:${MSVC}>:_DISABLE_STRING_VIEW_FIND_FIRST_NOT_OF_OPTIMIZATION>
)
option(CHRONON3D_ENABLE_MESH "Enable mesh processing support" ON)

# Only link meshoptimizer when mesh support is active
set(CHRONON3D_MESHOPTIMIZER_LINK "")
if(CHRONON3D_ENABLE_MESH)
    set(CHRONON3D_MESHOPTIMIZER_LINK meshoptimizer::meshoptimizer)
    target_compile_definitions(chronon3d INTERFACE CHRONON3D_ENABLE_MESH)
endif()

target_link_libraries(chronon3d INTERFACE glm::glm ${CHRONON3D_MESHOPTIMIZER_LINK} xxHash::xxhash magic_enum::magic_enum chronon3d_core_impl)

# 3. Cache
set(CHRONON3D_CACHE_SOURCES
    cache/frame_cache.cpp
    cache/node_cache.cpp
    cache/disk_node_cache.cpp
    cache/framebuffer_pool.cpp
    cache/persistent_bake_cache.cpp
    cache/video_frame_cache.cpp
)
add_library(chronon3d_cache STATIC ${CHRONON3D_CACHE_SOURCES})
target_link_libraries(chronon3d_cache PUBLIC chronon3d xxHash::xxhash)
target_precompile_headers(chronon3d_cache PRIVATE <xxhash.h>)

# 4. Render Graph
set(CHRONON3D_GRAPH_SOURCES
    render_graph/builder/graph_builder_internal.cpp
    render_graph/builder/graph_builder_pipeline.cpp
    render_graph/builder/graph_builder_static_analysis.cpp
    render_graph/builder/graph_builder_bbox.cpp
    render_graph/builder/graph_builder_matte.cpp
    render_graph/builder/graph_builder_layer_pipeline.cpp
    render_graph/builder/graph_build_pipeline.cpp
    scene/model/mask_utils.cpp
    render_graph/builder/passes/graph_builder_layer_passes.cpp
    render_graph/builder/passes/graph_builder_lighting_passes.cpp
    render_graph/builder/passes/graph_builder_source_pass.cpp
    render_graph/builder/passes/graph_builder_resolve_pass.cpp
    render_graph/builder/passes/graph_builder_root_sources_pass.cpp
    render_graph/builder/passes/graph_builder_layer_pipeline_pass.cpp
    render_graph/builder/passes/graph_builder_output_pass.cpp
    render_graph/builder/passes/graph_builder_validation_pass.cpp
    render_graph/builder/graph_build_registry.cpp
    render_graph/registry/graph_node_registry.cpp
    render_graph/executor/executor.cpp
    render_graph/executor/input_resolver.cpp
    render_graph/executor/cache_evaluator.cpp
    render_graph/executor/tile_pruning.cpp
    render_graph/executor/telemetry_emitter.cpp
    render_graph/executor/node_runner.cpp
    render_graph/executor/executor_levels.cpp
    render_graph/executor/framebuffer_lifetime.cpp
    render_graph/executor/executor_plan.cpp
    render_graph/graph_profiler.cpp
    render_graph/render_graph.cpp
    render_graph/framebuffer_acquire.cpp
    render_graph/pipeline/scene.cpp
    render_graph/pipeline/scene_dirty.cpp
    render_graph/pipeline/debug.cpp
    render_graph/pipeline/composition.cpp
    render_graph/preflight/preflight.cpp
    render_graph/preflight/analysis.cpp
    render_graph/preflight/analysis_graph.cpp
    render_graph/preflight/analysis_diagnostics.cpp
    render_graph/optimizer/graph_optimizer.cpp
    render_graph/compiler/frame_graph_compiler.cpp
    render_graph/nodes/source_node.cpp
    render_graph/nodes/multi_source_node.cpp
    render_graph/nodes/transform_node.cpp
    render_graph/nodes/transform_kernels.cpp
    render_graph/nodes/transition_node.cpp
    render_graph/nodes/transition/transition_node_math.cpp
)
add_library(chronon3d_graph STATIC ${CHRONON3D_GRAPH_SOURCES})
set_target_properties(chronon3d_graph PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_graph PUBLIC chronon3d chronon3d_cache fmt::fmt hwy::hwy tomlplusplus::tomlplusplus TBB::tbb)
target_precompile_headers(chronon3d_graph PRIVATE
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <fmt/core.h>
)

# 5. Effects
add_library(chronon3d_effects STATIC effects/effect_registry.cpp)
target_link_libraries(chronon3d_effects PUBLIC chronon3d ${CHRONON3D_MESHOPTIMIZER_LINK})

# 5b. Text Backend
set(CHRONON3D_BACKEND_TEXT_SOURCES
    backends/text/text_rasterizer_cache.cpp
    backends/text/text_rasterizer_render.cpp
    backends/text/text_material.cpp
    backends/text/font_engine.cpp
)
add_library(chronon3d_backend_text STATIC ${CHRONON3D_BACKEND_TEXT_SOURCES})
target_link_libraries(chronon3d_backend_text PUBLIC chronon3d chronon3d_cache spdlog::spdlog_header_only blend2d::blend2d freetype harfbuzz::harfbuzz)
target_include_directories(chronon3d_backend_text PRIVATE ${CMAKE_SOURCE_DIR})

# 6. Animations catalog
add_library(chronon3d_animations STATIC
    animations/animation_catalog.cpp
    animations/background_presets.cpp
)
target_link_libraries(chronon3d_animations PUBLIC chronon3d chronon3d_scene chronon3d_backend_software nlohmann_json::nlohmann_json)

# 7. Runtime & Telemetry
set(CHRONON3D_RUNTIME_SOURCES
    runtime/bench_runner.cpp
    runtime/pipeline.cpp
    runtime/scene_to_render_graph.cpp
    runtime/timeline_evaluator.cpp
    runtime/sub_composition_registry.cpp
    runtime/telemetry/telemetry_manager.cpp
    runtime/renderer_warmup.cpp
)
if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    list(APPEND CHRONON3D_RUNTIME_SOURCES
        runtime/telemetry/sqlite/sqlite_telemetry_store.cpp
        runtime/telemetry/sqlite/sqlite_telemetry_store_schema.cpp
    )
else()
    list(APPEND CHRONON3D_RUNTIME_SOURCES
        runtime/telemetry/sqlite/sqlite_telemetry_store_stub.cpp
    )
endif()
add_library(chronon3d_runtime STATIC ${CHRONON3D_RUNTIME_SOURCES})
target_link_libraries(chronon3d_runtime PUBLIC chronon3d chronon3d_graph chronon3d_effects chronon3d_cache Taskflow::Taskflow)
target_include_directories(chronon3d_runtime PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_runtime PRIVATE <chronon3d/detail/pch.hpp>)

# Generate telemetry schema header from canonical .sql file.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/runtime/telemetry/sqlite/telemetry_schema.sql" TELEMETRY_SCHEMA_CONTENT)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/telemetry/sqlite/telemetry_schema.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/telemetry_schema.hpp"
    @ONLY
)
target_include_directories(chronon3d_runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    target_link_libraries(chronon3d_runtime PUBLIC unofficial::sqlite3::sqlite3)
    target_compile_definitions(chronon3d_runtime PUBLIC CHRONON3D_ENABLE_SQLITE_TELEMETRY)
endif()

# 8. Backends (Image, Assets, Video, Software)
set(CHRONON3D_BACKEND_IMAGE_SOURCES
    backends/image/image_loader.cpp
    backends/image/image_writer.cpp
    backends/image/stb_image_backend.cpp
    backends/image/exr_mmap.cpp
)
add_library(chronon3d_backend_image STATIC ${CHRONON3D_BACKEND_IMAGE_SOURCES})
target_link_libraries(chronon3d_backend_image PUBLIC chronon3d spdlog::spdlog_header_only fmt::fmt OpenEXR::OpenEXR)
target_include_directories(chronon3d_backend_image PRIVATE ${Stb_INCLUDE_DIR})
target_include_directories(chronon3d_backend_image PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_backend_image PRIVATE <chronon3d/detail/pch.hpp>)

add_library(chronon3d_backend_assets STATIC backends/assets/image_cache.cpp backends/assets/image_renderer.cpp)
target_link_libraries(chronon3d_backend_assets PUBLIC chronon3d chronon3d_backend_image)
target_include_directories(chronon3d_backend_assets PRIVATE ${Stb_INCLUDE_DIR})# Video Export Backend
set(CHRONON3D_BACKEND_VIDEO_SOURCES
    backends/video/video_source.cpp
    media/frame_conversion/frame_converter.cpp
    media/frame_conversion/direct_yuv_converter.cpp
    media/frame_conversion/direct_yuv_converter_hwy.cpp
    media/frame_conversion/converted_frame_cache.cpp
)
add_library(chronon3d_backend_video STATIC ${CHRONON3D_BACKEND_VIDEO_SOURCES})
set_target_properties(chronon3d_backend_video PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_backend_video PUBLIC chronon3d chronon3d_cache spdlog::spdlog_header_only fmt::fmt TBB::tbb hwy::hwy ${CHRONON3D_FFMPEG_LIBS_LIGHT})
target_include_directories(chronon3d_backend_video PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(chronon3d_graph PUBLIC chronon3d_backend_video)

# Software Renderer Backend
set(CHRONON3D_BACKEND_SOFTWARE_SOURCES
    backends/software/processors/builtin_processors.cpp
    backends/software/processors/path_processor.cpp
    backends/software/processors/software_effect_processors.cpp
    backends/software/processors/image/software_image_processor.cpp
    backends/software/processors/image/software_tiled_image_processor.cpp
    backends/software/kernels/grid_background_kernel.cpp
    backends/software/processors/software_grid_background_processor.cpp
    backends/software/processors/software_mesh_processor.cpp
    backends/software/processors/software_line_processor.cpp
    backends/software/processors/software_shape_processor.cpp
    backends/software/processors/text/software_text_effects.cpp
    backends/software/processors/text/software_text_processor.cpp
    backends/software/processors/text/text_glow.cpp
    backends/software/processors/text/text_shadow.cpp
    backends/software/processors/software_utility_processors.cpp
    backends/software/rasterizers/line_rasterizer.cpp
    backends/software/rasterizers/path/path_cache.cpp
    backends/software/rasterizers/path/path_rasterizer.cpp
    backends/software/rasterizers/path/path_stroke.cpp
    backends/software/rasterizers/path/pip.cpp
    backends/software/rasterizers/card/projected_card_rasterizer.cpp
    backends/software/rasterizers/card/card3d_material_rasterizer.cpp
    backends/software/rasterizers/scanline_rasterizer.cpp
    backends/software/rasterizers/shape_rasterizer.cpp
    backends/software/rasterizers/card/card3d_material_rasterizer.cpp
    backends/software/software_compositor.cpp
    backends/software/software_effect_runner.cpp
    backends/software/software_node_dispatcher.cpp
    backends/software/software_renderer.cpp
    backends/software/utils/framebuffer_analysis.cpp
    backends/software/utils/projection_utils.cpp
    backends/software/utils/blend2d_bridge_core.cpp
    backends/software/utils/blend2d_bridge_transforms.cpp
    backends/software/utils/blend2d_bridge_transforms_fb.cpp
    backends/software/specialized/fake_box3d_renderer.cpp
    backends/software/specialized/grid_plane_renderer.cpp
    backends/software/specialized/mesh_renderer.cpp
    backends/software/utils/effects/effect_blur.cpp
    backends/software/utils/effects/effect_color.cpp
    backends/software/utils/effects/effect_wave.cpp
    backends/software/utils/effects/effect_glow_impl.cpp
    backends/software/utils/effects/effect_shadow_impl.cpp
    backends/software/utils/effects/effect_bloom_impl.cpp
    backends/software/utils/effects/effect_stack.cpp
    backends/software/simd/highway_color_kernels.cpp
    backends/software/simd/highway_rasterize_kernels.cpp
    backends/software/simd/highway_dof_kernels.cpp
)
add_library(chronon3d_backend_software STATIC ${CHRONON3D_BACKEND_SOFTWARE_SOURCES})
set_target_properties(chronon3d_backend_software PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_backend_software
    PUBLIC chronon3d chronon3d_graph chronon3d_cache chronon3d_effects chronon3d_backend_assets chronon3d_backend_image chronon3d_backend_text blend2d::blend2d
    PRIVATE spdlog::spdlog_header_only hwy::hwy fmt::fmt xxHash::xxhash tomlplusplus::tomlplusplus TBB::tbb
)
target_include_directories(chronon3d_backend_software PRIVATE ${Stb_INCLUDE_DIR} ${CMAKE_SOURCE_DIR})
target_include_directories(chronon3d_backend_software PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_backend_software PRIVATE
    <chronon3d/detail/pch.hpp>
    <blend2d.h>
)
# 9. Registry & Scene Assembly
set(CHRONON3D_REGISTRY_SOURCES
    registry/sampler_registry.cpp
    registry/shape_registry.cpp
    registry/source_registry.cpp
)
add_library(chronon3d_registry STATIC ${CHRONON3D_REGISTRY_SOURCES})
set_target_properties(chronon3d_registry PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_registry PUBLIC chronon3d)
target_include_directories(chronon3d_registry PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_registry PRIVATE <chronon3d/detail/pch.hpp>)

set(CHRONON3D_ASSETS_SOURCES
    assets/render_preflight_req.cpp
    assets/render_preflight_validate.cpp
    assets/render_preflight_helpers.cpp
    assets/svg_path_loader.cpp
)
add_library(chronon3d_assets STATIC ${CHRONON3D_ASSETS_SOURCES})
target_link_libraries(chronon3d_assets PUBLIC chronon3d chronon3d_registry)

set(CHRONON3D_SCENE_SOURCES
    scene/camera/camera_motion_presets.cpp
    scene/camera/camera_rig.cpp
    scene/camera/camera_rig_presets.cpp
    scene/camera/camera_projection.cpp
    scene/camera/camera_shot_validator.cpp
    scene/camera/camera_framing.cpp
    scene/camera/camera_path_sampler.cpp
    scene/camera/camera_debug_overlay.cpp
    scene/camera/overlay_hud_panels.cpp
    scene/camera/overlay_kinematic_panels.cpp
    scene/camera/overlay_spatial_panels.cpp
    scene/model/layer.cpp
    scene/model/render_node_factory.cpp
    scene/model/transform_resolver.cpp
    scene/utils/dark_grid_background.cpp
    scene/builders/layer_builder.cpp
    scene/builders/layer_builder_effects.cpp
    scene/builders/layer_command_registry.cpp
    scene/builders/commands/motion_preset_methods.cpp
    scene/builders/commands/motion_preset_commands.cpp
    scene/builders/commands/shape_commands.cpp
    scene/builders/commands/node_transform_commands.cpp
    scene/builders/commands/layer_property_commands.cpp
    scene/validation/scene_validator.cpp
    scene/validation/scene_validation_registry.cpp
    scene/builders/camera_api.cpp
    scene/builders/scene_builder.cpp
    timeline/timeline_builder.cpp
    animations/camera/camera_tilt_clips.cpp
    animations/camera_motion_applier.cpp
    layout/layout_solver.cpp
    math/path_utils.cpp
    presets/camera_motion_clip.cpp
    presets/motion_renderer.cpp
    presets/motion_resolver.cpp
    presets/motion_preset_registry.cpp
    presets/motion_presets_reveal.cpp
    presets/motion_presets_3d.cpp
    presets/motion_presets_glow.cpp
    presets/motion_presets_idle.cpp
    presets/motion_presets_impact.cpp
    presets/phrase_presets.cpp
    presets/phrase_group_builder.cpp
    specscene/model/specscene.cpp
    specscene/parser/specscene_parsers.cpp
    specscene/compiler/specscene_compiler.cpp
    specscene/validation/specscene_validator.cpp
    media/media_placement.cpp
    vector/path_factories.cpp
)
add_library(chronon3d_scene STATIC ${CHRONON3D_SCENE_SOURCES})
target_include_directories(chronon3d_scene PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_scene
    PUBLIC chronon3d chronon3d_runtime chronon3d_graph chronon3d_registry chronon3d_cache chronon3d_effects chronon3d_backend_image
    PRIVATE spdlog::spdlog_header_only fmt::fmt tomlplusplus::tomlplusplus
)
target_include_directories(chronon3d_scene PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_scene PRIVATE <chronon3d/detail/pch.hpp>)

# --- Aggregati Architetturali finali ---
# WHOLE_ARCHIVE is needed for targets containing CHRONON_REGISTER_* macros.
# These rely on static initialization which can be dead-code-eliminated if no
# symbol is visibly referenced. Using WHOLE_ARCHIVE only on registration targets
# (scene, animations, backend_video) avoids over-linking while ensuring
# registration symbols survive linking.

# 9b. Extension System
add_library(chronon3d_extension STATIC extension/extension_registry.cpp extension/extension_loader.cpp)
target_link_libraries(chronon3d_extension PUBLIC chronon3d chronon3d_graph chronon3d_registry chronon3d_effects)
target_include_directories(chronon3d_extension PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_extension PRIVATE <chronon3d/detail/pch.hpp>)

# --- Aggregati Architetturali finali ---
add_library(chronon3d_core INTERFACE)
target_link_libraries(chronon3d_core INTERFACE
    chronon3d_extension
    chronon3d_assets
    chronon3d_registry
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_scene>
    chronon3d_runtime
    chronon3d_graph
    chronon3d_cache
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_animations>
    chronon3d_core_impl
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_backend_video>
)

add_library(chronon3d_software INTERFACE)
target_link_libraries(chronon3d_software INTERFACE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_backend_software>
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_backend_video>
    chronon3d_backend_assets
    chronon3d_backend_image
)

add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline INTERFACE chronon3d_core chronon3d_software Taskflow::Taskflow concurrentqueue::concurrentqueue yuv
    ${CHRONON3D_FFMPEG_LIBS_LIGHT})
