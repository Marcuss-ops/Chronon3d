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

if(MSVC)
    add_compile_options(/utf-8 /FS)
endif()
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/FS>")

if(NOT DEFINED CMAKE_C_COMPILER_LAUNCHER AND DEFINED ENV{CMAKE_C_COMPILER_LAUNCHER})
    set(CMAKE_C_COMPILER_LAUNCHER "$ENV{CMAKE_C_COMPILER_LAUNCHER}")
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER AND DEFINED ENV{CMAKE_CXX_COMPILER_LAUNCHER})
    set(CMAKE_CXX_COMPILER_LAUNCHER "$ENV{CMAKE_CXX_COMPILER_LAUNCHER}")
endif()

# Options
option(CHRONON3D_BUILD_TESTS "Build Chronon3d tests" ON)
option(CHRONON3D_BUILD_CLI "Build Chronon3d CLI" ON)
option(CHRONON3D_ENABLE_PROFILING "Enable Tracy profiling" OFF)
option(CHRONON3D_ENABLE_SQLITE_TELEMETRY "Enable SQLite telemetry store" OFF)

if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    add_definitions(-DCHRONON3D_ENABLE_SQLITE_TELEMETRY)
endif()

if(CHRONON3D_BUILD_TESTS)
    add_definitions(-DCHRONON_BUILD_TESTS)
endif()

