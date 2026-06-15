# tests/CMakeLists.txt — Orchestrator
#
# This file includes per-area test definitions from .cmake sub-files.
# To add a new test target, create a new .cmake file and include it here.
# This keeps merge conflicts small when multiple developers add tests in parallel.

find_package(doctest CONFIG REQUIRED)

# Shared test main
set(TEST_MAIN test_main.cpp)

function(chronon3d_enable_test_pch target_name)
    # PCH disabled to prevent compiler crashes on resource-constrained environments
endfunction()

# ── Per-area test definitions ───────────────────────────────────────
include(${CMAKE_CURRENT_SOURCE_DIR}/core_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/animation_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/scene_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/renderer_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/io_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cli_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/optimizer_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cache_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/content_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/video_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/media_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/compositor_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/breathing_golden_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/visual_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/graphics_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/gradient_visual_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/deterministic_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/benchmarks.cmake)

# ── Aggregate targets: build test executables grouped by cost/speed ──
# Note: benchmarks are conditional; included only when TARGET chronon3d_benchmarks exists
if(TARGET chronon3d_benchmarks)
    set(CHRONON3D_BENCHMARK_DEP
        chronon3d_benchmarks
        chronon3d_scene_program_benchmarks)
else()
    set(CHRONON3D_BENCHMARK_DEP "")
endif()

# Fast tests: core + scene + CLI + deterministic (no rendering, no video, no content)
add_custom_target(chronon3d_tests_fast
    DEPENDS
        chronon3d_core_tests
        chronon3d_scene_tests
        chronon3d_cli_tests
        chronon3d_optimizer_tests
        chronon3d_cache_tests
        chronon3d_compositor_tests
        chronon3d_deterministic_tests
)

# Render tests: scene + renderer + golden + content (heavy, needs software backend + content)
add_custom_target(chronon3d_tests_render
    DEPENDS
        chronon3d_renderer_tests
        chronon3d_io_tests
        chronon3d_animation_tests
        chronon3d_content_tests
        chronon3d_breathing_golden_tests
        chronon3d_visual_test_support_tests
        chronon3d_camera_visual_tests
        chronon3d_cinematic_motion_visual_tests
        chronon3d_gradient_visual_tests
)

# Video tests: video pipeline tests (isolated, no pipeline dependency)
add_custom_target(chronon3d_tests_video
    DEPENDS
        chronon3d_media_video_tests
)

# All tests: everything
add_custom_target(chronon3d_tests
    DEPENDS
        chronon3d_tests_fast
        chronon3d_tests_render
        chronon3d_tests_video
        ${CHRONON3D_BENCHMARK_DEP}
)

add_dependencies(chronon3d_tests chronon3d_media_video_tests)
