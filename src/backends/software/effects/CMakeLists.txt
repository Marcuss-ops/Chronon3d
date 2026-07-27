# ==============================================================================
# Software effect algorithms and dispatch helpers.
# Processor adapters remain under processors/effects; this directory owns the
# implementation kernels and the single effect-stack dispatch path.
# ==============================================================================

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/effect_blur.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effect_color.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effect_wave.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glow_pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glow_bloom.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glow_pipeline_converters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effect_glow_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effect_shadow_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effect_bloom_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effect_stack.cpp
)
