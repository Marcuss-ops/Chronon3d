# ── Cache Tests ──

add_executable(chronon3d_cache_tests
    ${TEST_MAIN}
    cache/test_persistent_bake_cache.cpp
    cache/test_lru_cache.cpp
    cache/test_framebuffer_pool.cpp
    render_graph/cache/test_scene_program_cache.cpp
)
target_link_libraries(chronon3d_cache_tests PRIVATE chronon3d_pipeline chronon3d_graph_cache doctest::doctest)
target_include_directories(chronon3d_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_cache_tests COMMAND chronon3d_cache_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
