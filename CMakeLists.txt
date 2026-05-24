cmake_minimum_required(VERSION 3.24)

project(Chronon3d
    VERSION 0.1.0
    DESCRIPTION "A modular C++20 3D engine/library"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# --- Opzioni di Compilazione Globali ---
if(MSVC)
    add_compile_options(/utf-8 /FS /EHsc /fp:strict)
else()
    add_compile_options(-ffp-contract=off)
endif()

# --- Configurazione Strumenti di Build (ccache / linkers) ---
if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER AND DEFINED ENV{CMAKE_CXX_COMPILER_LAUNCHER})
    set(CMAKE_CXX_COMPILER_LAUNCHER "$ENV{CMAKE_CXX_COMPILER_LAUNCHER}")
    set(CMAKE_C_COMPILER_LAUNCHER "$ENV{CMAKE_C_COMPILER_LAUNCHER}")
endif()

if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "ccache found: ${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "C++ compiler launcher")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "C compiler launcher")
    endif()
endif()

if(NOT MSVC AND NOT CMAKE_CXX_LINKER_LAUNCHER)
    find_program(MOLD_PROGRAM mold)
    if(MOLD_PROGRAM)
        message(STATUS "mold found: ${MOLD_PROGRAM}")
        add_link_options("-fuse-ld=mold")
    else()
        find_program(LLD_PROGRAM ld.lld)
        if(LLD_PROGRAM)
            message(STATUS "lld found: ${LLD_PROGRAM}")
            add_link_options("-fuse-ld=lld")
        endif()
    endif()
endif()

# --- Opzioni del Progetto ---
option(CHRONON3D_UNITY_BUILD "Enable unity (jumbo) build" ON)
option(CHRONON3D_BUILD_TESTS "Build Chronon3d tests" ON)
option(CHRONON3D_BUILD_CLI "Build Chronon3d CLI" ON)
option(CHRONON3D_BUILD_TOOLS "Build development tools" OFF)
option(CHRONON3D_ENABLE_PROFILING "Enable Tracy profiling" OFF)
option(CHRONON3D_ENABLE_SQLITE_TELEMETRY "Enable SQLite telemetry store" ON)

if(CHRONON3D_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
    set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)
endif()

# --- Ricerca Dipendenze ---
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(hwy CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(blend2d CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)

if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_PROFILING)
    find_package(Tracy CONFIG QUIET)
endif()

# --- Git Commit Hash Extraction ---
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CHRONON3D_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CHRONON3D_GIT_COMMIT)
    set(CHRONON3D_GIT_COMMIT "unknown")
endif()

# ==============================================================================
# TARGETS DEFINITION (Ordinati per Grafo di Dipendenza Lineare)
# ==============================================================================

# 1. Core Impl (Spostato in alto per evitare anomalie di risoluzione del target)
add_library(chronon3d_core_impl STATIC src/core/trace.cpp src/core/benchmark_report.cpp src/core/render_telemetry_detail.cpp)
target_include_directories(chronon3d_core_impl PUBLIC include deps/include)
target_link_libraries(chronon3d_core_impl PUBLIC nlohmann_json::nlohmann_json fmt::fmt spdlog::spdlog_header_only)

# 2. Base Interface
add_library(chronon3d INTERFACE)
target_include_directories(chronon3d INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/include>
)
target_compile_definitions(chronon3d INTERFACE 
    CHRONON3D_GIT_COMMIT="${CHRONON3D_GIT_COMMIT}"
    $<$<BOOL:${MSVC}>:_DISABLE_STRING_VIEW_FIND_FIRST_NOT_OF_OPTIMIZATION>
)
target_link_libraries(chronon3d INTERFACE glm::glm meshoptimizer::meshoptimizer xxHash::xxhash chronon3d_core_impl)

