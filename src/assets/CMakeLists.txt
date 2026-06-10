# ==============================================================================
# src/assets/CMakeLists.txt — chronon3d_assets (STATIC)
#
# Asset pipeline: preflight rendering (richieste, validazione, helper) e
# caricamento path SVG.
#
# Dipendenze (PUBLIC):
#   chronon3d             — tipi base
#   chronon3d_registry    — registry sampler/shape per lookup asset
#
# Fornisce a: (aggregato chronon3d_core)
# ==============================================================================
add_library(chronon3d_assets STATIC
    render_preflight_req.cpp
    render_preflight_validate.cpp
    render_preflight_helpers.cpp
    svg_path_loader.cpp
)
target_link_libraries(chronon3d_assets
    PUBLIC chronon3d chronon3d_registry
)
