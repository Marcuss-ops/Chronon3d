# ==============================================================================
# src/backends/assets/CMakeLists.txt — chronon3d_backend_assets
#
# TICKET-011 cmake-boundary: chronon3d_backend_assets is now an OBJECT
# library.  Source ownership for `blend2d_bridge_*` was moved here from
# chronon3d_backend_software's utils/ so this OBJECT owns its own
# dependency surface — backend_assets no longer needs to link
# backend_software, breaking the pre-existing mutual PUBLIC-link cycle
# (which CMake forbids between OBJECT_LIBRARIES).
#
# Image rendering (image_renderer.cpp) consumes blend2d_bridge directly;
# the software backend PRIVATE-links back to backend_assets for the same
# symbols (one-way edge, no cycle).
# ==============================================================================

add_library(chronon3d_backend_assets OBJECT
    image_cache.cpp
    image_renderer.cpp
    # bridge symbols previously owned by backend_software's utils/.  Moved
    # here so backend_assets's image_renderer.cpp links against its own
    # OBJECT and the cycle backend_assets ↔ backend_software dissolves.
    ${CMAKE_CURRENT_SOURCE_DIR}/../software/utils/blend2d_bridge_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../software/utils/blend2d_bridge_transforms_fb.cpp
)
if(CHRONON3D_USE_BLEND2D)
    # 'target_sources' only accepts PRIVATE/PUBLIC/INTERFACE — not OBJECT.
    # (OBJECT is an add_library type, not a target_sources visibility.)
    target_sources(chronon3d_backend_assets PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../software/utils/blend2d_bridge_transforms.cpp
    )
endif()

# backend_assets no longer PUBLIC-links backend_software — symbols it needs
# (blend2d_bridge_*) are owned in this OBJECT's own source list above.  The
# remaining PUBLIC link to backend_image provides the ImageBackend interface
# (which asset's ImageCache::m_backend holds as a shared_ptr).
target_link_libraries(chronon3d_backend_assets PUBLIC chronon3d chronon3d_backend_image)
target_include_directories(chronon3d_backend_assets PRIVATE ${Stb_INCLUDE_DIR})
