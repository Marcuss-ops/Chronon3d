# ==============================================================================
# src/extension/CMakeLists.txt — chronon3d_extension
# ==============================================================================

add_library(chronon3d_extension STATIC
    extension_registry.cpp
)
target_link_libraries(chronon3d_extension PUBLIC
    chronon3d
    chronon3d_graph
    chronon3d_registry
    chronon3d_effects
)
target_include_directories(chronon3d_extension PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_extension PRIVATE <chronon3d/detail/pch.hpp>)
