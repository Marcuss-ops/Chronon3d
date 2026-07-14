# tests/showcase/CMakeLists.txt — AGENT 4 Cinematic Camera Showcase Tests
#
# Local test target registry for AGENT 4 / TICKET-A4:
#   * chronon3d_cinematic_camera_showcase_tests — six DoD gates
#     (A4.1 frame non-empty / A4.2 camera motion / A4.3 text motion /
#      A4.4 determinism / A4.5 contact sheet PNG / A4.6 performance).
#   * Composition under test: deep_parallax_cascade (180 frames,
#     Catmull-Rom camera path + 4 stacked text layers).
#
# This file is gated by CHRONON3D_BUILD_CONTENT because the
# cinematic_text_camera compositions live in content/anims/compositions/.
# Content is the only "core" code path exercised; Agent 1 (camera) and
# Agent 2 (text) core code are intentionally NOT touched.
#
# Phase-2.2 mechanical split — the monolithic
# tests/showcase/test_cinematic_camera_showcase.cpp (was 771 LOC)  # drift-allow: stale-ref
# decomposes into a single executable (no separate CMake target per
# gate) that links 4 test TUs + 1 shared fixture TU:
#
#   cinematic/cinematic_showcase_fixture.{hpp,cpp}
#       shared frame-analysis helpers + FrameMetrics + FNV-1a hash +
#       render_frames() definition.  ODR-safe across the 4 TUs via
#       `inline` keyword on every helper.
#   cinematic/cinematic_showcase_config.hpp
#       env-var driven runtime-mode config (CHRONON3D_CINEMATIC_FRAME/
#       COMP_COUNT, CHRONON3D_CINEMATIC_PERF accept-list).  Single
#       inline const source-of-truth shared across TUs.
#   cinematic/test_cinematic_smoke.cpp
#       TEST_CASE A4.1 (every keyframe non-empty) + A4.2 (camera motion).# cinematic/test_cinematic_presets.cpp
#   TEST_CASE A4.3 (text motion per preset, 5/5 strict) +
#   TEST_CASE A4.3-strict (>= 5 cinematic compositions contract).
# cinematic/test_cinematic_determinism.cpp
#   TEST_CASE A4.4 (run A == run B byte-exact).
# cinematic/test_cinematic_artifacts.cpp
#   TEST_CASE A4.5 (3×2 contact sheet PNG) +
#   TEST_CASE A4.6 (perf telemetry, TICKET-053 envelope).
#
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
# Compiled only when CHRONON3D_BUILD_CONTENT is on (matches the
# pre-refactor orchestrator's `if(CHRONON3D_BUILD_CONTENT)` block).
if(NOT CHRONON3D_BUILD_CONTENT)
    return()
endif()

# ── Showcase end-to-end verification test binary ──────────────────────
# Path note: `include(${CMAKE_CURRENT_SOURCE_DIR}/showcase/CMakeLists.txt)`
# in the parent tests/CMakeLists.txt preserves the parent's scope, so
# CMAKE_CURRENT_SOURCE_DIR here is still `tests/` (not `tests/showcase/`).
# That means src paths must be relative to THE PARENT directory, exactly
# as in tests/visual_tests.cmake's `add_executable(... visual/camera/...)`
# pattern.  Hence `cinematic/*.cpp` (not `tests/showcase/cinematic/*.cpp`).
# Without this prefix CMake config fails with "Cannot find source file"
# because it would look at `cinematic/*.cpp` from a wrong base.
add_executable(chronon3d_cinematic_camera_showcase_tests
    ${TEST_MAIN}
    showcase/cinematic/cinematic_showcase_fixture.cpp
    showcase/cinematic/test_cinematic_smoke.cpp
    showcase/cinematic/test_cinematic_presets.cpp
    showcase/cinematic/test_cinematic_determinism.cpp
    showcase/cinematic/test_cinematic_artifacts.cpp
)

target_link_libraries(chronon3d_cinematic_camera_showcase_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_backend_software
        chronon3d_backend_image
        chronon3d_scene
        chronon3d_content
        doctest::doctest
)

target_compile_definitions(chronon3d_cinematic_camera_showcase_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(chronon3d_cinematic_camera_showcase_tests
    PRIVATE ${CMAKE_SOURCE_DIR}
)

set_target_properties(chronon3d_cinematic_camera_showcase_tests
    PROPERTIES UNITY_BUILD OFF
)

chronon3d_enable_test_pch(chronon3d_cinematic_camera_showcase_tests)

add_test(
    NAME chronon3d_cinematic_camera_showcase_tests
    COMMAND chronon3d_cinematic_camera_showcase_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
