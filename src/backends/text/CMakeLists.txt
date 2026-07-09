# ==============================================================================
# src/backends/text/CMakeLists.txt — chronon3d_backend_text
# ==============================================================================

add_library(chronon3d_backend_text OBJECT
    text_rasterizer_cache.cpp        # M1.5#7 stub — canonical impl in resources/raster_cache.cpp
    text_rasterizer_render.cpp       # M1.5#11 — legacy entry (deleted with M1.5#9 Step 5)
    blend2d_glyph_conversion.cpp     # M1.5#11 — fill/stroke style applicator + Hb→BL glyph run converter (pure conversion)
    freetype_outline_conversion.cpp  # M1.5#11 — FT_Outline→BLPath outline-decoder (pure conversion, caller-owned FT_Face)
    glyph_texture_updater.cpp        # FASE 4 — atlas-or-fill placed-glyph renderer (centralizes render_run + for-line duplication)
    text_rasterizer_ink.cpp
    text_material.cpp
    font_engine.cpp
    font_engine_placed_run.cpp    # FASE 14 — resolve_placed_glyph_run extracted
    bidi_segmenter.cpp
    glyph_atlas.cpp                  # M1.5#7 stub — canonical impl in resources/glyph_atlas.cpp
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
