# ═══════════════════════════════════════════════════════════════════════════
# tests/golden_matrix_subtitle_tests.cmake — TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-1
#
# Golden matrix sweep harness for 4 baseline Subtitle-category presets
# (minimal_white, yellow_keyword, glow_pulse, caption_box).  Per-cell
# golden PNG emission + 11-metric ScenarioMetrics capture.
#
# Matrix dimensions (per preset):
#   * aspect ratio (16:9 / 9:16)            — 2 cells
#   * text length  (short / long)           — 2 cells
#   * background    (light / dark)          — 2 cells
#   * scale         (normal / extreme)      — 2 cells
#   * cache state   (warm / cold)           — 2 cells
#   * scheduler     (serial / parallel)     — 2 cells
#   * timestamp     (initial / mid / end)   — 3 cells
# Total: 192 cells per preset × 4 presets = 768 cells.
#
# FAST_MODE env gate: CHRONON3D_GOLDEN_MATRIX_FAST_MODE=1 reduces to
# 6 cells per preset (2 AR × 3 ts) for local dev / quick smoke.  CI
# runs the full matrix.
#
# Golden PNGs: test_renders/golden/matrix/<preset>_<dim-tag>_<ts>.png
# Manifest:    test_renders/golden/matrix/<preset>.matrix.jsonl
#
# Update goldens: CHRONON3D_UPDATE_GOLDENS=1 ctest -R GoldenMatrixSubtitle
# ═══════════════════════════════════════════════════════════════════════════

# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_golden_matrix_subtitle_tests
    TIER INTEGRATION
    # Custom link contract: requires chronon3d_content for
    # `content/text/text_helpers.hpp::centered_text` helper + the
    # SDK face (chronon3d_sdk INTERFACE) for composition + RenderSettings.
    # Default INTEGRATION contract (chronon3d_pipeline + chronon3d_backend_software)
    # is insufficient — override is INTENTIONAL and documented.
    LINK_TARGETS chronon3d_sdk chronon3d_software chronon3d_pipeline
                  chronon3d_backend_software chronon3d_content
                  chronon3d_text_core
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text_golden/matrix/test_golden_matrix_subtitle.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/visual/support/golden_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/visual/support/image_diff.cpp
)
target_compile_definitions(chronon3d_golden_matrix_subtitle_tests PRIVATE
    CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
