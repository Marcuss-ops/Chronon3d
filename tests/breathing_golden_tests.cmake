# ── Breathing Golden Regression Tests ──
# End-to-end golden test for MinimalistImageTrackingBreathing at frame 50.
# Kept separate from chronon3d_renderer_tests to avoid linker OOM.
# Migrated: 3 add_executable → 3 chronon3d_add_test_suite(TIER INTEGRATION).

chronon3d_add_test_suite(
    NAME chronon3d_breathing_golden_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_backend_software chronon3d_scene
    SOURCES golden/test_breathing_golden.cpp
)
target_compile_definitions(chronon3d_breathing_golden_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ── Determinism Check: same frame 5 times, compare hashes ──
chronon3d_add_test_suite(
    NAME chronon3d_determinism_test
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_backend_software chronon3d_scene
    SOURCES golden/test_determinism_breathing.cpp
)
target_compile_definitions(chronon3d_determinism_test PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ── TBB Workers Parallelism Test: verify tbb_active_workers_peak > 1 ──
chronon3d_add_test_suite(
    NAME chronon3d_tbb_workers_test
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_backend_software chronon3d_scene
    SOURCES golden/test_tbb_workers_parallelism.cpp
)
target_compile_definitions(chronon3d_tbb_workers_test PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
