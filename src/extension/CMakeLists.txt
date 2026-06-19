# ==============================================================================
# src/extension/CMakeLists.txt — chronon3d_extension
#
# Fase 5: Extension system — ExtensionModule + ExtensionRegistry.
# Minimal owning registry for extension modules.
# ==============================================================================

add_library(chronon3d_extension STATIC
    extension_catalog.cpp
)
target_link_libraries(chronon3d_extension PRIVATE chronon3d)
target_include_directories(chronon3d_extension PRIVATE ${CMAKE_SOURCE_DIR})
