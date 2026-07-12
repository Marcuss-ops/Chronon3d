# ─── M1.8 §4A-ext TICKET-SIMPLICITY-TEXT-CLIP-POLICY tests (chronon3d_text_clip_policy_tests) ───
#
# Adds the §4A-ext clip-policy matrix test target. This test target covers:
#   1. 5 clip variants (Baseline / Expanded / Conservative / Full / Off)
#   2. 7 pipeline variations per clip (chronon3d_cli render / video / inspect-text /
#      warmup on/off / tile_pruning on/off / serial/parallel / Debug/Release)
#   3. 18 CHECK assertions per pipeline × clip combination (sanity + identity-vs-base
#      + determinism)
#   Total: 7 pipelines × 5 clips × 18 CHECKs = 630 CHECKs.
#
# Source: the test materialises a canary composition inline (no file I/O),
# renders it at frame 0 (or 5 frames for the video pipeline), then audits the
# visibility contract via the gated `audit_text_visibility()` helper to extract
# 6 invariant fields (glyph_count, layout_bbox, world_bbox, predicted_bbox,
# alpha_bbox, hash).
#
# Per ADR-018, this .cmake file is also listed in the parent's
# `CMAKE_CONFIGURE_DEPENDS` so Ninja / Make re-run cmake when this file
# is touched.
#
# IMPORTANT: this test target is CONDITIONAL on `CHRONON3D_BUILD_DIAGNOSTICS`
# because `audit_text_visibility()` (the FU02/FU03 canonical function, gated
# by `CHRONON3D_BUILD_DIAGNOSTICS` in `src/text/text_visibility_audit.hpp`)
# is required to extract the 6 invariant fields for the 18 CHECK macro
# `assert_pipeline_clip_18_checks`. In non-diagnostic builds the function
# declaration is missing, so the test cannot compile. The early-return
# below guards both the target creation AND the FAST aggregator append.
#
# Forward-point: a future FU04 pass can promote the field-extraction to a
# non-gated API (e.g., a thin wrapper that reads from the framebuffer +
# TextRunShape directly), at which point the gate can be dropped.

if(NOT CHRONON3D_BUILD_DIAGNOSTICS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_text_clip_policy_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_pipeline chronon3d_backend_software chronon3d_scene chronon3d_text_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_text_clip_policy.cpp
)

# Wire into the FAST test aggregator (math + harness, no rendering
# backend required for the base identity checks). Mirrors the pattern used
# for chronon3d_text_definition_tests + chronon3d_safe_area_placement_tests
# + chronon3d_text_builder_ergonomics_tests + chronon3d_text_simplicity_adapters_tests
# + chronon3d_animation_helpers_tests. Gated by `CHRONON3D_BUILD_DIAGNOSTICS`
# via the early return above; only append the target to the FAST aggregator
# when the executable exists.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_text_clip_policy_tests)

