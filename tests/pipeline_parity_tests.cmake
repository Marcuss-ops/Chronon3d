# ─── M1.8 §4A TICKET-SIMPLICITY-PIPELINE-PARITY tests (chronon3d_pipeline_parity_tests) ───
#
# Adds the §4A pipeline parity test target. This test target covers:
#   1. 7 pipeline variations (chronon3d_cli render / video / inspect-text /
#      warmup on/off / tile_pruning on/off / serial/parallel / Debug/Release)
#   2. 6 invariant fields per pipeline (glyph_count, layout_bbox, world_bbox,
#      predicted_bbox, alpha_bbox, hash)
#   3. 3 properties per field (sanity + identity-vs-base + determinism)
#   4. 18 CHECK assertions per pipeline × 7 pipelines = 126 CHECKs total
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
    NAME chronon3d_pipeline_parity_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_text_core chronon3d_scene chronon3d_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_pipeline_parity.cpp
)

# Wire into the FAST test aggregator (math + harness, no rendering
# backend required). Mirrors the pattern used for chronon3d_text_definition_tests
# + chronon3d_safe_area_placement_tests + chronon3d_text_builder_ergonomics_tests
# + chronon3d_text_simplicity_adapters_tests + chronon3d_animation_helpers_tests.
# Gated by CHRONON3D_BUILD_DIAGNOSTICS via the early return above; only
# append the target to the FAST aggregator when the executable exists.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_pipeline_parity_tests)
