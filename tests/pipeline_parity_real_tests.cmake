# ─── Real pipeline parity tests (chronon3d_pipeline_parity_real_tests) ───
#
# Compares SDK still, CLI still, raw video frame, pruning ON/OFF,
# 1/N threads, and warm/cold cache using real framebuffer hashes.
# Unconditional (no CHRONON3D_BUILD_DIAGNOSTICS gate) because it only
# compares rendered pixels, not audit internals.

chronon3d_add_test_suite(
    NAME chronon3d_pipeline_parity_real_tests
    TIER INTEGRATION
    LINK_TARGETS
        chronon3d_pipeline
        chronon3d_backend_software
        chronon3d_scene
        chronon3d_text_core
        TBB::tbb
    # TICKET-REFACTOR-TESTS-SPLIT-18-19 §A — single executable with 4 split
    # test files + the shared harness impl.  doctest_discover_tests at the
    # bottom picks up all TEST_CASEs across the 4 sources (still: 1,
    # video: 1, runtime_modes: 7, chronon_glow_temporal: 1 = 10 total).
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/text/support/pipeline_parity_harness.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text/test_pipeline_parity_still.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text/test_pipeline_parity_video.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text/test_pipeline_parity_runtime_modes.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text/test_chronon_glow_temporal.cpp
)

# The test invokes the CLI binary as a subprocess; make sure it is built
# before this test runs, and pass its absolute path as a compile definition.
add_dependencies(chronon3d_pipeline_parity_real_tests chronon3d_cli)
target_compile_definitions(chronon3d_pipeline_parity_real_tests PRIVATE
    CHRONON3D_CLI_PATH="$<TARGET_FILE:chronon3d_cli>"
)

# NOTE (TICKET-DOCTEST-DISCOVER-TESTS-ROT-FIX): redundant `doctest_discover_tests`
# REMOVED — canonical `chronon3d_add_test_suite(...)` helper already emits
# WORKING_DIRECTORY + ctest registration (see `cmake/Chronon3DTestSuite.cmake:146`).
# Pre-ADR-018 stale remnant, removed per AGENTS.md Cat-3 anti-duplication.
# §honest semantic loss: per-TEST_CASE TIMEOUT 120 (set via the removed
# `PROPERTIES TIMEOUT 120` on `doctest_discover_tests`) cannot be safely
# translated to a target-level `set_tests_properties(... TIMEOUT 120)`
# because the helper uses a single cumulative `add_test` wrapper (line 194) —
# that would apply 120s to the entire executable's cumulative runtime (CI
# flake risk for the video-completeness matrix + thread×cache variants).
# Per-TEST_CASE timeout recovery deferred to TICKET-DOCTEST-DISCOVER-TESTS-TIMEOUT-RECOVERY.
