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
#
# §11.1 / §12.1 — register via chronon3d_add_test_suite() (canonical
# test-suite registration) so the §12.3 source-registry gate sees
# these source paths and the test target is tracked under the SDK
# tier for the §11.4 gate.  The previous raw add_executable / link /
# include / add_test block bypassed the registry, leaving both
# `sdk/test_sdk_render_grid_background.cpp` and
# `sdk/test_sdk_archive_manifest.cpp` as orphan sources (per the
# §12.3 gate) and the executable off the SDK-tier tracker.

chronon3d_add_test_suite(
    NAME chronon3d_sdk_tests
    TIER SDK
    SOURCES sdk/test_sdk_render_grid_background.cpp
            sdk/test_sdk_archive_manifest.cpp
    # LINK_TARGETS override (AUTHOR_WARNING expected): the default
    # SDK tier link is just `chronon3d_sdk` (the public INTERFACE
    # alias); the regression-lock + archive-manifest tests exercise
    # the SDK facade end-to-end against the full backend, so we link
    # the same per-subsystem targets the previous raw block did.
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_scene chronon3d_pipeline chronon3d_backend_software
)

# TICKET-SDK-PACKAGING-CONSOLIDATION -- pass the canonical archive
# path to the test source as a preprocessor string literal. Modern
# cmake auto-escapes values containing spaces, so we use the bare
# form.  Defined here (after the macro call) because
# chronon3d_add_test_suite() does not wrap compile definitions.
target_compile_definitions(chronon3d_sdk_tests PRIVATE
    CHRONON3D_SDK_ARCHIVE_PATH=${CMAKE_BINARY_DIR}/src/libchronon3d_sdk_impl.a
)
