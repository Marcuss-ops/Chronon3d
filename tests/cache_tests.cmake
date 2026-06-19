# ── Cache Tests ──

add_executable(chronon3d_cache_tests
    ${TEST_MAIN}
    cache/test_cache_diagnostics.cpp
    cache/test_hash_builder.cpp
    cache/test_cache_policy.cpp
    cache/test_persistent_framebuffer_store.cpp
    cache/test_lru_cache.cpp
    cache/test_lru_extensions.cpp
    cache/test_frame_cache.cpp
    cache/test_video_frame_cache.cpp
    cache/test_framebuffer_pool.cpp
    render_graph/cache/test_scene_program_cache.cpp
    render_graph/cache/test_compiled_graph_cache.cpp
)
target_link_libraries(chronon3d_cache_tests PRIVATE chronon3d_pipeline chronon3d_graph_cache doctest::doctest)
target_include_directories(chronon3d_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_cache_tests COMMAND chronon3d_cache_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
