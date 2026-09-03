# Cache test suites.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_cache_tests
    TIER UNIT
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_graph_cache
    SOURCES
        cache/test_cache_diagnostics.cpp
        cache/test_cache_policy.cpp
        cache/test_persistent_framebuffer_store.cpp
        cache/test_lru_cache.cpp
        cache/test_native_video_decoder_lru.cpp
        cache/test_lru_extensions.cpp
        cache/test_frame_cache.cpp
        cache/test_evict_lru_for.cpp
        cache/test_video_frame_cache.cpp
        cache/test_framebuffer_pool.cpp
        cache/test_node_cache_hash_includes_camera.cpp
        cache/test_node_cache_identity_builder.cpp
        cache/test_node_cache.cpp
        cache/test_node_cache_ae_sweep.cpp
        render_graph/cache/test_scene_program_cache.cpp
        cache/stress/test_cache_diagnostics_stress.cpp
        cache/stress/test_camera_transition_catalog_stress.cpp
        render_graph/cache/test_compiled_graph_cache.cpp
        cache/test_cache_reuse_identical_frame.cpp
        cache/test_cache_invariance.cpp
)

if(CHRONON3D_ENABLE_VIDEO)
    target_sources(chronon3d_cache_tests PRIVATE cache/test_hash_builder.cpp)
endif()

chronon3d_add_test_suite(
    NAME chronon3d_parse_framebuffer_pool_clear_policy_tests
    TIER UNIT
    SOURCES cache/test_parse_framebuffer_pool_clear_policy.cpp
)
