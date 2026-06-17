# ==============================================================================
# src/backends/software/processors/text_run/CMakeLists.txt
# ==============================================================================

if(CHRONON3D_ENABLE_TEXT)
    target_sources(chronon3d_backend_software PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/text_run_processor.cpp
    )
endif()
