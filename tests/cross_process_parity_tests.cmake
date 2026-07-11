# ─── M1.8 §4A TICKET-SIMPLICITY-CROSS-PROCESS-PARITY tests (chronon3d_cross_process_parity_tests) ───
#
# ═══════════════════════════════════════════════════════════════════════════
# DRAFT — NOT YET REGISTERED.  See FOLLOWUP_TICKETS.md for the 6 rot
# surfaces that block this test target from compiling/linking.
# ═══════════════════════════════════════════════════════════════════════════
#
# This test target covers:
#   1. 5 pipeline entry points (SDK still / CLI still / video raw / render
#      graph / direct pipeline) for the same text (cert_title canary).
#   2. 6 invariant fields per pipeline (glyph_count, layout_bbox, world_bbox,
#      predicted_bbox, alpha_bbox, hash).
#   3. 3 properties per field (sanity + identity-vs-base + cross-path SSIM).
#   4. Post-H.264 transport invariants: SSIM >= 0.98, mean err <= 3/255.
#
# Rot surfaces (see FOLLOWUP_TICKETS.md TICKET-PARITY-001..006):
#   1. Rect API rot — `Rect{4 floats}` is broken; need `Rect{Vec2{...}, Vec2{...}}`
#   2. Link target rot — `test::make_renderer_shared()` needs SoftwareBackend
#      + NodeCache + simd link targets not currently in this cmake
#   3. DIAGNOSTICS gate rot — the existing `pipeline_parity_tests.cmake` is
#      gated on `CHRONON3D_BUILD_DIAGNOSTICS=OFF`, hiding rot from CI
#   4. NativeAvEncoder rot — public `include/chronon3d/video/native_encoder.hpp`
#      includes a missing `encoder.hpp`; actual `IVideoEncoder` is in CLI utils
#   5. CLI inspect-text JSON rot — JSON doesn't surface the 4 structural fields
#   6. doctest SUCCEED rot — SUCCEED doesn't accept `<<` stream; use MESSAGE
#
# To re-enable: remove the `return()` below AFTER fixing rot #1 + #2.
# The .cpp file at `tests/text/test_cross_process_parity.cpp` is the
# design document + skeleton (400+ LoC) — preserve as-is for future
# implementation.
#
# Per ADR-018, this .cmake file is also listed in the parent's
# CMAKE_CONFIGURE_DEPENDS so Ninja / Make re-run cmake when this file
# is touched.
return()  # DRAFT: see TICKET-PARITY-001..006 in docs/FOLLOWUP_TICKETS.md

chronon3d_add_test_suite(
    NAME chronon3d_cross_process_parity_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_text_core chronon3d_scene chronon3d_core chronon3d_content
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_cross_process_parity.cpp
)

# Inject the path to the chronon3d_cli binary so the test can subprocess it.
# The compile definition is read at runtime; CHRONON3D_CLI_EXE_PATH env var
# overrides the default.
target_compile_definitions(chronon3d_cross_process_parity_tests PRIVATE
    CHRONON3D_CLI_EXE_PATH="$<TARGET_FILE:chronon3d_cli>"
)

# Wire into the FAST test aggregator (math + harness, no rendering
# backend required). Mirrors the pattern used for chronon3d_pipeline_parity_tests.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_cross_process_parity_tests)
