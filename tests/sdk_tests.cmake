# ── SDK Tests ─────────────────────────────────────────────────────
#
# Tests in tests/sdk/ lock the *manifest-reachable* public surface of
# the Chronon3D SDK — the same surface `tools/install_consumer_test.sh`
# Phase 4 exercises via the standalone consumer.  Every test here
# must compile + run against the installed SDK headers/umbrella
# exclusively (no internal.hpp / advanced/ / runtime.hpp).
#
# ADR-013 / TICKET-GATE-10-PHASE-4-BLACK anchors the regression-lock
# pattern: each regression-lock pins a SILENT-FALLBACK class of
# failure, names a stable prefix for diagnostic emission, and links
# to the source-anchor file that owns the contract.

add_executable(chronon3d_sdk_tests
    ${TEST_MAIN}
    sdk/test_sdk_render_grid_background.cpp
)
target_link_libraries(chronon3d_sdk_tests PRIVATE
    chronon3d_sdk
    chronon3d_sdk_impl
    chronon3d_scene
    chronon3d_pipeline
    chronon3d_backend_software
    doctest::doctest)
target_include_directories(chronon3d_sdk_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_sdk_tests)
add_test(NAME chronon3d_sdk_tests
         COMMAND chronon3d_sdk_tests
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
