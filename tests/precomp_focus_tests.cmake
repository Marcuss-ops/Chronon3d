# ── PrecompNode focus tests (WP-0 PR 0.2) ────────────────────────────
# Compile-coherence target that links ONLY the precomp-node TU family
# + its direct test TU, without pulling in the full renderer-test bag.
# Surfaces regressions in precomp_node_execute.cpp / precomp_node.hpp /
# test_precomp_node_cache.cpp early (cheap, fast to build).
#
# Public executor contract: this target links `chronon3d_sdk` (the
# only-public alias), NOT a per-subsystem OBJECT target, so any
# internal refactor that breaks the public surface fails this test
# at link time before merging.

if(CHRONON3D_USE_BLEND2D)
chronon3d_add_test_suite(
    NAME chronon3d_precomp_focus_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_backend_software chronon3d::content
    SOURCES render_graph/nodes/test_precomp_node_cache.cpp
)
target_compile_definitions(chronon3d_precomp_focus_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# Note: integration into chronon3d_tests_fast is wired from
# tests/CMakeLists.txt after `set(CHRONON3D_FAST_TEST_DEPS ...)`.
# This `.cmake` only declares the target + CTest registration;
# tests/CMakeLists.txt adds it to the FAST aggregate custom target.
endif()
