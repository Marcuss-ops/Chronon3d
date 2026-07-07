# ── Cache Tests ──

# Video-dependent test sources (require CHRONON3D_ENABLE_VIDEO)
set(CACHE_VIDEO_TESTS "")
if(CHRONON3D_ENABLE_VIDEO)
    list(APPEND CACHE_VIDEO_TESTS
        cache/test_hash_builder.cpp
    )
endif()

add_executable(chronon3d_cache_tests
    ${TEST_MAIN}
    cache/test_cache_diagnostics.cpp
    ${CACHE_VIDEO_TESTS}
    cache/test_cache_policy.cpp
    cache/test_persistent_framebuffer_store.cpp
    cache/test_lru_cache.cpp
    cache/test_lru_extensions.cpp
    cache/test_frame_cache.cpp
    # Fix #3 (post-FASE-18 review) -- standalone test for FASE 18's
    # extracted evict_lru_for helper trio (src/cache/framebuffer_pool_evict.cpp).
    # Exercises the LRU eviction driver in isolation via the public
    # release() / stats() / set_budget_bytes() surface. Deterministic,
    # cat-2 freeze-compliant (no threads / no time / no PRNG).
    # Also registered in tests/core_tests.cmake (cache subgroup) for
    # blanket coverage alongside test_framebuffer_pool.cpp.
    cache/test_evict_lru_for.cpp
    cache/test_video_frame_cache.cpp
    cache/test_framebuffer_pool.cpp
    # TICKET-ae-cam-hash-collision Soluzione B — camera-aware NodeCacheKey
    # regression lock. Asserts `camera_fingerprint_digest` and
    # `fold_camera_into_params_hash` differentiate per-camera-state so the
    # AE_CAM_02/04 framebuffer-hash-collision (downstream cache surface)
    # cannot regress to pre-Soluzione-B state. See
    # `tests/cache/test_node_cache_hash_includes_camera.cpp`.
    cache/test_node_cache_hash_includes_camera.cpp
    # TICKET-ae-cam-hash-collision / Soluzione B - opt-in regression lock
    # + first captured-in-a-test activation of the
    # `node_cache_hash_collisions` counter (declared via X-macro at
    # `include/chronon3d/core/profiling/render_counter_macros.hpp:35`).
    # Asserts 3 scenes x 3 frames = 9 distinct NodeCacheKey per AE_CAM
    # sweep scenarios (zoom-only AE_CAM_02 / Z-dolly AE_CAM_04 /
    # parent-name axis), with the TLS counter held at 0 post-probe.
    cache/test_node_cache_ae_sweep.cpp
    render_graph/cache/test_scene_program_cache.cpp
    render_graph/cache/test_compiled_graph_cache.cpp
)
target_link_libraries(chronon3d_cache_tests PRIVATE chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_graph_cache doctest::doctest)
target_include_directories(chronon3d_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_cache_tests COMMAND chronon3d_cache_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
