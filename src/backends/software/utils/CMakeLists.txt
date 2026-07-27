# ==============================================================================
# src/backends/software/utils/CMakeLists.txt
# ==============================================================================

if(CHRONON3D_USE_BLEND2D)
# Shared Blend2D paint utilities — used by both backend_software and backend_text.
add_library(chronon3d_blend2d_paint OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/blend2d_paint.cpp
)
target_link_libraries(chronon3d_blend2d_paint PUBLIC chronon3d blend2d::blend2d)
target_include_directories(chronon3d_blend2d_paint PRIVATE ${CMAKE_SOURCE_DIR})
endif()

# Backend-internal analysis, projection and effect helpers.
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/framebuffer_analysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/projection_utils.cpp
)
