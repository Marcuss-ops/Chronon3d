# ==============================================================================
# src/timeline/CMakeLists.txt
#
# TICKET-034 — Composition lives under chronological namespace
# `<chronon3d/timeline/>` in the public include path.  Its implementation
# (`composition.cpp`) holds the camera-free Composition implementation.
#
# The timeline is a consumer of Scene values and does not depend on the
# camera-authoring subsystem.
# ==============================================================================

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/composition.cpp
    # Timeline scene compilation and evaluation.
    ${CMAKE_CURRENT_SOURCE_DIR}/compile_evaluate.cpp
)
