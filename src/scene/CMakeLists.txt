# ==============================================================================
# src/scene/CMakeLists.txt — chronon3d_scene
#
# This target aggregates sources from scene/, timeline/, animations/camera/,
# scene/ are referenced via relative paths (e.g. ../timeline/...).
# ==============================================================================

add_library(chronon3d_scene OBJECT
    # Local files outside subsystem dirs
    utils/dark_grid_background.cpp
    # Moved into src/scene/ to eliminate ../ cross-directory references
    timeline_builder.cpp
    camera_tilt_clips.cpp
    camera_motion_applier.cpp
    layout_solver.cpp
    overlay_layout_resolver.cpp
    path_utils.cpp
    media_placement.cpp
    path_factories.cpp
    presets/camera_motion_clip.cpp
    # ── P2 joints API (Layer <-> Layer attachment) ───────────────────────
    joints/joints_api.cpp

    # ── ADR-032 — contract-zone converters (chronon/abi.h motion handshake) ──
    motion_contract_bridge.cpp
    motion_contract_apply.cpp

    # Authoring facade implementations that depend on LayerBuilder.
    subtitle_track_builder.cpp
)

# ==============================================================================
# Subsystem source files (each directory adds its sources via target_sources)
# ==============================================================================
add_subdirectory(camera)
add_subdirectory(model)
add_subdirectory(builders)
add_subdirectory(validation)
add_subdirectory(registry)    # Camera System V1 — motion/constraint registries
target_include_directories(chronon3d_scene PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_scene
    PUBLIC
        chronon3d
        chronon3d_runtime
        chronon3d_graph
        chronon3d_registry
        chronon3d_cache
        chronon3d_effects
        chronon3d_backend_image
    PRIVATE
        spdlog::spdlog_header_only        fmt::fmt
    )
target_include_directories(chronon3d_scene PRIVATE ${CMAKE_SOURCE_DIR}/include)
# TICKET-029-followup: workaround gcc 15.2 ICE su PCH — disabilitato
# temporaneamente; riattivare quando il compiler toolchain è aggiornato.
# target_precompile_headers(chronon3d_scene PRIVATE <chronon3d/detail/pch.hpp>)