# 3. Cache
set(CHRONON3D_CACHE_SOURCES
    src/cache/frame_cache.cpp
    src/cache/node_cache.cpp
    src/cache/disk_node_cache.cpp
    src/cache/framebuffer_pool.cpp
    src/cache/video_frame_cache.cpp
)
add_library(chronon3d_cache STATIC ${CHRONON3D_CACHE_SOURCES})
target_link_libraries(chronon3d_cache PUBLIC chronon3d xxHash::xxhash)
target_precompile_headers(chronon3d_cache PRIVATE <xxhash.h>)

# 4. Render Graph
set(CHRONON3D_GRAPH_SOURCES
    src/render_graph/builder/graph_builder_internal.cpp
    src/render_graph/builder/graph_builder_layer_pipeline.cpp
    src/render_graph/builder/graph_builder_pipeline.cpp
    src/render_graph/builder/graph_builder_bbox.cpp
    src/render_graph/builder/graph_builder_static.cpp
    src/scene/mask/mask_utils.cpp
    src/render_graph/builder/passes/graph_builder_composite_pass.cpp
    src/render_graph/builder/passes/graph_builder_effect_pass.cpp
    src/render_graph/builder/passes/graph_builder_lighting_pass.cpp
    src/render_graph/builder/passes/graph_builder_mask_pass.cpp
    src/render_graph/builder/passes/graph_builder_shadow_pass.cpp
    src/render_graph/builder/passes/graph_builder_source_pass.cpp
    src/render_graph/builder/passes/graph_builder_transform_pass.cpp
    src/render_graph/builder/utils/graph_builder_camera25d_sorter.cpp
    src/render_graph/graph_builder.cpp
    src/render_graph/graph_executor.cpp
    src/render_graph/executor/graph_executor_plan.cpp
    src/render_graph/executor/graph_executor_cache.cpp
    src/render_graph/executor/graph_executor_dirty.cpp
    src/render_graph/executor/graph_executor_telemetry.cpp
    src/render_graph/executor/graph_executor_phases.cpp
    src/render_graph/graph_profiler.cpp
    src/render_graph/render_graph.cpp
    src/render_graph/render_pipeline_scene.cpp
    src/render_graph/render_pipeline_scene_dirty.cpp
    src/render_graph/render_pipeline_debug.cpp
    src/render_graph/render_pipeline_composition.cpp
    src/render_graph/graph_filter.cpp
    src/render_graph/nodes/source_node.cpp
    src/render_graph/nodes/multi_source_node.cpp
    src/render_graph/nodes/transform_node.cpp
    src/render_graph/nodes/transform_translate.cpp
    src/render_graph/nodes/transform_affine.cpp
    src/render_graph/nodes/transform_projective.cpp
    src/render_graph/nodes/transition_node.cpp
    src/render_graph/nodes/transition/transition_node_math.cpp
)
add_library(chronon3d_graph STATIC ${CHRONON3D_GRAPH_SOURCES})
target_link_libraries(chronon3d_graph PUBLIC chronon3d chronon3d_cache fmt::fmt hwy::hwy tomlplusplus::tomlplusplus TBB::tbb)
target_precompile_headers(chronon3d_graph PRIVATE
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <fmt/core.h>
)

# 5. Effects
add_library(chronon3d_effects STATIC src/effects/effect_registry.cpp)
target_link_libraries(chronon3d_effects PUBLIC chronon3d meshoptimizer::meshoptimizer)

# 6. Animations catalog
add_library(chronon3d_animations STATIC
    src/animations/animation_catalog.cpp
    src/animations/background_presets.cpp
)
target_link_libraries(chronon3d_animations PUBLIC chronon3d chronon3d_scene chronon3d_backend_software nlohmann_json::nlohmann_json)

