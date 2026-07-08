# ── Gradient Visual Golden Tests ──────────────────────────────────────────
# Golden image regression tests for gradient fills and the GradientDefinition
# sampler.  Uses the shared chronon3d_visual_test_support framework.

chronon3d_add_test_suite(
    NAME chronon3d_gradient_visual_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_visual_test_support chronon3d_backend_software chronon3d_backend_image chronon3d_scene
    SOURCES visual/gradient_visual_tests.cpp
)
target_compile_definitions(chronon3d_gradient_visual_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
