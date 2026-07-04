# ==============================================================================
# src/backends/text/CMakeLists.txt — chronon3d_backend_text
# ==============================================================================

add_library(chronon3d_backend_text OBJECT
    text_rasterizer_cache.cpp        # M1.5#7 stub — canonical impl in resources/raster_cache.cpp
    text_rasterizer_render.cpp
    text_rasterizer_ink.cpp
    text_material.cpp
    font_engine.cpp
    bidi_segmenter.cpp
    glyph_atlas.cpp                  # M1.5#7 stub — canonical impl in resources/glyph_atlas.cpp
    rich_text.cpp
    text_render_resources.cpp        # M1.5#7 slimmed — resolve_handle only    # No submodule hierarchy currently (M1.5#7 split deferred to a follow-up
    # commit: it requires migrating text_rasterizer_render.cpp and friends to
    # the per-session instance API, which is out of scope for M1.5#7).

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
