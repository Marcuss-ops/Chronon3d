# tests/text_presets_stability_tests.cmake
# ─────────────────────────────────────────────────────────────────────
# TICKET-SIMPLICITY-PRESETS §3C — Pure struct inspection lock for the
# 5 reusable TextDefinition-producing presets:
#   1. title_centered()
#   2. subtitle_bottom()
#   3. caption_safe_area()
#   4. kinetic_word()
#   5. lower_third()
#
# Highlights:
#   - Pure struct inspection (no rendering / no Blend2D / no graphics
#     backend dependency).  The 5 preset functions live in
#     `include/chronon3d/presets/text/text_presets_v1.hpp` (header-only
#     `inline` free functions in the `chronon3d::presets::text`
#     namespace).  Each returns a fully-populated `TextDefinition` DTO
#     (F2.A canonical authoring DTO).
#   - 5 TEST_CASEs × 3 CHECKs = 15 total assertions (matches the §3C
#     spec "5×3=15 assertion").
#   - Existing build dependency on `chronon3d_text_core` (covers
#     `text_definition.cpp` + `text_document_builder.cpp` per
#     `src/text/CMakeLists.txt`).
#
# Registration helper: chronon3d_register_test_source() (cmake/
# Chronon3DTestSuite.cmake) so the §12 Python gate
# (tools/check_test_source_registration.py) tracks this file under
# the canonical test-source registry.
#
# Per ADR-018, this file is also listed in CMAKE_CONFIGURE_DEPENDS at
# the top of `tests/CMakeLists.txt`.
# ─────────────────────────────────────────────────────────────────────

chronon3d_add_test_suite(
    NAME chronon3d_text_presets_stability_tests
    TIER UNIT
    LINK_TARGETS chronon3d_text_core chronon3d_backend_text
    SOURCES text/test_presets_stability.cpp
)
