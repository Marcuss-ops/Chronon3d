# tests/text/CMakeLists.txt — Standalone test bundle for isolated alignment + auto-fit
#
# TICKET-ISOLATED-ALIGNMENT-TESTS — Fase 7 of CapCut-grade verdict.
#
# These are SEPARATE test binaries (not compiled into chronon3d_core_tests)
# so that targeted CTest filters work as designed:
#   ctest -R chronon3d_text_alignment_isolated_tests
#   ctest -R chronon3d_text_auto_fit_tests
#
# Pattern mirrors tests/reference/capcut/CMakeLists.txt (CapCut corpus,
# previous chore bbe2d28a) which also uses chronon3d_add_test_suite
# directly — both files are wired into tests/manifests/test_definitions.cmake.
#
# NO raw add_executable calls — per AGENTS.md §test_suite_registration
# gate, all test targets MUST use chronon3d_add_test_suite(NAME TIER
# SOURCES LINK_TARGETS LABELS).

if(CHRONON3D_BUILD_TESTS)

# ──────────────────────────────────────────────────────────────────────────
# 1. Alignment isolation (Pattern B — full render + alpha_bbox)
#    3 TEST_CASE: Left/Center/Right alignment with SAME box/pos/anchor;
#    only TextAlign varies.  Regression lock for when alignment engine
#    applies TextAlign to single-line text (currently EXPECT_FAIL).
# ──────────────────────────────────────────────────────────────────────────
chronon3d_add_test_suite(
    NAME  chronon3d_text_alignment_isolated_tests
    TIER  INTEGRATION
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_text_alignment_isolated.cpp
    LINK_TARGETS
        chronon3d_pipeline
        chronon3d_backend_software
        chronon3d_visual_test_support
)

# ──────────────────────────────────────────────────────────────────────────
# 2. Auto-fit isolation (Pattern A — LocalEngine + materialize_text_run_shape)
#    3 TEST_CASE: cache on/off determinism, impossible min_font_size,
#    5-run determinism.  Pre-raster layout lock (no full render needed).
# ──────────────────────────────────────────────────────────────────────────
chronon3d_add_test_suite(
    NAME  chronon3d_text_auto_fit_tests
    TIER  UNIT
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_text_auto_fit.cpp
    LINK_TARGETS
        chronon3d_pipeline
)

# ──────────────────────────────────────────────────────────────────────────
# 3. Unicode line breaking (UAX#14-style, no ICU)
#    CJK / Thai / Devanagari / Arabic / Hebrew no-space wrapping,
#    mixed-direction runs, max_lines and grapheme-safe ellipsis hint.
# ──────────────────────────────────────────────────────────────────────────
chronon3d_add_test_suite(
    NAME  chronon3d_text_unicode_line_breaking_tests
    TIER  UNIT
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_unicode_line_breaking.cpp
    LINK_TARGETS
        chronon3d_text_core
        chronon3d_pipeline
        chronon3d_backend_software
)

endif()  # CHRONON3D_BUILD_TESTS