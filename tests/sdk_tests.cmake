# ── SDK Tests ─────────────────────────────────────────────────────
# Tests in tests/sdk/ lock the manifest-reachable public surface of the SDK.
chronon3d_add_test_suite(
    NAME chronon3d_sdk_tests
    TIER SDK
    SOURCES sdk/test_sdk_render_grid_background.cpp
            sdk/test_sdk_archive_manifest.cpp
            sdk/test_sdk_assets_root_isolation.cpp
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_scene chronon3d_pipeline chronon3d_backend_software chronon3d_backend_image
)

target_compile_definitions(chronon3d_sdk_tests PRIVATE
    CHRONON3D_SDK_ARCHIVE_PATH=${CMAKE_BINARY_DIR}/src/libchronon3d_sdk_impl.a
)

# C ABI / render-plan schema validator coverage belongs to the SDK/ABI domain.
set(_chronon3d_c_abi_links
    chronon3d_render_plan
    chronon3d_render_plan_compiler
    nlohmann_json::nlohmann_json)
set(_chronon3d_c_abi_sources
    c_abi/test_render_plan_decoder.cpp
    c_abi/test_render_plan_validator.cpp
    c_abi/test_prepared_render_plan.cpp)
if(TARGET chronon3d_c)
    list(APPEND _chronon3d_c_abi_links chronon3d_c)
    list(APPEND _chronon3d_c_abi_sources c_abi/test_c_api_v2.cpp)
endif()
chronon3d_add_test_suite(
    NAME chronon3d_c_abi_tests
    TIER UNIT
    LINK_TARGETS ${_chronon3d_c_abi_links}
    SOURCES ${_chronon3d_c_abi_sources}
)
