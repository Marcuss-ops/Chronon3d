# ==============================================================================
# src/scene/CMakeLists.txt — chronon3d_scene (STATIC)
#
# Modello e costruzione della scena.  Sottocomponenti:
#   scene/camera/     ← sistemi camera (rig, motion presets, framing, HUD overlay)
#   scene/model/      ← layer, render node factory, transform resolver
#   scene/builders/   ← LayerBuilder, SceneBuilder, command registry
#   scene/builders/commands/ ← comandi builder (motion, shape, transform, layer)
#   scene/validation/ ← validatore scena + registry validazione
#   scene/utils/      ← helper (dark grid background)
#   presets/          ← motion presets (reveal, 3D, glow, idle, impact) + phrase
#   specscene/        ← scene specification (model, parser, compiler, validation)
#   timeline/         ← timeline builder
#   animations/       ← animation camera clips + motion applier
#   layout/           ← layout solver
#   math/             ← path utilities
#   media/            ← media placement
#   vector/           ← path factories
#
# NOTA: include file da directory esterne (../timeline, ../animations, ...)
# perché fanno ancora parte del subsystem scene.  In futuro potrebbero
# essere estratti in subsystem propri.
#
# Dipendenze (PUBLIC):
#   chronon3d                 — tipi base
#   chronon3d_runtime         — pipeline + graph evaluation
#   chronon3d_graph           — render graph types
#   chronon3d_registry        — registry sampler/shape/source
#   chronon3d_cache           — cache rendering
#   chronon3d_effects         — effect registry
#   chronon3d_backend_image   — image I/O per scene con immagini
#
# Dipendenze (PRIVATE):
#   spdlog, fmt, tomlplusplus — logging, formattazione, config
#
# Fornisce a: chronon3d_animations, (aggregato chronon3d_core via WHOLE_ARCHIVE)
# ==============================================================================
add_library(chronon3d_scene STATIC
    camera/camera_motion_presets.cpp
    camera/camera_rig.cpp
    camera/camera_rig_presets.cpp
    camera/camera_projection.cpp
    camera/camera_shot_validator.cpp
    camera/camera_framing.cpp
    camera/camera_path_sampler.cpp
    camera/camera_debug_overlay.cpp
    camera/overlay_hud_panels.cpp
    camera/overlay_kinematic_panels.cpp
    camera/overlay_spatial_panels.cpp
    model/layer.cpp
    model/mask_utils.cpp
    model/render_node_factory.cpp
    model/transform_resolver.cpp
    utils/dark_grid_background.cpp
    builders/layer_builder.cpp
    builders/layer_builder_effects.cpp
    builders/layer_command_registry.cpp
    builders/commands/motion_preset_methods.cpp
    builders/commands/motion_preset_commands.cpp
    builders/commands/shape_commands.cpp
    builders/commands/node_transform_commands.cpp
    builders/commands/layer_property_commands.cpp
    validation/scene_validator.cpp
    validation/scene_validation_registry.cpp
    builders/camera_api.cpp
    builders/scene_builder.cpp
    # ── File esterni (src/ root) inclusi nel subsystem scene ──
    ../timeline/timeline_builder.cpp
    ../animations/camera/camera_tilt_clips.cpp
    ../animations/camera_motion_applier.cpp
    ../layout/layout_solver.cpp
    ../math/path_utils.cpp
    ../presets/camera_motion_clip.cpp
    ../presets/motion_renderer.cpp
    ../presets/motion_resolver.cpp
    ../presets/motion_preset_registry.cpp
    ../presets/motion_presets_reveal.cpp
    ../presets/motion_presets_3d.cpp
    ../presets/motion_presets_glow.cpp
    ../presets/motion_presets_idle.cpp
    ../presets/motion_presets_impact.cpp
    ../presets/phrase_presets.cpp
    ../presets/phrase_group_builder.cpp
    ../specscene/model/specscene.cpp
    ../specscene/parser/specscene_parsers.cpp
    ../specscene/compiler/specscene_compiler.cpp
    ../specscene/validation/specscene_validator.cpp
    ../media/media_placement.cpp
    ../vector/path_factories.cpp
)
target_include_directories(chronon3d_scene PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_scene
    PUBLIC  chronon3d chronon3d_runtime chronon3d_graph chronon3d_registry
            chronon3d_cache chronon3d_effects chronon3d_backend_image
    PRIVATE spdlog::spdlog_header_only fmt::fmt tomlplusplus::tomlplusplus
)
target_include_directories(chronon3d_scene PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_scene PRIVATE <chronon3d/detail/pch.hpp>)
