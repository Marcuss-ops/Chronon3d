# ==============================================================================
# src/scene/builders/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_builder_effects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_command_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scene_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/commands/motion_preset_methods.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/commands/motion_preset_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/commands/shape_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/commands/node_transform_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/commands/layer_property_commands.cpp
)
