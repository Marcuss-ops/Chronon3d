# ==============================================================================
# src/render_plan/CMakeLists.txt — chronon3d_render_plan
# ==============================================================================

add_library(chronon3d_render_plan OBJECT
    render_plan_decoder.cpp
    render_plan_decoder_support.cpp
    render_plan_decoder_layer.cpp
    render_plan_decoder_animation.cpp
    render_plan_decoder_shape.cpp
    render_plan_decoder_mask.cpp
    render_plan_decoder_text.cpp
    render_budget.cpp
    spring_bake.cpp
    shape_path_generator.cpp
    fingerprint.cpp
    render_plan_validator.cpp
    validation/json_schema_diagnostic_adapter.cpp
    render_plan_compiler.cpp
    render_plan_compiler_lottie.cpp
    render_plan_compiler_animation.cpp
    render_plan_compiler_text.cpp
    render_plan_compiler_fingerprint.cpp
    audio_react_baker.cpp
    audio_mix_compiler.cpp
)
set_target_properties(chronon3d_render_plan PROPERTIES UNITY_BUILD OFF)
if(CHRONON3D_ENABLE_SVG)
    target_compile_definitions(chronon3d_render_plan PRIVATE CHRONON3D_ENABLE_SVG=1)
endif()
if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    target_link_libraries(chronon3d_render_plan PRIVATE chronon3d_ffmpeg_full)
endif()
set_source_files_properties(
    render_plan_compiler.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

set(CHRONON3D_RENDER_PLAN_SCHEMA_FILE
    ${CMAKE_SOURCE_DIR}/schemas/json/chronon.render-plan.v2.schema.json)
set(CHRONON3D_RENDER_PLAN_V3_EXTENSIONS_SCHEMA_FILE
    ${CMAKE_SOURCE_DIR}/schemas/json/chronon.render-plan.v3.extensions.schema.json)
# These schemas are embedded at configure time; make edits to either source
# schema invalidate the generated header instead of leaving validation stale.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CHRONON3D_RENDER_PLAN_SCHEMA_FILE}"
    "${CHRONON3D_RENDER_PLAN_V3_EXTENSIONS_SCHEMA_FILE}")
file(READ "${CHRONON3D_RENDER_PLAN_SCHEMA_FILE}"
     CHRONON3D_RENDER_PLAN_SCHEMA_JSON)
file(READ "${CHRONON3D_RENDER_PLAN_V3_EXTENSIONS_SCHEMA_FILE}"
     CHRONON3D_RENDER_PLAN_V3_EXTENSIONS_SCHEMA_JSON)
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

target_link_libraries(chronon3d_render_plan
    PUBLIC
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator::validator
        chronon3d_core_impl
    PRIVATE
        spdlog::spdlog_header_only
)

# Propagation: chronon3d_render_plan is intentionally NOT linked into
# chronon3d_core. Its object files are attached explicitly to consumers that
# expose render-plan decoding/validation.
