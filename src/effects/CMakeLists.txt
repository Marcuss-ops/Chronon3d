# ==============================================================================
# src/effects/CMakeLists.txt — chronon3d_effects (STATIC)
#
# Registry effetti: mantiene il catalogo di tutti gli effetti registrabili
# (Blur, Tint, Glow, DropShadow, Bloom, FocusInLadder, ecc.).
#
# Dipendenze (PUBLIC):
#   chronon3d              — tipi base (EffectType, EffectParams)
#   meshoptimizer          — (opzionale, CHRONON3D_ENABLE_MESH) mesh processing
#
# Nota: usa ${CHRONON3D_MESHOPTIMIZER_LINK} definito in src/CMakeLists.txt
#
# Fornisce a: chronon3d_runtime, chronon3d_backend_software, chronon3d_scene,
#             chronon3d_extension
# ==============================================================================
add_library(chronon3d_effects STATIC
    effect_registry.cpp
)
target_link_libraries(chronon3d_effects
    PUBLIC chronon3d ${CHRONON3D_MESHOPTIMIZER_LINK}
)
