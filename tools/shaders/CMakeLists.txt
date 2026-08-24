# Host build tools for shader processing
# spirv_abi_gen: SPIR-V → C++ ABI header (build-time codegen)

add_executable(spirv_abi_gen spirv_abi_gen.cpp)
target_link_libraries(spirv_abi_gen PRIVATE unofficial::spirv-reflect)
set_target_properties(spirv_abi_gen PROPERTIES
    UNITY_BUILD OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
)