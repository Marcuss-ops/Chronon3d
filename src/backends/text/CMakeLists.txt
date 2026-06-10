# ==============================================================================
# src/backends/text/CMakeLists.txt — chronon3d_backend_text (STATIC)
#
# Backend text rendering: rasterizzazione font con cache, materiali testo,
# font engine (caricamento, selezione, metrics).
#
# Dipendenze (PUBLIC):
#   chronon3d              — tipi base (Framebuffer, Color, FontStyle...)
#   chronon3d_cache        — caching risultati rasterizzazione
#   spdlog (header-only)   — logging
#   blend2d                — vector graphics / path rasterization
#   freetype               — font loading, glyph rasterization
#   harfbuzz               — shaping (ligature, bidirectional text, ecc.)
#
# Fornisce a: chronon3d_backend_software (via blend2d bridge)
# ==============================================================================
add_library(chronon3d_backend_text STATIC
    text_rasterizer_cache.cpp
    text_rasterizer_render.cpp
    text_material.cpp
    font_engine.cpp
)
target_link_libraries(chronon3d_backend_text
    PUBLIC chronon3d chronon3d_cache
           spdlog::spdlog_header_only blend2d::blend2d freetype harfbuzz::harfbuzz
)
target_include_directories(chronon3d_backend_text PRIVATE ${CMAKE_SOURCE_DIR})
