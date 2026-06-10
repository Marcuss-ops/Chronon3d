# ==============================================================================
# src/backends/text/CMakeLists.txt — chronon3d_backend_text
# ==============================================================================

add_library(chronon3d_backend_text STATIC
    text_rasterizer_cache.cpp
    text_rasterizer_render.cpp
    text_material.cpp
    font_engine.cpp
)
target_link_libraries(chronon3d_backend_text PUBLIC
    chronon3d
    chronon3d_cache
    spdlog::spdlog_header_only
    blend2d::blend2d
    freetype
    harfbuzz::harfbuzz
)
target_include_directories(chronon3d_backend_text PRIVATE ${CMAKE_SOURCE_DIR})
