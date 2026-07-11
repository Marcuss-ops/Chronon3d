# ─── M1.8 §1E TICKET-SIMPLICITY-VISIBILITY-CONTRACT tests (chronon3d_visibility_contract_tests) ───
#
# Adds the §1E visibility contract test target. This test target covers:
#   1. TextVisibilityAudit status cascade (PASS/FAIL/INDETERMINATE)
#   2. 4 critical invariants (font_resolved, shaping_succeeded, finite, predicted_contains_world)
#   3. §9 FU04 violation response (should_disable_tile_pruning + expanded_predicted_bbox)
#   4. Edge cases (empty shape, NaN, infinite)
#
# Per ADR-018, this .cmake file is also listed in the parent's
# CMAKE_CONFIGURE_DEPENDS so Ninja / Make re-run cmake when this file
# is touched.
#
# IMPORTANT: this test target is CONDITIONAL on CHRONON3D_BUILD_DIAGNOSTICS
# because the test file calls `audit_text_visibility()` (FU02/FU04 canonical
# function, gated by `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`).  In
# non-diagnostic builds the function declaration is missing, so the test
# cannot compile.  The .cpp file self-guards with a SUCCEED no-op TEST_CASE
# in non-diagnostic builds, but the audit call sites in the active TEST_CASEs
# require the gated function.  Forward-point: a future FU04 pass can promote
# the audit function to a non-gated API.

if(NOT CHRONON3D_BUILD_DIAGNOSTICS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_visibility_contract_tests
    TIER UNIT
    LINK_TARGETS chronon3d_text_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_visibility_contract.cpp
)

# Wire into the FAST test aggregator (math + harness, no rendering
# backend required). Mirrors the pattern used for chronon3d_text_definition_tests
# + chronon3d_safe_area_placement_tests + chronon3d_text_builder_ergonomics_tests
# + chronon3d_animation_helpers_tests + chronon3d_text_simplicity_adapters_tests.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_visibility_contract_tests)
