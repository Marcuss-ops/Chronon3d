# Shader ABI consistency test: verifies embedded SPIR-V bindings match C++ expectations
# Requires: CHRONON3D_ENABLE_VULKAN, spirv-reflect
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

if(NOT CHRONON3D_ENABLE_VULKAN)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_shader_abi_consistency_tests
    TIER UNIT
    SOURCES backends/test_shader_abi_consistency.cpp
    LINK_TARGETS chronon3d_pipeline unofficial::spirv-reflect
)

# Object libraries (chronon3d_backend_vulkan) don't propagate PUBLIC properties.
# Inject the necessary compile definition and generated-shader include path
# so the test can include the embedded SPIR-V headers.
target_compile_definitions(chronon3d_shader_abi_consistency_tests
    PRIVATE CHRONON3D_ENABLE_VULKAN)
target_include_directories(chronon3d_shader_abi_consistency_tests
    PRIVATE "${CHRONON3D_VULKAN_GENERATED_DIR}")

# The embedded SPIR-V and ABI headers must exist before the test compiles.
add_dependencies(chronon3d_shader_abi_consistency_tests
    chronon3d_shader_abi_headers)

chronon3d_add_test_suite(
    NAME chronon3d_vulkan_debug_context_tests
    TIER UNIT
    SOURCES backends/test_vulkan_debug_context.cpp
    LINK_TARGETS chronon3d_pipeline Vulkan::Vulkan spdlog::spdlog
)
target_compile_definitions(chronon3d_vulkan_debug_context_tests
    PRIVATE CHRONON3D_ENABLE_VULKAN)

chronon3d_add_test_suite(
    NAME chronon3d_vulkan_descriptor_arena_tests
    TIER UNIT
    SOURCES backends/test_vulkan_descriptor_arena.cpp
    LINK_TARGETS Vulkan::Vulkan doctest::doctest)
target_compile_definitions(chronon3d_vulkan_descriptor_arena_tests
    PRIVATE CHRONON3D_ENABLE_VULKAN)

chronon3d_add_test_suite(
    NAME chronon3d_vulkan_kernel_store_tests
    TIER UNIT
    SOURCES backends/test_vulkan_kernel_store.cpp
    LINK_TARGETS Vulkan::Vulkan doctest::doctest)
target_compile_definitions(chronon3d_vulkan_kernel_store_tests
    PRIVATE CHRONON3D_ENABLE_VULKAN)

chronon3d_add_test_suite(
    NAME chronon3d_vulkan_submission_ring_tests
    TIER INTEGRATION
    SOURCES backends/test_vulkan_submission_ring.cpp
    LINK_TARGETS chronon3d_pipeline Vulkan::Vulkan spdlog::spdlog)
target_compile_definitions(chronon3d_vulkan_submission_ring_tests
    PRIVATE CHRONON3D_ENABLE_VULKAN)

chronon3d_add_test_suite(
    NAME chronon3d_vulkan_upload_ring_tests
    TIER INTEGRATION
    SOURCES backends/test_vulkan_upload_ring.cpp
    LINK_TARGETS chronon3d_pipeline Vulkan::Vulkan spdlog::spdlog)
target_compile_definitions(chronon3d_vulkan_upload_ring_tests
    PRIVATE CHRONON3D_ENABLE_VULKAN)

# Headless "TEST 123" canary: dispatches the embedded tile-bin/raster SPIR-V
# on a real Vulkan device and verifies format qualification and premultiplied
# compositing.  Skips when no compute device is available.
chronon3d_add_test_suite(
    NAME chronon3d_text_tile_raster_canary_tests
    TIER INTEGRATION
    SOURCES backends/test_text_tile_raster_canary.cpp
    LINK_TARGETS chronon3d_pipeline Vulkan::Vulkan)
target_compile_definitions(chronon3d_text_tile_raster_canary_tests
    PRIVATE CHRONON3D_ENABLE_VULKAN)
target_include_directories(chronon3d_text_tile_raster_canary_tests
    PRIVATE "${CHRONON3D_VULKAN_GENERATED_DIR}")
add_dependencies(chronon3d_text_tile_raster_canary_tests
    chronon3d_shader_abi_headers)