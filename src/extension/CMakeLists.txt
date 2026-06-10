# ==============================================================================
# src/extension/CMakeLists.txt — chronon3d_extension (STATIC)
#
# Sistema di estensione: registry delle estensioni e loader dinamico.
# Permette di caricare moduli esterni che registrano nuovi effetti, nodi,
# o processor.
#
# Dipendenze (PUBLIC):
#   chronon3d              — tipi base
#   chronon3d_graph        — tipi grafo (per registrare nodi custom)
#   chronon3d_registry     — registry (per registrare sampler/shape/source)
#   chronon3d_effects      — effect registry (per registrare effetti custom)
#
# Fornisce a: (aggregato chronon3d_core)
# ==============================================================================
add_library(chronon3d_extension STATIC
    extension_registry.cpp
    extension_loader.cpp
)
target_link_libraries(chronon3d_extension
    PUBLIC chronon3d chronon3d_graph chronon3d_registry chronon3d_effects
)
target_include_directories(chronon3d_extension PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_extension PRIVATE <chronon3d/detail/pch.hpp>)
