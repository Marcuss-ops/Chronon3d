# ── Optimizer Tests ──

add_executable(chronon3d_optimizer_tests
    ${TEST_MAIN}
    render_graph/optimizer/test_graph_optimizer.cpp
)
target_link_libraries(chronon3d_optimizer_tests PRIVATE chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline doctest::doctest)
target_include_directories(chronon3d_optimizer_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_optimizer_tests COMMAND chronon3d_optimizer_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
