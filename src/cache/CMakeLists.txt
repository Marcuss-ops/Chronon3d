# ==============================================================================
# src/cache/CMakeLists.txt — chronon3d_cache
# ==============================================================================

add_library(chronon3d_cache STATIC
    cache_diagnostics.cpp
    cache_diagnostics_format.cpp
    cache_policy.cpp
    frame_cache.cpp
    node_cache.cpp
    framebuffer_pool.cpp
    persistent_framebuffer_store.cpp
    video_frame_cache.cpp
    lru_log.cpp
)
target_link_libraries(chronon3d_cache PUBLIC chronon3d xxHash::xxhash)
target_precompile_headers(chronon3d_cache PRIVATE <xxhash.h>)