# 7. Runtime & Telemetry
set(CHRONON3D_RUNTIME_SOURCES
    src/runtime/bench_runner.cpp
    src/runtime/pipeline.cpp
    src/runtime/scene_to_render_graph.cpp
    src/runtime/timeline_evaluator.cpp
    src/runtime/telemetry/jsonl/jsonl_serializers.cpp
    src/runtime/telemetry/jsonl/jsonl_telemetry_store.cpp
    src/runtime/telemetry/sqlite/sqlite_telemetry_store.cpp
    src/runtime/telemetry/sqlite/sqlite_telemetry_store_schema.cpp
    src/runtime/telemetry/sqlite/sqlite_telemetry_store_write.cpp
    src/runtime/telemetry/sqlite/sqlite_telemetry_store_batch.cpp
    src/runtime/telemetry/sqlite/sqlite_telemetry_store_events.cpp
    src/runtime/telemetry/sqlite/sqlite_telemetry_store_media.cpp
    src/runtime/telemetry/sqlite/sqlite_telemetry_store_stub.cpp
    src/runtime/telemetry/telemetry_manager.cpp
    src/runtime/renderer_warmup.cpp
)
add_library(chronon3d_runtime STATIC ${CHRONON3D_RUNTIME_SOURCES})
target_link_libraries(chronon3d_runtime PUBLIC chronon3d chronon3d_graph chronon3d_effects chronon3d_cache Taskflow::Taskflow)
target_precompile_headers(chronon3d_runtime PRIVATE <spdlog/spdlog.h> <fmt/core.h> <glm/glm.hpp>)

