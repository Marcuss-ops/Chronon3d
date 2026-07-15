# ── SDK Tests ─────────────────────────────────────────────────────
#
# Tests in tests/sdk/ lock the *manifest-reachable* public surface of
# the Chronon3D SDK — the same surface `tools/install_consumer_test.sh`
# Phase 4 exercises via the standalone consumer.  Every test here
# must compile + run against installed/public headers only.
#
# ADR-013 / TICKET-GATE-10-PHASE-4-BLACK anchors the regression-lock
# pattern: each regression-lock pins a SILENT-FALLBACK class of
# failure, names a stable prefix for diagnostic emission, and links
# to the source-anchor file that owns the contract.

chronon3d_add_test_suite(
    NAME chronon3d_sdk_tests
    TIER SDK
    SOURCES sdk/test_sdk_render_grid_background.cpp
            sdk/test_sdk_archive_manifest.cpp
            sdk/test_sdk_assets_root_isolation.cpp
    # The regression locks exercise the facade end-to-end against the full
    # software/image backend while including only public SDK headers.
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_scene chronon3d_pipeline chronon3d_backend_software chronon3d_backend_image
)

# TICKET-SDK-PACKAGING-CONSOLIDATION -- pass the canonical archive
# path to the test source as a preprocessor string literal. Modern
# cmake auto-escapes values containing spaces, so we use the bare
# form. Defined here because chronon3d_add_test_suite() does not wrap
# compile definitions.
target_compile_definitions(chronon3d_sdk_tests PRIVATE
    CHRONON3D_SDK_ARCHIVE_PATH=${CMAKE_BINARY_DIR}/src/libchronon3d_sdk_impl.a
)
