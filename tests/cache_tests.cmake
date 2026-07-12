# ── Cache Tests ──

chronon3d_add_test_suite(
    NAME chronon3d_cache_tests
    TIER UNIT
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_graph_cache
    SOURCES cache/test_cache_diagnostics.cpp
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
            # TICKET-lock-free-shared_mutex — 16-thread concurrency stress
            # tests validating that shared_mutex migration is safe under
            # heavy concurrent register + read + clear (no UB / deadlock
            # / torn-read). Tagged [stress] so CI runtimes stay bounded.
            cache/stress/test_cache_diagnostics_stress.cpp
            cache/stress/test_camera_transition_catalog_stress.cpp
            render_graph/cache/test_compiled_graph_cache.cpp
            # In-process cache reuse: same renderer, same frame, 5 iterations.
            # Verifies cache hits increase, no hash collisions, pixel consistency.
            cache/test_cache_reuse_identical_frame.cpp
            # Cache invariance: cold/warm identical hash, 8 input dimensions
            # produce distinct keys (text/font/glow/tracking/placement/
            # transform/animation/frame), and A/B/A job-state restoration.
            # Direct on main per user spec.
            cache/test_cache_invariance.cpp
)
# Video-dependent test sources (require CHRONON3D_ENABLE_VIDEO)
if(CHRONON3D_ENABLE_VIDEO)
    target_sources(chronon3d_cache_tests PRIVATE cache/test_hash_builder.cpp)
endif()
