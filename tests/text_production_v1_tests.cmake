# ═══════════════════════════════════════════════════════════════════════════
# tests/text_production_v1_tests.cmake
#
# Text Production V1 — anti-false-green certification test target.
#
# Materialises the 20-TEST_CASE file `tests/certification/
# test_text_production_v1.cpp` as the canonical chronon3d_text_production_v1_tests
# ctest target.  Per the user spec, the suite covers:
#   1.  Anti-false-green core (frame.result + glyph_count>0 +
#       missing_glyph_count==0 + ink_bounds non-empty +
#       visible_ink_pixels>100)
#   2-3. Negative: missing font + corrupt font
#   4.  Blank text → 0 glyphs
#   5-6. UTF-8 + font fallback
#   7-8. Wrapping + auto-fit
#   9-10. Alignment (X) + placement (Y)
#   11-13. Glow + stroke + shadow
#   14. Typewriter (canonical)
#   15. Long text (200 chars) wrapping
#   16-17. 16:9 + 9:16 aspect ratios
#   18. Animation frame-by-frame
#   19. Clipping
#   20. Alpha threshold > 100 (canonical)
#
# Registered as INTEGRATION tier (depends on the production Chronon3D
# renderer + composition pipeline).  Linked against the same targets
# as `chronon3d_text_golden_tests` (chronon3d_sdk + chronon3d_software
# + chronon3d_content + chronon3d_runtime + chronon3d_text_core).
#
# macchina-verifica on a working build host: `ctest --test-dir
# build/chronon/linux-content-dev -R chronon3d_text_production_v1_tests --output-on-failure`
# expects 20/20 PASS (anti-false-green + 19 user-spec scenarios).
# ═══════════════════════════════════════════════════════════════════════════

chronon3d_add_test_suite(
    NAME chronon3d_text_production_v1_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_software chronon3d_content
                  chronon3d_runtime chronon3d_text_core
    SOURCES certification/test_text_production_v1.cpp
)
target_compile_definitions(chronon3d_text_production_v1_tests PRIVATE
    CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Apply the chronon3d_text_full_acceptance label so the umbrella target
# picks this target up alongside the 6 existing text_*_tests targets.
set_tests_properties(chronon3d_text_production_v1_tests PROPERTIES
    LABELS "text-full-acceptance"
)
