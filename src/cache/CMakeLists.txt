# ==============================================================================
# src/cache/CMakeLists.txt — chronon3d_cache
# ==============================================================================

add_library(chronon3d_cache OBJECT
    cache_policy.cpp
    frame_cache.cpp
    node_cache.cpp
    framebuffer_pool.cpp
    persistent_framebuffer_store.cpp
    video_frame_cache.cpp
    lru_log.cpp
)
if(CHRONON3D_ENABLE_DIAGNOSTICS)
    target_sources(chronon3d_cache PRIVATE
        cache_diagnostics.cpp
        cache_diagnostics_format.cpp
    )
endif()
target_link_libraries(chronon3d_cache PUBLIC chronon3d xxHash::xxhash)
target_precompile_headers(chronon3d_cache PRIVATE <xxhash.h>)
