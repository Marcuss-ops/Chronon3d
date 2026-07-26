# ==============================================================================
# cmake/Chronon3DSdkTargets.cmake — SDK consumer-facing targets
# ==============================================================================

add_library(chronon3d_sdk_impl STATIC
    ${CMAKE_SOURCE_DIR}/src/sdk_impl_marker.cpp
)

chronon3d_link_registered_objects_into_archive(chronon3d_sdk_impl)

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

foreach(_entry IN LISTS CHRONON3D_SDK_PUBLIC_DEPS)
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 0 _target_alias)
    target_link_libraries(chronon3d_sdk INTERFACE
        $<INSTALL_INTERFACE:${_target_alias}>
    )
endforeach()

set_target_properties(chronon3d_sdk PROPERTIES EXPORT_NAME SDK)
add_library(Chronon3D::SDK ALIAS chronon3d_sdk)
