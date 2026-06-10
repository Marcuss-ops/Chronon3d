# ==============================================================================
# src/cache/CMakeLists.txt — chronon3d_cache (STATIC)
#
# Sistemi di caching: frame cache (memoizzazione risultati render), node cache
# (memoizzazione nodi), disk cache (persistenza su disco), framebuffer pool
# (riuso buffer), persistent bake cache, video frame cache.
#
# Dipendenze (PUBLIC):
#   chronon3d     — tipi base (Frame, Framebuffer, Color...)
#   xxHash::xxhash — XXH64 per chiavi hash nelle cache
#
# Fornisce a: chronon3d_graph, chronon3d_runtime, chronon3d_backend_text,
#             chronon3d_backend_video, chronon3d_backend_software, chronon3d_scene
# ==============================================================================
add_library(chronon3d_cache STATIC
    frame_cache.cpp
    node_cache.cpp
    disk_node_cache.cpp
    framebuffer_pool.cpp
    persistent_bake_cache.cpp
    video_frame_cache.cpp
)
target_link_libraries(chronon3d_cache
    PUBLIC chronon3d xxHash::xxhash
)
target_precompile_headers(chronon3d_cache PRIVATE <xxhash.h>)
