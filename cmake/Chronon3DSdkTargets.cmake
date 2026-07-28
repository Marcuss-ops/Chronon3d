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

if(CHRONON3D_BUILD_C_API)
    add_library(chronon3d_c SHARED
        ${CMAKE_SOURCE_DIR}/src/c_api/chronon3d_c_api.cpp
    )
    target_include_directories(chronon3d_c PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(chronon3d_c PRIVATE
        chronon3d_sdk
        nlohmann_json::nlohmann_json
        # TICKET-JSON-SCHEMA-VALIDATOR — chronon3d_render_plan is the
        # render-plan JSON Schema validator OBJECT library.  It is NOT
        # linked into chronon3d_core (would require it in the SDK export
        # set — see Chronon3DRegistry.cmake install/export wiring), so we
        # link it directly to the C API SHARED lib here.  chronon3d_c is
        # the only SDK surface that parses render-plan JSON
        # (chronon_plan_compile_json + the legacy render entrypoint),
        # so this direct link is the canonical propagation path.  In-tree
        # tests link chronon3d_render_plan directly via
        # tests/c_abi_tests.cmake LINK_TARGETS.
        chronon3d_render_plan
        chronon3d_render_plan_compiler
    )
    set_target_properties(chronon3d_c PROPERTIES
        EXPORT_NAME C
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        INSTALL_RPATH "$ORIGIN"
    )
    add_library(Chronon3D::C ALIAS chronon3d_c)
endif()
