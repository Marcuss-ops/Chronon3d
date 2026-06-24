# ═══════════════════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════════════════
# tests/text_preset_visual_tests.cmake — PR-A4 (Blocco A, Fase 1 followup)
#
# Test executable registration for the 16 text-style presets' visual
# regression validation harness (TICKET-038 / TXT-00).
#
# STATUS (2026-06-24, Blocco 2 TXT-00 closure): build + link + run: rc=0 ✅.
#   ctest VRTextPresetVisual: 18/18 doctest test cases, 263/263 assertions passed.
#   All 128 (preset, ratio, frame) sentinels match the expected_visible matrix:
#     * 14 entrance-animation presets have ink_pixels == 0 at Frame 0 (truly
#       transparent by design — fade_in / soft_pop / fade_shift_* clamp
#       opacity to 0).
#     * BlurIn F020 (169 + 916) is sub-threshold mid-animation
#       (focus_in(Frame{30}) at frame 20 → blur ≈ 1.08 dilutes alpha < 0.05).
#     * MaskedLineReveal F020 (169 + 916) is sub-threshold mid-animation
#       (center_split scale_y ≈ 0.66 + fade_shift opacity ≈ 0.94 mixed
#       below threshold).
#     * tracking_close + minimal_white visible at ALL timestamps (no
#       entrance opacity animation).
#   Both TextE2E tests pass: render_frame with text ink_pixels=1372;
#   materialize + draw_text_run items_drawn > 0, ink_pixels=1372.
# See: docs/NEXT_STEPS.md (Blocco 2 — TXT-00 closure).
#
# Canonical linker surface:
#   - chronon3d_sdk_impl  : STATIC archive aggregating ALL registry OBJECTs.
#   - chronon3d_sdk       : INTERFACE aggregate
#   - chronon3d_software  : INTERFACE umbrella
#
# Single-file test:
#   tests/text/test_text_preset_visual.cpp (~510 LOC, 128 sentinels)
#
# PNG dump pipeline is wired in follow-up sub-PR A4.png (no-op here).
# ═══════════════════════════════════════════════════════════════════════════

# TICKET-029: align to canonical Chronon3D test pattern (doctest::doctest
# + shared tests/test_main.cpp). The vcpkg `doctest` port exports only
# `doctest::doctest`, not `doctest_with_main` — previous link broke the
# cmake configure step before any TU compiled.
add_executable(chronon3d_text_preset_visual_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_preset_visual.cpp
    ${TEST_MAIN}
)

target_include_directories(chronon3d_text_preset_visual_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(chronon3d_text_preset_visual_tests PRIVATE
    chronon3d_sdk_impl
    chronon3d_sdk
    chronon3d_software
    doctest::doctest
)

add_test(
    NAME VRTextPresetVisual
    COMMAND chronon3d_text_preset_visual_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
