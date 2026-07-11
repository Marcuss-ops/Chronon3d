# tests/diagnostic_overlay_tests.cmake
# ─────────────────────────────────────────────────────────────────────
# TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY §4C — Diagnostic overlay PNG
# regression for the 5 clip variants.  Each test renders the canary
# composition with one clip_rect variant, draws the 6 overlay elements
# (box / baseline / ink-bbox / predicted-bbox / clip-rect / anchor)
# on top of the rendered frame, and verifies the result against the
# corresponding PNG golden.
#
# Highlights:
#   - 5 PNG goldens in `test_renders/golden/text/diagnostic_overlay/`:
#       1. clip_06_baseline.png      (full canvas 1920×1080)
#       2. clip_06_expanded.png      (full canvas + 100px FU04 violation response)
#       3. clip_06_conservative.png  (5% safe-area inset)
#       4. clip_06_full.png          (way over-sized)
#       5. clip_06_off.png           (zero rect, clip disabled)
#   - Blend2D-gated (the test renders via the SoftwareRenderer which
#     requires CHRONON3D_USE_BLEND2D + CHRONON3D_ENABLE_TEXT).  Listed
#     inside the Blend2D+Text conditional in tests/CMakeLists.txt.
#   - TIER=INTEGRATION (per ADR-018 convention: visual regression
#     tests are INTEGRATION tier).
#   - Re-bake: CHRONON3D_UPDATE_GOLDENS=1 ctest -R
#     chronon3d_diagnostic_overlay_tests  (deferred to working build
#     host per AGENTS.md §honesty; the test gracefully skips on
#     missing golden via the `result.golden_missing` short-circuit).
#
# Registration helper: chronon3d_register_test_source() (cmake/
# Chronon3DTestSuite.cmake) so the §12 Python gate
# (tools/check_test_source_registration.py) tracks this file under
# the canonical test-source registry.
# ─────────────────────────────────────────────────────────────────────

if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    chronon3d_add_test_suite(
        NAME chronon3d_diagnostic_overlay_tests
        TIER INTEGRATION
        LINK_TARGETS chronon3d_sdk chronon3d_software chronon3d_content
                      chronon3d_runtime chronon3d_text_core
        SOURCES text_golden/diagnostic_overlay/test_diagnostic_overlay.cpp
                visual/support/golden_test.cpp
                visual/support/image_diff.cpp
    )
    target_compile_definitions(chronon3d_diagnostic_overlay_tests
        PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    add_test(
        NAME DiagnosticOverlay
        COMMAND chronon3d_diagnostic_overlay_tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    # §12.1 test source registration (canonical).
    if(COMMAND chronon3d_register_test_source)
        chronon3d_register_test_source("tests/text_golden/diagnostic_overlay/test_diagnostic_overlay.cpp")
    endif()
endif()
