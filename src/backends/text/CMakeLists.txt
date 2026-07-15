# ==============================================================================
# src/backends/text/CMakeLists.txt — chronon3d_backend_text
# ==============================================================================

add_library(chronon3d_backend_text OBJECT
    blend2d_glyph_conversion.cpp     # fill/stroke style applicator + Hb→BL glyph run converter
    freetype_outline_conversion.cpp  # FT_Outline→BLPath outline decoder
    glyph_texture_updater.cpp        # atlas-or-fill placed-glyph renderer
    text_rasterizer_ink.cpp
    text_material.cpp
    font_engine.cpp
    font_engine_placed_run.cpp        # resolve_placed_glyph_run
    typewriter_layout_cache.cpp       # per-runtime typewriter layout cache
    bidi_segmenter.cpp
    glyph_atlas.cpp                   # canonical impl in resources/glyph_atlas.cpp
    text_render_resources.cpp         # per-renderer text resources
)
target_link_libraries(chronon3d_backend_text PUBLIC
    chronon3d
    chronon3d_cache
    spdlog::spdlog_header_only
    blend2d::blend2d
    chronon3d_blend2d_paint
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
