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
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_pipeline_parity_real.cpp
)

# The test invokes the CLI binary as a subprocess; make sure it is built
# before this test runs, and pass its absolute path as a compile definition.
add_dependencies(chronon3d_pipeline_parity_real_tests chronon3d_cli)
target_compile_definitions(chronon3d_pipeline_parity_real_tests PRIVATE
    CHRONON3D_CLI_PATH="$<TARGET_FILE:chronon3d_cli>"
)

doctest_discover_tests(chronon3d_pipeline_parity_real_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTIES
        TIMEOUT 120
)
