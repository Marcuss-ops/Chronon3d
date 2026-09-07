add_library(chronon3d_compiler OBJECT
    compiler.cpp
    graph_compilation.cpp
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
    ../render_plan/render_plan_compiler.cpp
    ../render_plan/render_plan_compiler_animation.cpp
    ../render_plan/render_plan_compiler_text.cpp
    ../render_plan/render_plan_compiler_fingerprint.cpp
    ../render_graph/builder/graph_builder_pipeline.cpp
    ../render_graph/builder/graph_builder_static_analysis.cpp
    ../render_graph/builder/graph_builder_bbox.cpp
    ../render_graph/builder/graph_builder_matte.cpp
    ../render_graph/builder/graph_builder_layer_pipeline.cpp
    ../render_graph/builder/graph_build_pipeline.cpp
    ../render_graph/builder/passes/graph_builder_layer_passes.cpp
    ../render_graph/builder/passes/graph_builder_lighting_passes.cpp
    ../render_graph/builder/passes/graph_builder_source_pass.cpp
    ../render_graph/builder/passes/graph_builder_resolve_pass.cpp
    ../render_graph/builder/passes/graph_builder_root_sources_pass.cpp
    ../render_graph/builder/passes/graph_builder_layer_pipeline_pass.cpp
    ../render_graph/builder/passes/graph_builder_output_pass.cpp
    ../render_graph/builder/passes/graph_builder_validation_pass.cpp
    ../render_graph/builder/graph_build_registry.cpp
    ../render_graph/builder/precomp_builder_service.cpp
    ../render_graph/compiler/frame_graph_compiler.cpp
    ../render_graph/compiler/frame_graph_builder.cpp
    ../render_graph/compiler/scene_binding_compiler.cpp
    ../render_graph/compiler/fused_pixel_program.cpp
    ../render_graph/compiler/compiled_template_program.cpp
    ../render_graph/optimizer/graph_optimizer.cpp
)
set_target_properties(chronon3d_compiler PROPERTIES UNITY_BUILD OFF)
set_source_files_properties(
    ../render_plan/render_plan_compiler.cpp
    ../render_graph/compiler/frame_graph_builder.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

target_include_directories(chronon3d_compiler
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_SOURCE_DIR}/src/render_plan
            ${CMAKE_BINARY_DIR}/src/render_plan
            ${CMAKE_SOURCE_DIR}/src/render_graph
            ${CMAKE_SOURCE_DIR}/src/render_graph/nodes
            ${CMAKE_SOURCE_DIR}/src/render_graph/builder
            ${CMAKE_SOURCE_DIR}/src/render_graph/compiler
)

# The transitional facade consumes the existing plan lowering implementation.
target_link_libraries(chronon3d_compiler
    PUBLIC
        chronon3d_render_plan
        chronon3d_graph_core
        chronon3d_graph_nodes
)
