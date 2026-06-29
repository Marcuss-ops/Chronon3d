# ==============================================================================
# src/registry/CMakeLists.txt — chronon3d_registry
# ==============================================================================

add_library(chronon3d_registry OBJECT
    sampler_registry.cpp
    shape_registry.cpp
    source_registry.cpp
    text_preset_registry.cpp
    # Phase 3.2 — AnimatorResolver implementations moved out of the public
    # header (include/chronon3d/registry/animator_resolver.hpp) into this
    # .cpp so the header is declaration-only. The three static bodies
    # (spec_is_rich + rich_paint_anchor + compose_for — 22-branch
    # resolver table) now link from this OBJECT library.
    animator_resolver.cpp
)
set_target_properties(chronon3d_registry PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_registry PUBLIC chronon3d)
target_include_directories(chronon3d_registry PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_registry PRIVATE <chronon3d/detail/pch.hpp>)
