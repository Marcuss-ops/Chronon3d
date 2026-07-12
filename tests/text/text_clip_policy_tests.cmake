# ─── M1.8 §4A-ext / TICKET-SIMPLICITY-TEXT-CLIP-POLICY tests (chronon3d_text_clip_policy_tests) ───
#
# Extracted from `tests/pipeline_parity_tests.cmake` per the §2 cleanup
# mandate (TICKET-SIMPLICITY-PARITY-EXTRACT).  Carries the 5-clip-rect
# matrix: 7 pipeline variations × 5 `ClipVariant` (Baseline / Expanded /
# Conservative / Full / Off) × 18 CHECKs = 630 CHECKs total.
#
# Per ADR-018, this .cmake file is also listed in the parent's
# CMAKE_CONFIGURE_DEPENDS so Ninja / Make re-run cmake when this file
# is touched.
#
# IMPORTANT: this test target is CONDITIONAL on CHRONON3D_BUILD_DIAGNOSTICS
# because the test file uses `audit_text_visibility()` (FU02/FU03 canonical
# function, gated by CHRONON3D_BUILD_DIAGNOSTICS in the .cpp file) to
# extract the 6 invariant fields.  In non-diagnostic builds the function
# declaration is missing, so the test cannot compile.  Forward-point: a
# future FU04 pass can promote the field-extraction to a non-gated API
# (e.g., a thin wrapper that reads from the framebuffer + TextRunShape
# directly), at which point the gate can be dropped.

if(NOT CHRONON3D_BUILD_DIAGNOSTICS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_text_clip_policy_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_pipeline chronon3d_backend_software chronon3d_scene chronon3d_text_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_clip_policy.cpp
)

# Wire into the FAST test aggregator (math + harness, no rendering
# backend required). Mirrors the pattern used for the deleted
# chronon3d_pipeline_parity_tests target + chronon3d_text_definition_tests
# + chronon3d_safe_area_placement_tests + chronon3d_text_builder_ergonomics_tests
# + chronon3d_text_simplicity_adapters_tests + chronon3d_animation_helpers_tests.
# Gated by CHRONON3D_BUILD_DIAGNOSTICS via the early return above; only
# append the target to the FAST aggregator when the executable exists.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_text_clip_policy_tests)

doctest_discover_tests(chronon3d_text_clip_policy_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTIES
        TIMEOUT 30
)
