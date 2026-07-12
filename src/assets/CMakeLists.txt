# ==============================================================================
# src/assets/CMakeLists.txt — chronon3d_assets
# ==============================================================================

add_library(chronon3d_assets OBJECT
    asset_resolver.cpp
    render_preflight_req.cpp
    render_preflight_validate.cpp
    render_preflight_helpers.cpp
    svg_path_loader.cpp
)
# TICKET-PREFLIGHT-PATH-EXISTENCE-MAP-EXISTS-BODY: link chronon3d_path_cache
# (the dedicated STATIC library defined in src/render_graph/CMakeLists.txt
# that owns preflight/path_existence_map.cpp).  The consumer
# render_preflight_validate.cpp calls cache->exists(...) which is the
# O(1) lookup with TTL-driven refresh; without this link dep, the
# apps/chronon3d_cli/chronon3d_cli link step fails with
# `undefined reference to 'chronon3d::preflight::PathExistenceMap::exists(
# std::filesystem::__cxx11::path const&) const'`.  PUBLIC so the
# transitive link closure reaches apps/chronon3d_cli/chronon3d_cli via
# chronon3d_assets → chronon3d_core → chronon3d_pipeline → chronon3d_sdk.
target_link_libraries(chronon3d_assets PUBLIC chronon3d chronon3d_registry chronon3d_path_cache)
