# ==============================================================================
# src/backends/software/processors/text/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/software_text_effects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_text_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text_glow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text_shadow.cpp
)
