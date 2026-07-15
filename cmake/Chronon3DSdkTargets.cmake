# ==============================================================================
# cmake/Chronon3DSdkTargets.cmake — SDK consumer-facing targets
# ==============================================================================

add_library(chronon3d_sdk_impl STATIC
    ${CMAKE_SOURCE_DIR}/src/sdk_impl_marker.cpp
)

foreach(_reg_obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
    if(TARGET ${_reg_obj})
        target_link_libraries(chronon3d_sdk_impl PRIVATE ${_reg_obj})
    endif()
endforeach()

include(${CMAKE_SOURCE_DIR}/cmake/Chronon3DSdkArchive.cmake)
set_target_properties(chronon3d_sdk_impl PROPERTIES EXPORT_NAME SDKImpl)

add_library(chronon3d_sdk INTERFACE)
target_include_directories(chronon3d_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(chronon3d_sdk INTERFACE
    $<BUILD_INTERFACE:chronon3d_pipeline>
    $<INSTALL_INTERFACE:chronon3d_sdk_impl>
)

# Transitive implementation fragments and umbrella subheaders belong to the
# same public_headers FILE_SET installed by Chronon3DSdkInstall.cmake.
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
        "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/params/shape_params.hpp"
        "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/params/media_params.hpp"
        "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/params/text_params.hpp"
        "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/params/three_d_params.hpp"
)

foreach(_entry IN LISTS CHRONON3D_SDK_PUBLIC_DEPS)
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 0 _target_alias)
    target_link_libraries(chronon3d_sdk INTERFACE
        $<INSTALL_INTERFACE:${_target_alias}>
    )
endforeach()

set_target_properties(chronon3d_sdk PROPERTIES EXPORT_NAME SDK)
add_library(Chronon3D::SDK ALIAS chronon3d_sdk)
