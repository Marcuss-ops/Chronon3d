# tests/reference/capcut/CMakeLists.txt — CapCut-grade parity test.
#
# TICKET-CAPCUT-REFERENCE-CORPUS (FU09 of verdict CapCut-grade §Fase 9).
# Compares Chronon3D-rendered output against blessed CapCut PNG references.
#
# TIER=INTEGRATION (matches tests/text_golden/ precedent: visual tests need
# Blend2D + SoftwareRenderer). LINK_TARGETS override to add chronon3d_visual_test_support
# for image_diff.hpp::compute_ssim reuse (Cat-3 anti-dup: don't duplicate SSIM impl).
#
# Skeleton phase: blessed reference corpus is empty. All TEST_CASEs skip
# gracefully via MESSAGE+CHECK(true)+return pattern until PR-review-seeded.
#
# Per AGENTS.md ADR-018: this file is included via tests/manifests/test_definitions.cmake
# (NOT via add_subdirectory) because tests/reference/capcut/ is a leaf test area,
# not a sub-project with separate build target.

# LINK_TARGETS override: chronon3d_visual_test_support added beyond the default
# INTEGRATION contract (chronon3d_pipeline + chronon3d_backend_software) to
# enable reuse of chronon3d::test::compute_ssim from tests/visual/support/image_diff.hpp
# (Cat-3 anti-dup: don't duplicate SSIM implementation in this test).
#
# The override emits AUTHOR_WARNING per cmake/Chronon3DTestSuite.cmake:91 — this
# is INTENTIONAL. If a future maintainer removes chronon3d_visual_test_support to
# "fix" the warning, the test will break silently at link time (undefined reference
# to compute_ssim). The reverse-direction Cat-3 fix would be to inline SSIM here,
# but the cost (duplicating ~80 LoC of SSIM math) exceeds the warning-suppression cost.
chronon3d_add_test_suite(
    NAME chronon3d_capcut_parity_tests
    TIER INTEGRATION
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_capcut_parity.cpp
    LINK_TARGETS
        chronon3d_pipeline
        chronon3d_backend_software
        chronon3d_visual_test_support
)
