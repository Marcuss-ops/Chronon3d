# ── Optimizer Tests ──
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_optimizer_tests
    TIER UNIT
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
    SOURCES render_graph/optimizer/test_graph_optimizer.cpp
)
