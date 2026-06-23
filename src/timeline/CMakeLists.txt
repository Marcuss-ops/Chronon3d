# ==============================================================================
# src/timeline/CMakeLists.txt
#
# TICKET-034 — Composition lives under chronological namespace
# `<chronon3d/timeline/>` in the public include path.  Its implementation
# (`composition.cpp`) wraps the canonical V1 `compile_camera()` + runtime
# `evaluate()` pipeline into the slim legacy `Composition::camera` field.
#
# We follow the same wiring pattern used by
# `src/scene/camera/camera_v1/CMakeLists.txt`: instead of creating a new
# top-level OBJECT library, we inject the source into the parent
# `chronon3d_scene` OBJECT set so the heavy compile_camera() / spdlog
# translation units (and their transitive includes of camera_program.hpp,
# camera_program_compiler.hpp, camera_session.hpp, camera_descriptor.hpp,
# camera_2_5d.hpp) are built exactly once, by the same target that already
# pulls in the rest of the scene + camera_subsystem.
#
# This is also the cheapest fix for the original concern that motivated
# moving the bodies out-of-line: TUs that only WANT
# `<chronon3d/timeline/composition.hpp>` get a forward-declared header
# (no transitive camera_program heaviness).
# ==============================================================================

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/composition.cpp
)
