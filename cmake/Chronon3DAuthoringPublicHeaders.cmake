# ==============================================================================
# Chronon3D focused public authoring header additions
#
# `Chronon3DPublicHeaders.cmake` and `Chronon3DSdkTargets.cmake` already carry
# the established authoring primitives and text implementation fragments. This
# explicit no-glob list contains only the missing files required by the
# documented asset/composition/layer/scene syntax.
# ==============================================================================

set(CHRONON3D_AUTHORING_PUBLIC_HEADERS
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_ref.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/asset.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/composition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/layer.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/scene.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/subtitle_track_builder.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/presets/text/subtitle.hpp"

    # layer.hpp includes this lightweight host-owned registry context directly.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/extension/extension_context.hpp"
)
