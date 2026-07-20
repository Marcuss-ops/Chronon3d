# ═══════════════════════════════════════════════════════════════════════════
# tests/golden_matrix_reveal_tests.cmake — TICKET-GOLDEN-MATRIX-BATCH-3
#
# Golden matrix sweep harness for all 10 Reveal-category descriptors
# (6 BASIC + 4 SELECTORS) using the shared
# `tests/text_golden/matrix/golden_matrix_harness.hpp` header.
#
# BASIC (6):
#   * text_animations
#   * fade_in
#   * blur_in
#   * slide_up
#   * slide_down
#   * scale_in
#
# SELECTORS (4):
#   * tracking_close          (Stage 3, AGENT 2 animated timeline)
#   * masked_line_reveal      (Stage 3, FASE 2b Line selector)
#   * word_cascade            (Stage 3, AGENT 2 Word selector)
#   * character_cascade       (Stage 3, FASE 2b Grapheme selector)
#
# Matrix dimensions (per preset, shared with Batch 1 baseline):
#   * aspect ratio (16:9 / 9:16)            — 2 cells
#   * text length  (short / long)           — 2 cells
#   * scale         (normal / extreme)      — 2 cells
#   * cache state   (warm / cold)           — 2 cells
#   * timestamp     (initial / mid / end)   — 3 cells
# Total: 48 cells per preset × 10 presets = 480 cells.
#
# FAST_MODE env gate: CHRONON3D_GOLDEN_MATRIX_FAST_MODE=1 reduces to
# 6 cells per preset (2 AR × 3 ts) for local dev / quick smoke.  CI
# runs the full matrix.
#
# Golden PNGs: test_renders/golden/matrix/<preset>_<dim-tag>_<ts>.png
# Manifest:    test_renders/golden/matrix/<preset>.matrix.jsonl
#
# Update goldens: CHRONON3D_UPDATE_GOLDENS=1 ctest -R chronon3d_golden_matrix_reveal_tests
# ═══════════════════════════════════════════════════════════════════════════

# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_golden_matrix_reveal_tests
    TIER INTEGRATION
    # Custom link contract mirrors Batch 1 baseline:
    # requires chronon3d_content for `centered_text` helper + the SDK face
    # (chronon3d_sdk INTERFACE) + chronon3d_text_core for shaping/lib.
    LINK_TARGETS chronon3d_sdk chronon3d_software chronon3d_pipeline
                  chronon3d_backend_software chronon3d_content
                  chronon3d_text_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text_golden/matrix/test_golden_matrix_reveal.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/visual/support/golden_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/visual/support/image_diff.cpp
)
target_compile_definitions(chronon3d_golden_matrix_reveal_tests PRIVATE
    CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
