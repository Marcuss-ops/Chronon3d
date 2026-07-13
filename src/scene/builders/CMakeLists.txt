# ==============================================================================
# src/scene/builders/CMakeLists.txt
# ==============================================================================
#
# LayerBuilder implementation is split by domain. The public facade remains
# single: include/chronon3d/scene/builders/layer_builder.hpp.

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_builder_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_builder_transform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_builder_layout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_builder_text.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_builder_shapes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_builder_effects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_builder_media.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_builder_masks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text_run_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layer_command_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scene_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/commands/motion_preset_commands.cpp
)
