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
# Guarded by CHRONON3D_BUILD_TESTS (the project's own flag) rather than
# BUILD_TESTING, because BUILD_TESTING is only set by `include(CTest)`
# whereas `enable_testing()` (used at the top level) does NOT define it.
# Without this alignment, the per-area test executables and the
# chronon3d_tests_fast / chronon3d_tests_render / chronon3d_tests_video
# aggregate custom targets are silently dropped, which makes the
# chronon3d_dev_fast aggregate target unbuildable (ninja reports the
# custom target as a missing rule).
if(CHRONON3D_BUILD_TESTS)
include(${CMAKE_CURRENT_SOURCE_DIR}/core_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/scene_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cli_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/optimizer_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cache_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/compositor_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/authoring_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/benchmarks.cmake)
# Expressions V2 tests are quarantined under experimental/ and gated by
# CHRONON3D_BUILD_EXPERIMENTAL. See docs/EXPRESSIONS_V2_PROMOTION.md.

# Tests requiring Blend2D + text
if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    include(${CMAKE_CURRENT_SOURCE_DIR}/animation_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/renderer_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/io_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/breathing_golden_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/visual_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/graphics_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/gradient_visual_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/deterministic_tests.cmake)
endif()

# Tests requiring content
if(CHRONON3D_BUILD_CONTENT)
    include(${CMAKE_CURRENT_SOURCE_DIR}/content_tests.cmake)
endif()

# Tests requiring video
if(CHRONON3D_ENABLE_VIDEO)
    include(${CMAKE_CURRENT_SOURCE_DIR}/video_tests.cmake)
endif()

if(CHRONON3D_ENABLE_VIDEO)
    include(${CMAKE_CURRENT_SOURCE_DIR}/media_tests.cmake)
endif()

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
set(CHRONON3D_FAST_TEST_DEPS
    chronon3d_core_tests
    chronon3d_scene_tests
    chronon3d_optimizer_tests
    chronon3d_cache_tests
    chronon3d_compositor_tests
    chronon3d_authoring_tests
)
if(CHRONON3D_USE_BLEND2D)
    list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_deterministic_tests)
endif()
if(TARGET chronon3d_cli_tests)
    list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_cli_tests)
endif()
add_custom_target(chronon3d_tests_fast
    DEPENDS ${CHRONON3D_FAST_TEST_DEPS}
)

# Render tests: scene + renderer + golden + content (heavy, needs software backend + content)
set(CHRONON3D_RENDER_TEST_DEPS "")
if(TARGET chronon3d_renderer_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_renderer_tests)
endif()
if(TARGET chronon3d_io_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_io_tests)
endif()
if(TARGET chronon3d_animation_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_animation_tests)
endif()
if(TARGET chronon3d_content_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_content_tests)
endif()
if(TARGET chronon3d_breathing_golden_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_breathing_golden_tests)
endif()
if(TARGET chronon3d_visual_test_support_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_visual_test_support_tests)
endif()
if(TARGET chronon3d_camera_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_camera_visual_tests)
endif()
if(TARGET chronon3d_cinematic_motion_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_cinematic_motion_visual_tests)
endif()
if(TARGET chronon3d_gradient_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_gradient_visual_tests)
endif()
if(TARGET chronon3d_render_graph_node_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_render_graph_node_visual_tests)
endif()
if(TARGET chronon3d_pr3_composition_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_pr3_composition_visual_tests)
endif()
add_custom_target(chronon3d_tests_render
    DEPENDS ${CHRONON3D_RENDER_TEST_DEPS}
)

# Video tests: video pipeline tests (isolated, no pipeline dependency)
if(TARGET chronon3d_media_video_tests)
    add_custom_target(chronon3d_tests_video
        DEPENDS
            chronon3d_media_video_tests
    )
else()
    add_custom_target(chronon3d_tests_video)
endif()

# All tests: everything
add_custom_target(chronon3d_tests
    DEPENDS
        chronon3d_tests_fast
        chronon3d_tests_render
        chronon3d_tests_video
        ${CHRONON3D_BENCHMARK_DEP}
)

if(TARGET chronon3d_media_video_tests)
    add_dependencies(chronon3d_tests chronon3d_media_video_tests)
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/architecture_tests.cmake)
endif()
