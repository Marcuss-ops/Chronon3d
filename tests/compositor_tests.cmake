# ── Compositor Regression Tests ──
# Small, fast unit tests for the software compositor.
# Kept separate from chronon3d_renderer_tests to avoid linker OOM.

chronon3d_add_test_suite(
    NAME chronon3d_compositor_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_backend_software chronon3d_scene chronon3d::content
    SOURCES render_graph/pipeline/test_composite_origin_regression.cpp
            compositor/test_blend_reference.cpp
            compositor/test_blend_simd_equivalence.cpp
            compositor/test_track_matte.cpp
)
target_compile_definitions(chronon3d_compositor_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
