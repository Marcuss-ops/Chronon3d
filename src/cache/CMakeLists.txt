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

# P1-E Pass E — internal type headers for NodeCache / FramebufferPool /
# PersistentFramebufferStore moved out of include/chronon3d/ into this
# submodule's include_private/.  PRIVATE for cache's own .cpp
# compilation only; INTERFACE removed (TICKET-P1-PART2, P1 #12)
# because all include_private/ headers have been promoted to the
# public include/chronon3d/cache/ surface.
# NEVER installed: `install(DIRECTORY include/)` does not traverse src/.
target_include_directories(chronon3d_cache
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include_private
)
