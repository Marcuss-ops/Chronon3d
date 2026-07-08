# ── Optimizer Tests ──

chronon3d_add_test_suite(
    NAME chronon3d_optimizer_tests
    TIER UNIT
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
    SOURCES render_graph/optimizer/test_graph_optimizer.cpp
)
