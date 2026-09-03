# ==============================================================================
# src/backends/text/CMakeLists.txt — chronon3d_backend_text
# ==============================================================================

add_library(chronon3d_backend_text OBJECT
    text_material.cpp
    font_engine.cpp
    font_engine_freetype.cpp
    font_engine_placed_run.cpp        # resolve_placed_glyph_run
    typewriter_layout_cache.cpp       # per-runtime typewriter layout cache
    typewriter_layout.cpp             # compute_typewriter_layout (restored to core after content externalization)
    bidi_segmenter.cpp
    text_layout_inline.cpp
    glyph_atlas.cpp                   # canonical impl in resources/glyph_atlas.cpp
    text_render_resources.cpp         # per-renderer text resources
    text_render_resources_cache.cpp   # raster/glyph cache implementation
)
set_target_properties(chronon3d_backend_text PROPERTIES UNITY_BUILD OFF)
# These are deliberately independent compilation units.  Keep this list
# explicit so a future global UNITY_BUILD/IPO change cannot silently fold the
# large font/resource implementations back together.
set_source_files_properties(
    font_engine.cpp
    text_render_resources.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
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
