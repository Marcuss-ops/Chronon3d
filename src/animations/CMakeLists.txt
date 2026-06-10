# ==============================================================================
# src/animations/CMakeLists.txt — chronon3d_animations (STATIC)
#
# Catalogo animazioni: preset animazioni predefinite (background, effetti)
# e cataloghi animazione utilizzabili dalla scena.
#
# Dipendenze (PUBLIC):
#   chronon3d                  — tipi base
#   chronon3d_scene            — modello scena (per applicare animazioni)
#   chronon3d_backend_software — backend rendering (per preview animazioni)
#   nlohmann_json              — serializzazione preset
#
# Fornisce a: (aggregato chronon3d_core via WHOLE_ARCHIVE)
# ==============================================================================
add_library(chronon3d_animations STATIC
    animation_catalog.cpp
    background_presets.cpp
)
target_link_libraries(chronon3d_animations
    PUBLIC chronon3d chronon3d_scene chronon3d_backend_software
           nlohmann_json::nlohmann_json
)
