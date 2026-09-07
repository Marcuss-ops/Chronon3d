add_library(chronon3d_compiler OBJECT
    compiler.cpp
    compiled_program.cpp
    compiled_program_validator.cpp
    frontend/json_decoder.cpp
    frontend/schema_validator.cpp
    lowering/animation_lowering.cpp
    lowering/text_lowering.cpp
    lowering/scene_lowering.cpp
    lowering/timeline_lowering.cpp
    graph/dependency_builder.cpp
    graph/pass_builder.cpp
    optimizer/static_optimizer.cpp
    fingerprint/program_fingerprint.cpp
)
set_target_properties(chronon3d_compiler PROPERTIES UNITY_BUILD OFF)

target_include_directories(chronon3d_compiler
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

# The transitional facade consumes the existing plan lowering implementation.
target_link_libraries(chronon3d_compiler
    PUBLIC
        chronon3d_render_plan
        chronon3d_render_plan_compiler
)
