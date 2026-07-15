# ==============================================================================
# Chronon3D focused public authoring header subset
#
# Explicit, no-glob extension of the installed SDK surface. No umbrella header
# or second link target is introduced.
# ==============================================================================

set(CHRONON3D_AUTHORING_PUBLIC_HEADERS
    # Documented authoring surface.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_ref.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/asset.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/animator.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/composition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/layer.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/material.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/motion_registry.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/resolution_outcome.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/scene.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/selector.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/style_registry.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/text.hpp"

    # Authoring implementation headers included directly by text.hpp.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/basic_registry.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_appearance_animation.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_content_font.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_placement_layout.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_private.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_registry_access.hpp"

    # Direct non-authoring dependencies not present in the core manifest.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/extension/extension_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/node_handle.hpp"
)
