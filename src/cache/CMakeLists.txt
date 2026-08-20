# ==============================================================================
# src/cache/CMakeLists.txt — chronon3d_cache
# ==============================================================================

add_library(chronon3d_cache OBJECT
    cache_policy.cpp
    frame_cache.cpp
    node_cache.cpp
    framebuffer_pool.cpp
    framebuffer_pool_evict.cpp
    persistent_framebuffer_store.cpp
    persistent_framebuffer_store_config.cpp
    video_frame_cache.cpp
    lru_log.cpp
    cache_diagnostics.cpp
    cache_diagnostics_format.cpp
)
set_target_properties(chronon3d_cache PROPERTIES UNITY_BUILD OFF)
set_source_files_properties(
    persistent_framebuffer_store.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)
target_link_libraries(chronon3d_cache PUBLIC chronon3d xxHash::xxhash)
target_precompile_headers(chronon3d_cache PRIVATE <xxhash.h>)

# All include_private/ headers have been promoted to the public
# include/chronon3d/cache/ surface.  The stale mirrors were deleted
# (they were byte-identical to or outdated copies of the public headers).
