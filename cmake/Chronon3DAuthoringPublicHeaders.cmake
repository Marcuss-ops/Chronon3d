# ==============================================================================
# Chronon3D public authoring header subset
#
# Explicit, no-glob extension of the installed SDK surface.  These headers are
# required by the documented lightweight authoring syntax; they remain a
# separate FILE_SET so the core SDK manifest stays auditable and no umbrella
# header is introduced.
# ==============================================================================

set(CHRONON3D_AUTHORING_PUBLIC_HEADERS
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
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/basic_registry.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_appearance_animation.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_content_font.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_placement_layout.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_private.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_registry_access.hpp"
)
