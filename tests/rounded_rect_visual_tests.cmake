# ── Rounded Rect Visual Golden Tests ──────────────────────────────────────
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()
# Golden image regression tests for rounded_rect shapes with various
# stroke alignments and gradient fills.  Uses the shared
# chronon3d_visual_test_support framework.

chronon3d_add_test_suite(
    NAME chronon3d_rounded_rect_visual_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_visual_test_support chronon3d_backend_software chronon3d_backend_image chronon3d_scene
    SOURCES visual/rounded_rect_visual_tests.cpp
)
target_compile_definitions(chronon3d_rounded_rect_visual_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
