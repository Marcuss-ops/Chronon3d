# ═══════════════════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════════════════
# tests/text_preset_visual_tests.cmake — PR-A4 (Blocco A, Fase 1 followup)
#
# Test executable registration for the 16 text-style presets' visual
# regression validation harness (TICKET-038 / TXT-00).
#
# STATUS (2026-06-23): TXT-00 NOT GREEN.  F-C closure attempt BLOCKED.
# The build fails with ~30 unresolved symbols after 7 iterations across
# 4 strategies (granular lib-additive, D3 structural fix, Option C with
# chronon3d_sdk_impl static archive).  See:
#   docs/baselines/tcb-txt-00-f-c-blocked-4ab8cbb8.md
# for the full iteration history and root-cause analysis.
#
# Next recommended step: F-D — wrap chronon3d_sdk_impl in
# -Wl,--whole-archive to force all .o files into the link line.
#
# Canonical linker surface (blocked state — Option C residual):
#   - chronon3d_sdk_impl  : STATIC archive aggregating ALL registry OBJECTs.
#     Option C added it as FIRST PRIVATE dep to resolve the INTERFACE
#     propagation gap.  Failed: link-order / symbol-drop hypothesis.
#   - chronon3d_sdk       : INTERFACE aggregate (via target_sources
#     $<TARGET_OBJECTS:...>).  Historically insufficient for the test
#     executable — OBJECT .o files dropped at link stage.
#   - chronon3d_software  : INTERFACE umbrella (backend_software +
#     backend_assets + backend_image).  SoftwareRenderer surface.
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
)
