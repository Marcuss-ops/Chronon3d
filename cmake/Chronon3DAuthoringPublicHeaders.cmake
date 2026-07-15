# ==============================================================================
# Chronon3D focused public authoring header additions
#
# The frozen core manifest already carries the established authoring primitives
# (Text, Animator, Material, registries, NodeHandle). This explicit no-glob list
# contains only the missing files required by the documented asset/composition/
# layer/scene syntax, keeping FILE_SET membership disjoint.
# ==============================================================================

set(CHRONON3D_AUTHORING_PUBLIC_HEADERS
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_ref.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/asset.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/composition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/layer.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/scene.hpp"

    # Direct implementation includes consumed by the already-public text.hpp.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_appearance_animation.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_content_font.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_placement_layout.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_private.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_registry_access.hpp"

    # layer.hpp requires this lightweight context definition directly.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/extension/extension_context.hpp"
)