if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    target_link_libraries(chronon3d_runtime PUBLIC unofficial::sqlite3::sqlite3)
    target_compile_definitions(chronon3d_runtime PRIVATE CHRONON3D_ENABLE_SQLITE_TELEMETRY)

    # Generate telemetry schema header from canonical .sql file
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/telemetry/sqlite/telemetry_schema.sql" TELEMETRY_SCHEMA_CONTENT)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/telemetry/sqlite/telemetry_schema.hpp.in"
        "${CMAKE_CURRENT_BINARY_DIR}/telemetry_schema.hpp"
        @ONLY
    )
    target_include_directories(chronon3d_runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# 8. Backends (Image, Assets, Video, Software)
set(CHRONON3D_BACKEND_IMAGE_SOURCES
    src/backends/image/image_loader.cpp
    src/backends/image/image_writer.cpp
    src/backends/image/stb_image_backend.cpp
    src/backends/image/exr_mmap.cpp
)
add_library(chronon3d_backend_image STATIC ${CHRONON3D_BACKEND_IMAGE_SOURCES})
target_link_libraries(chronon3d_backend_image PUBLIC chronon3d spdlog::spdlog_header_only fmt::fmt OpenEXR::OpenEXR)
target_include_directories(chronon3d_backend_image PRIVATE ${Stb_INCLUDE_DIR})
target_precompile_headers(chronon3d_backend_image PRIVATE <spdlog/spdlog.h> <fmt/core.h>)

add_library(chronon3d_backend_assets STATIC src/backends/assets/image_cache.cpp src/backends/assets/image_renderer.cpp)
target_link_libraries(chronon3d_backend_assets PUBLIC chronon3d chronon3d_backend_image)
target_include_directories(chronon3d_backend_assets PRIVATE ${Stb_INCLUDE_DIR})


# Video Export Backend
set(CHRONON3D_BACKEND_VIDEO_SOURCES
    src/backends/video/video_export.cpp
    src/backends/video/video_source.cpp
)
add_library(chronon3d_backend_video STATIC ${CHRONON3D_BACKEND_VIDEO_SOURCES})
target_link_libraries(chronon3d_backend_video PUBLIC chronon3d chronon3d_cache spdlog::spdlog_header_only fmt::fmt TBB::tbb hwy::hwy)
target_include_directories(chronon3d_backend_video PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Software Renderer Backend
set(CHRONON3D_BACKEND_SOFTWARE_SOURCES
    src/backends/software/primitive_renderer.cpp
    src/backends/software/processors/builtin_processors.cpp
    src/backends/software/processors/path_processor.cpp
    src/backends/software/processors/software_effect_processors.cpp
    src/backends/software/processors/software_image_processor.cpp
    src/backends/software/processors/software_tiled_image_processor.cpp
    src/backends/software/processors/software_grid_background_processor.cpp
    src/backends/software/processors/software_mesh_processor.cpp
    src/backends/software/processors/software_line_processor.cpp
    src/backends/software/processors/software_shape_processor.cpp
    src/backends/software/processors/software_text_processor.cpp
    src/backends/software/processors/software_text_processor_hash.cpp
    src/backends/software/processors/software_text_processor_cache.cpp
    src/backends/software/processors/software_text_processor_shadow.cpp
    src/backends/software/processors/software_text_processor_glow.cpp
    src/backends/software/processors/software_utility_processors.cpp
    src/backends/text/text_rasterizer_utils.cpp
    src/backends/software/rasterizers/line_rasterizer.cpp
    src/backends/software/rasterizers/path_cache.cpp
    src/backends/software/rasterizers/path_fill.cpp
    src/backends/software/rasterizers/path_rasterizer.cpp
    src/backends/software/rasterizers/path_stroke.cpp
    src/backends/software/rasterizers/pip.cpp
    src/backends/software/rasterizers/projected_card_rasterizer.cpp
    src/backends/software/rasterizers/scanline_rasterizer.cpp
    src/backends/software/rasterizers/shape_rasterizer.cpp
    src/backends/software/software_compositor.cpp
    src/backends/software/software_effect_runner.cpp
    src/backends/software/software_node_dispatcher.cpp
    src/backends/software/software_renderer.cpp
    src/backends/software/specialized/fake_box3d_renderer.cpp
    src/backends/software/specialized/glass_panel_renderer.cpp
    src/backends/software/specialized/grid_plane_renderer.cpp
    src/backends/software/specialized/mesh_renderer.cpp
    src/backends/software/utils/framebuffer_analysis.cpp
    src/backends/software/utils/projection_utils.cpp
    src/backends/software/utils/blend2d_bridge.cpp
    src/backends/software/utils/blend2d_bridge/bl2d_sampling.cpp
    src/backends/software/utils/blend2d_bridge/bl2d_compositing.cpp
    src/backends/software/utils/blend2d_bridge/bl2d_blimage_transformed.cpp
    src/backends/software/utils/blend2d_bridge/bl2d_blimage_tiled.cpp
    src/backends/software/utils/blend2d_bridge/bl2d_framebuffer_transformed.cpp
    src/backends/software/utils/effects/effect_blur.cpp
    src/backends/software/utils/effects/effect_color.cpp
    src/backends/software/utils/effects/effect_wave.cpp
    src/backends/software/utils/effects/effect_stack.cpp
    src/backends/software/utils/render_hash_utils.cpp
    src/backends/software/simd/highway_kernels.cpp
)
add_library(chronon3d_backend_software STATIC ${CHRONON3D_BACKEND_SOFTWARE_SOURCES})
target_link_libraries(chronon3d_backend_software
    PUBLIC chronon3d chronon3d_graph chronon3d_cache chronon3d_effects chronon3d_backend_assets chronon3d_backend_image blend2d::blend2d
    PRIVATE spdlog::spdlog_header_only hwy::hwy fmt::fmt xxHash::xxhash tomlplusplus::tomlplusplus TBB::tbb
)
target_include_directories(chronon3d_backend_software PRIVATE ${Stb_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(chronon3d_backend_software PRIVATE
    <glm/glm.hpp> <glm/gtc/matrix_transform.hpp> <glm/gtc/type_ptr.hpp> <glm/gtc/quaternion.hpp>
    <blend2d.h> <spdlog/spdlog.h> <fmt/core.h>
)

# 9. Registry & Scene Assembly (Risolto il ciclo: Registry NON linka direttamente scene qui)
set(CHRONON3D_REGISTRY_SOURCES
    src/registry/sampler_registry.cpp
    src/registry/shape_registry.cpp
    src/registry/source_registry.cpp
)
add_library(chronon3d_registry STATIC ${CHRONON3D_REGISTRY_SOURCES})
target_link_libraries(chronon3d_registry PUBLIC chronon3d) # Rimossa dipendenza diretta circolare da chronon3d_scene
target_precompile_headers(chronon3d_registry PRIVATE <glm/glm.hpp> <glm/gtc/matrix_transform.hpp> <spdlog/spdlog.h> <fmt/core.h>)

set(CHRONON3D_ASSETS_SOURCES src/assets/render_preflight.cpp src/assets/svg_path_loader.cpp)
add_library(chronon3d_assets STATIC ${CHRONON3D_ASSETS_SOURCES})
target_link_libraries(chronon3d_assets PUBLIC chronon3d chronon3d_registry)

set(CHRONON3D_SCENE_SOURCES
    src/scene/camera/camera_motion_presets.cpp
    src/scene/dark_grid_background.cpp
    src/scene/layer.cpp
    src/scene/layer_builder.cpp
    src/scene/layer_builder_delegates.cpp
    src/scene/layer_builder_effects.cpp
    src/scene/layer_builder_internal.cpp
    src/scene/render_node_factory.cpp
    src/scene/scene.cpp
    src/scene/camera_api.cpp
    src/scene/scene_builder.cpp
    src/animations/camera/camera_tilt_clips.cpp
    src/animations/camera_motion_applier.cpp
    src/layout/layout_solver.cpp
    src/math/path_utils.cpp
    src/presets/camera_motion_clip.cpp
    src/presets/motion_renderer.cpp
    src/presets/motion_resolver.cpp
    src/presets/phrase_presets.cpp
    src/specscene/specscene.cpp
    src/specscene/specscene_parsers.cpp
)
add_library(chronon3d_scene STATIC ${CHRONON3D_SCENE_SOURCES})
target_include_directories(chronon3d_scene PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_scene
    PUBLIC chronon3d chronon3d_runtime chronon3d_graph chronon3d_registry chronon3d_cache chronon3d_effects chronon3d_backend_image
    PRIVATE spdlog::spdlog_header_only fmt::fmt tomlplusplus::tomlplusplus
)
target_precompile_headers(chronon3d_scene PRIVATE <glm/glm.hpp> <glm/gtc/matrix_transform.hpp> <spdlog/spdlog.h> <fmt/core.h>)

# --- Aggregati Architetturali finali ---
add_library(chronon3d_core INTERFACE)
target_link_libraries(chronon3d_core INTERFACE
    chronon3d_assets chronon3d_registry chronon3d_scene chronon3d_runtime chronon3d_graph chronon3d_cache $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_animations> chronon3d_core_impl
)

add_library(chronon3d_software INTERFACE)
target_link_libraries(chronon3d_software INTERFACE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_backend_software> chronon3d_backend_assets chronon3d_backend_image chronon3d_backend_video
)

add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline INTERFACE chronon3d_core chronon3d_software Taskflow::Taskflow concurrentqueue::concurrentqueue)

# --- Profiling & Condizionali di Build ---
if(CHRONON3D_ENABLE_PROFILING AND Tracy_FOUND)
    target_link_libraries(chronon3d_scene PRIVATE Tracy::TracyClient)
    target_compile_definitions(chronon3d_scene PRIVATE CHRONON_PROFILING TRACY_ON_DEMAND)
    target_link_libraries(chronon3d_pipeline INTERFACE Tracy::TracyClient)
    target_compile_definitions(chronon3d_pipeline INTERFACE CHRONON_PROFILING TRACY_ON_DEMAND)
endif()

if(CHRONON3D_BUILD_TESTS)
    enable_testing()
    target_compile_definitions(chronon3d_core_impl PUBLIC CHRONON_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(CHRONON3D_BUILD_CLI)
    add_subdirectory(apps/chronon3d_cli)
endif()

if(CHRONON3D_BUILD_TOOLS)
    add_executable(preview_new_features tools/preview_new_features.cpp)
    target_link_libraries(preview_new_features PRIVATE chronon3d_pipeline chronon3d_backend_image chronon3d_backend_software chronon3d_graph chronon3d_scene)
    target_include_directories(preview_new_features PRIVATE ${CMAKE_SOURCE_DIR})
endif()
