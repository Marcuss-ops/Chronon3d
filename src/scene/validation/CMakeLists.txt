# ==============================================================================
# src/scene/validation/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/scene_validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scene_validation_registry.cpp
)
