# ==============================================================================
# src/render_plan/CMakeLists.txt — chronon3d_render_plan
# ==============================================================================

add_library(chronon3d_render_plan OBJECT
    render_plan_decoder.cpp
    render_plan_decoder_support.cpp
    render_plan_decoder_layer.cpp
    render_budget.cpp
    fingerprint.cpp
    render_plan_validator.cpp
    validation/json_schema_diagnostic_adapter.cpp
)
set_target_properties(chronon3d_render_plan PROPERTIES UNITY_BUILD OFF)

add_library(chronon3d_render_plan_compiler OBJECT
    render_plan_compiler.cpp
    render_plan_compiler_animation.cpp
    render_plan_compiler_text.cpp
    render_plan_compiler_fingerprint.cpp
)
set_target_properties(chronon3d_render_plan_compiler PROPERTIES UNITY_BUILD OFF)
set_source_files_properties(
    render_plan_compiler.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

set(CHRONON3D_RENDER_PLAN_SCHEMA_FILE
    ${CMAKE_SOURCE_DIR}/schemas/json/chronon.render-plan.v2.schema.json)
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
# chronon3d_core. Its object files are attached explicitly to consumers that
# expose render-plan decoding/validation.
