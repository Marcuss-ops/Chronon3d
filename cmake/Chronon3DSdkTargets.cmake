# ==============================================================================
# cmake/Chronon3DSdkTargets.cmake — SDK consumer-facing targets
#
# Single source of truth for the consumer-facing target graph:
#   chronon3d_sdk_impl — one aggregated static archive
#   chronon3d_sdk      — build/install interface closure
#   Chronon3D::SDK     — only namespace-aliased public target
# ==============================================================================

# ==============================================================================
# chronon3d_sdk_impl — the single aggregated STATIC archive
# ==============================================================================
add_library(chronon3d_sdk_impl STATIC
    ${CMAKE_SOURCE_DIR}/src/sdk_impl_marker.cpp
)

# Every registered OBJECT library is folded into the single SDK archive.
foreach(_reg_obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
    if(TARGET ${_reg_obj})
        target_link_libraries(chronon3d_sdk_impl PRIVATE ${_reg_obj})
    endif()
endforeach()

include(${CMAKE_SOURCE_DIR}/cmake/Chronon3DSdkArchive.cmake)
set_target_properties(chronon3d_sdk_impl PROPERTIES EXPORT_NAME SDKImpl)

# ==============================================================================
# chronon3d_sdk + Chronon3D::SDK alias
# ==============================================================================
add_library(chronon3d_sdk INTERFACE)
target_include_directories(chronon3d_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(chronon3d_sdk INTERFACE
    $<BUILD_INTERFACE:chronon3d_pipeline>
    $<INSTALL_INTERFACE:chronon3d_sdk_impl>
)

# `authoring/text.hpp` includes these implementation fragments inside the
# existing Text class. Register them on the SAME public_headers FILE_SET that
# Chronon3DSdkInstall.cmake later extends with the canonical manifest and
# installs once. No second install rule or parallel SDK surface is introduced.
target_sources(chronon3d_sdk INTERFACE
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/include"
    FILES
        "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_content_font.hpp"
        "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_placement_layout.hpp"
        "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_appearance_animation.hpp"
        "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_registry_access.hpp"
        "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_private.hpp"
)

# The static archive bundles object code but does not propagate public third-
# party dependencies. Re-expose the central dependency registry to installed
# consumers through the SDK interface.
foreach(_entry IN LISTS CHRONON3D_SDK_PUBLIC_DEPS)
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 0 _target_alias)
    target_link_libraries(chronon3d_sdk INTERFACE
        $<INSTALL_INTERFACE:${_target_alias}>
    )
endforeach()

# Object propagation for in-tree consumers is owned by chronon3d_pipeline.
set_target_properties(chronon3d_sdk PROPERTIES EXPORT_NAME SDK)
add_library(Chronon3D::SDK ALIAS chronon3d_sdk)
