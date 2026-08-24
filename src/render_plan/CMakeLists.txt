# ==============================================================================
# src/render_plan/CMakeLists.txt — chronon3d_render_plan
#
# Standalone OBJECT libraries for the render-plan JSON decoder, validator and
# compiler. They have no SDK state or asset access. Their object files are
# attached explicitly to the consumers that expose render-plan compilation;
# this file does not link them into chronon3d_core.
# ==============================================================================

add_library(chronon3d_render_plan OBJECT
    render_plan_decoder.cpp
    render_plan_validator.cpp
)
set_target_properties(chronon3d_render_plan PROPERTIES UNITY_BUILD OFF)

add_library(chronon3d_render_plan_compiler OBJECT
    render_plan_compiler.cpp
    animation_intent.cpp
    visual_preset_materializer.cpp
)
set_target_properties(chronon3d_render_plan_compiler PROPERTIES UNITY_BUILD OFF)
set_source_files_properties(
    render_plan_compiler.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

set(CHRONON3D_RENDER_PLAN_SCHEMA_FILE
    ${CMAKE_SOURCE_DIR}/schemas/chronon.render-plan.v1.schema.json)
file(READ "${CHRONON3D_RENDER_PLAN_SCHEMA_FILE}"
     CHRONON3D_RENDER_PLAN_SCHEMA_JSON)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/render_plan_schema.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated_render_plan_schema.hpp
    @ONLY
)

target_include_directories(chronon3d_render_plan
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
)

target_include_directories(chronon3d_render_plan_compiler
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chronon3d_render_plan
    PUBLIC
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator::validator
        chronon3d_core_impl
)

target_link_libraries(chronon3d_render_plan_compiler
    PUBLIC
        nlohmann_json::nlohmann_json
        chronon3d_core_impl
)

# Propagation: chronon3d_render_plan is intentionally NOT linked into
# chronon3d_core (would require it in the SDK export set — see
# Chronon3DTargets install/export wiring).  Instead, the validator's
# OBJECT files reach consumers via direct link:
#   - in-tree tests: tests/c_abi_tests.cmake LINK_TARGETS
#   - C API SHARED lib (chronon3d_c): see
#     cmake/Chronon3DSdkTargets.cmake — the C API SHARED is the only
#     SDK consumer that actually needs the validator (the C++ SDK
#     surface compiles plans in src/sdk_impl_marker.cpp + render_graph
#     internals which don't touch JSON parsing).
