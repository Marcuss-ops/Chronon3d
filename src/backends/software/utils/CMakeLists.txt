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

# TICKET-011 cmake-boundary: bridge symbol ownership moved to
# chronon3d_backend_assets (its only consumer is image_renderer.cpp)
# to break the OBJECT↔OBJECT cycle.
# Remaining sources stay with backend_software (framebuffer_analysis,
# projection_utils, effects — all backend_software-internal helpers).
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/framebuffer_analysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/projection_utils.cpp
)

# Effects implementations
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/effect_blur.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/effect_color.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/effect_wave.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/glow_pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/glow_bloom.cpp       # FASE 7 — extracted bloom mode
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/effect_glow_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/effect_shadow_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/effect_bloom_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/effect_stack.cpp
)