# Find Dependencies
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(hwy CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(Tracy CONFIG QUIET)
find_package(blend2d CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)



# Capture git commit hash at configure time (works if .git exists)
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

# 1. Base Interface
add_library(chronon3d INTERFACE)
target_include_directories(chronon3d INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/include>
)
target_compile_definitions(chronon3d INTERFACE CHRONON3D_GIT_COMMIT="${CHRONON3D_GIT_COMMIT}")
target_link_libraries(chronon3d INTERFACE glm::glm meshoptimizer::meshoptimizer xxHash::xxhash chronon3d_core_impl)
if(MSVC)
    target_compile_options(chronon3d INTERFACE /EHsc /fp:strict /utf-8)
    target_compile_definitions(chronon3d INTERFACE _DISABLE_STRING_VIEW_FIND_FIRST_NOT_OF_OPTIMIZATION)
else()
    target_compile_options(chronon3d INTERFACE -ffp-contract=off)
endif()

# 3. Registry
set(CHRONON3D_REGISTRY_SOURCES
    src/registry/sampler_registry.cpp
    src/registry/shape_registry.cpp
    src/registry/source_registry.cpp
)
add_library(chronon3d_registry STATIC ${CHRONON3D_REGISTRY_SOURCES})
target_link_libraries(chronon3d_registry PUBLIC chronon3d chronon3d_scene)

# 3b. Assets
set(CHRONON3D_ASSETS_SOURCES
    src/assets/render_preflight.cpp
    src/assets/svg_path_loader.cpp
)
add_library(chronon3d_assets STATIC ${CHRONON3D_ASSETS_SOURCES})
target_link_libraries(chronon3d_assets PUBLIC chronon3d chronon3d_registry)

# 4. Cache
set(CHRONON3D_CACHE_SOURCES
    src/cache/frame_cache.cpp
    src/cache/node_cache.cpp
    src/cache/framebuffer_pool.cpp
)
add_library(chronon3d_cache STATIC ${CHRONON3D_CACHE_SOURCES})
target_link_libraries(chronon3d_cache PUBLIC chronon3d xxHash::xxhash)

# 5. Render Graph
set(CHRONON3D_GRAPH_SOURCES
    src/render_graph/builder/graph_builder_internal.cpp
    src/render_graph/builder/graph_builder_layer_pipeline.cpp
    src/render_graph/builder/graph_builder_pipeline.cpp
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
    src/render_graph/graph_profiler.cpp
    src/render_graph/render_graph.cpp
    src/render_graph/render_pipeline.cpp
    src/render_graph/nodes/transform_node.cpp
)
add_library(chronon3d_graph STATIC ${CHRONON3D_GRAPH_SOURCES})
target_link_libraries(chronon3d_graph PUBLIC chronon3d chronon3d_cache fmt::fmt hwy::hwy tomlplusplus::tomlplusplus)

# 6. Effects
set(CHRONON3D_EFFECTS_SOURCES
    src/effects/effect_registry.cpp
)
add_library(chronon3d_effects STATIC ${CHRONON3D_EFFECTS_SOURCES})
target_link_libraries(chronon3d_effects PUBLIC chronon3d meshoptimizer::meshoptimizer)

# 6b. Runtime
set(CHRONON3D_RUNTIME_SOURCES
    src/runtime/bench_runner.cpp
    src/runtime/legacy_scene_adapter.cpp
    src/runtime/pipeline.cpp
    src/runtime/scene_to_render_graph.cpp
    src/runtime/timeline_evaluator.cpp
    src/runtime/telemetry/jsonl_telemetry_store.cpp
    src/runtime/telemetry/sqlite_telemetry_store.cpp
    src/runtime/telemetry/telemetry_manager.cpp
)
add_library(chronon3d_runtime STATIC ${CHRONON3D_RUNTIME_SOURCES})
target_link_libraries(chronon3d_runtime PUBLIC
    chronon3d chronon3d_graph chronon3d_effects chronon3d_cache
    Taskflow::Taskflow
)

if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    target_link_libraries(chronon3d_runtime PUBLIC unofficial::sqlite3::sqlite3)
endif()

# 7. Backends

# 7a. Image Backend
set(CHRONON3D_BACKEND_IMAGE_SOURCES
    src/backends/image/image_loader.cpp
    src/backends/image/image_writer.cpp
    src/backends/image/stb_image_backend.cpp
)
add_library(chronon3d_backend_image STATIC ${CHRONON3D_BACKEND_IMAGE_SOURCES})
target_link_libraries(chronon3d_backend_image PUBLIC chronon3d spdlog::spdlog fmt::fmt)
target_include_directories(chronon3d_backend_image PRIVATE ${Stb_INCLUDE_DIR})

# 7b. Assets Backend
set(CHRONON3D_BACKEND_ASSETS_SOURCES
    src/backends/assets/image_cache.cpp
    src/backends/assets/image_renderer.cpp
)
add_library(chronon3d_backend_assets STATIC ${CHRONON3D_BACKEND_ASSETS_SOURCES})
target_link_libraries(chronon3d_backend_assets PUBLIC chronon3d chronon3d_backend_image)
target_include_directories(chronon3d_backend_assets PRIVATE ${Stb_INCLUDE_DIR})

# 7d. Video Backend (Generic)
set(CHRONON3D_BACKEND_VIDEO_SOURCES
    src/backends/video/video_source.cpp
)
add_library(chronon3d_backend_video STATIC ${CHRONON3D_BACKEND_VIDEO_SOURCES})
target_link_libraries(chronon3d_backend_video PUBLIC chronon3d)


# 7f. Software Renderer Backend
set(CHRONON3D_BACKEND_SOFTWARE_SOURCES
    src/backends/software/primitive_renderer.cpp
    src/backends/software/processors/builtin_processors.cpp
    src/backends/software/processors/path_processor.cpp
    src/backends/software/processors/software_effect_processors.cpp
    src/backends/software/processors/software_image_processor.cpp
    src/backends/software/processors/software_line_processor.cpp
    src/backends/software/processors/software_mesh_processor.cpp
    src/backends/software/processors/software_shape_processor.cpp
    src/backends/software/processors/software_text_processor.cpp
    src/backends/software/processors/software_utility_processors.cpp
    src/backends/text/text_rasterizer_utils.cpp
    src/backends/software/rasterizers/line_rasterizer.cpp
    src/backends/software/rasterizers/path_rasterizer.cpp
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
    src/backends/software/utils/render_effects_processor.cpp
    src/backends/software/utils/render_hash_utils.cpp
)
add_library(chronon3d_backend_software STATIC ${CHRONON3D_BACKEND_SOFTWARE_SOURCES})
target_link_libraries(chronon3d_backend_software
    PUBLIC 
        chronon3d chronon3d_graph chronon3d_cache chronon3d_effects
        chronon3d_backend_assets chronon3d_backend_image
        blend2d::blend2d
    PRIVATE spdlog::spdlog hwy::hwy fmt::fmt xxHash::xxhash tomlplusplus::tomlplusplus
)
target_include_directories(chronon3d_backend_software PRIVATE ${Stb_INCLUDE_DIR})

# 8. Scene Assembly
set(CHRONON3D_SCENE_SOURCES
    src/scene/camera/camera_motion_presets.cpp
    src/scene/dark_grid_background.cpp
    src/scene/layer.cpp
    src/scene/layer_builder.cpp
    src/scene/layer_builder_delegates.cpp
    src/scene/layer_builder_internal.cpp
    src/scene/render_node_factory.cpp
    src/scene/scene.cpp
    src/scene/scene_builder.cpp
    src/animations/camera/camera_tilt_clips.cpp
    src/animations/camera_motion_applier.cpp
    src/layout/layout_solver.cpp
    src/math/path_utils.cpp
    src/presets/camera_motion_clip.cpp
    src/presets/motion_renderer.cpp
    src/presets/motion_resolver.cpp
    src/specscene/specscene.cpp
)
add_library(chronon3d_scene STATIC ${CHRONON3D_SCENE_SOURCES})
target_include_directories(chronon3d_scene PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_scene
    PUBLIC 
        chronon3d chronon3d_runtime chronon3d_graph chronon3d_registry 
        chronon3d_cache chronon3d_effects chronon3d_backend_image
    PRIVATE spdlog::spdlog fmt::fmt tomlplusplus::tomlplusplus
)

# 9. Compositions
set(CHRONON3D_COMPOSITION_SOURCES
    src/compositions/background_grid.cpp
    src/compositions/demos/chronon_intro_card.cpp
    src/compositions/proof_25d_objects.cpp
    src/compositions/proofs/lil_dirk_clean.cpp
    src/compositions/proofs/motion_depth_proof.cpp
    src/compositions/proofs/motion_object_proof.cpp
    src/compositions/proofs/motion_preset_gallery.cpp
    src/compositions/proofs/proof_depth_ladder.cpp
    src/compositions/proofs/proof_edge_on_rotation.cpp
    src/compositions/proofs/proof_orbit_subject.cpp
    src/compositions/proofs/proof_overlay_fixed.cpp
    src/compositions/proofs/proof_pan_parallax.cpp
    src/compositions/proofs/proof_roll_stability.cpp
    src/compositions/proofs/proof_tilt_parallax.cpp
    src/compositions/proofs/youtube/proof_youtube_depth_title.cpp
    src/compositions/proofs/youtube/proof_youtube_image_dof.cpp
    src/compositions/proofs/youtube/proof_youtube_news_card.cpp
    src/compositions/proofs/youtube/proof_youtube_parallax_thumbnail.cpp
    src/compositions/proofs/youtube/proof_youtube_quote_scene.cpp
)
add_library(chronon3d_compositions STATIC ${CHRONON3D_COMPOSITION_SOURCES})
target_link_libraries(chronon3d_compositions
    PUBLIC
        chronon3d
        chronon3d_scene
        chronon3d_registry
)

# 1b. Core Tracing
add_library(chronon3d_core_impl STATIC src/core/trace.cpp)
target_link_libraries(chronon3d_core_impl PUBLIC chronon3d nlohmann_json::nlohmann_json)

# Architectural Aggregates (Agent rules)
add_library(chronon3d_core INTERFACE)
target_link_libraries(chronon3d_core INTERFACE
    chronon3d_assets chronon3d_registry chronon3d_scene chronon3d_runtime chronon3d_graph chronon3d_cache chronon3d_core_impl
)

add_library(chronon3d_software INTERFACE)
target_link_libraries(chronon3d_software INTERFACE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_backend_software> chronon3d_backend_assets chronon3d_backend_image
)



# 11. Pipeline (Interface)
add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline INTERFACE
    chronon3d_core
    chronon3d_software
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_compositions>
    Taskflow::Taskflow concurrentqueue::concurrentqueue
)

# Profiling
if(CHRONON3D_ENABLE_PROFILING AND Tracy_FOUND)
    target_link_libraries(chronon3d_scene PRIVATE Tracy::TracyClient)
    target_compile_definitions(chronon3d_scene PRIVATE CHRONON_PROFILING TRACY_ON_DEMAND)
    target_link_libraries(chronon3d_pipeline INTERFACE Tracy::TracyClient)
    target_compile_definitions(chronon3d_pipeline INTERFACE CHRONON_PROFILING TRACY_ON_DEMAND)
endif()

# Subdirectories
if(CHRONON3D_BUILD_CLI)
    add_subdirectory(apps/chronon3d_cli)
endif()

if(CHRONON3D_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CHRONON3D_BUILD_TOOLS)
    # Feature preview tool — opt-in developer target.
    add_executable(preview_new_features tools/preview_new_features.cpp)
    target_link_libraries(preview_new_features PRIVATE
        chronon3d_pipeline
        chronon3d_backend_image
        chronon3d_backend_software
        chronon3d_compositions
        chronon3d_graph
        chronon3d_scene
    )
    target_include_directories(preview_new_features PRIVATE ${CMAKE_SOURCE_DIR})
endif()
