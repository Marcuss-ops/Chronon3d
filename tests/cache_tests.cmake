# ── Cache Tests ──

add_executable(chronon3d_cache_tests
    ${TEST_MAIN}
    cache/test_persistent_bake_cache.cpp
)
target_link_libraries(chronon3d_cache_tests PRIVATE chronon3d_pipeline doctest::doctest)
target_include_directories(chronon3d_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_cache_tests COMMAND chronon3d_cache_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
