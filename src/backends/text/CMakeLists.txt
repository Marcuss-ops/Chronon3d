# ==============================================================================
# src/backends/text/CMakeLists.txt — chronon3d_backend_text
# ==============================================================================

add_library(chronon3d_backend_text STATIC
    text_rasterizer_cache.cpp
    text_rasterizer_render.cpp
    text_material.cpp
    font_engine.cpp
    bidi_segmenter.cpp
    glyph_atlas.cpp
    rich_text.cpp
)
target_link_libraries(chronon3d_backend_text PUBLIC
    chronon3d
    chronon3d_cache
    spdlog::spdlog_header_only
    blend2d::blend2d

)

if(CHRONON3D_ENABLE_TEXT)
    target_link_libraries(chronon3d_backend_text PUBLIC
        freetype
        harfbuzz::harfbuzz
    )
endif()

# ── FriBidi for bidi text segmentation ──
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FRIBIDI IMPORTED_TARGET fribidi)
endif()
if(FRIBIDI_FOUND)
    target_link_libraries(chronon3d_backend_text PUBLIC PkgConfig::FRIBIDI)
    target_compile_definitions(chronon3d_backend_text PUBLIC CHRONON3D_HAS_FRIBIDI)
else()
    message(WARNING "FriBidi not found — bidi text segmentation will use LTR-only fallback")
endif()

target_include_directories(chronon3d_backend_text PRIVATE ${CMAKE_SOURCE_DIR})
