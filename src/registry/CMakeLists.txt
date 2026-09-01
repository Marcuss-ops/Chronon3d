# ==============================================================================
# src/registry/CMakeLists.txt — chronon3d_registry
#
# Engine-level registries only. Editorial visual/text preset ownership lives
# upstream; Chronon keeps only registries required to execute concrete
# renderer primitives.
# ==============================================================================

add_library(chronon3d_registry OBJECT
    backend_registry.cpp
    sampler_registry.cpp
    shape_registry.cpp
    source_registry.cpp
)
set_target_properties(chronon3d_registry PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_registry PUBLIC chronon3d)
target_include_directories(chronon3d_registry PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_registry PRIVATE <chronon3d/detail/pch.hpp>)
