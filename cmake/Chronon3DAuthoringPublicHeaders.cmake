# ==============================================================================
# Chronon3D focused public authoring header additions
#
# `Chronon3DPublicHeaders.cmake` and `Chronon3DSdkTargets.cmake` already carry
# the established authoring primitives and text implementation fragments. This
# explicit no-glob list contains only the missing files required by the
# documented asset/composition/layer/scene syntax and focused project metadata
# APIs intended for external SDK consumers.
# ==============================================================================

set(CHRONON3D_AUTHORING_PUBLIC_HEADERS
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/asset.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/composition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/layer.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/scene.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/subtitle_track_builder.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/presets/text/subtitle.hpp"

    # Focused project-owned semantic content registries for SDK consumers.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/registry/content_registry.hpp"

    # layer.hpp includes this lightweight host-owned registry context directly.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/extension/extension_context.hpp"
)
