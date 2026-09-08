add_library(chronon3d_compiler OBJECT
    compiler.cpp
    compiler_graph.cpp
    compiler_pass_pipeline.cpp
    program_sampler.cpp
    typed_program_sampler.cpp
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
    graph_builder/graph_builder_static_analysis.cpp
    graph_builder/graph_builder_bbox.cpp
    graph_builder/graph_builder_matte.cpp
    graph_builder/graph_builder_layer_pipeline.cpp
    graph_builder/passes/graph_builder_layer_passes.cpp
    graph_builder/passes/graph_builder_lighting_passes.cpp
    graph_builder/passes/graph_builder_source_pass.cpp
    graph_builder/passes/graph_builder_resolve_pass.cpp
    graph_builder/passes/graph_builder_root_sources_pass.cpp
    graph_builder/passes/graph_builder_layer_pipeline_pass.cpp
    graph_builder/passes/graph_builder_output_pass.cpp
    graph_builder/passes/graph_builder_validation_pass.cpp
    graph_builder/graph_build_registry.cpp
    graph_builder/precomp_builder_service.cpp
    graph/graph_compile_pipeline.cpp
    graph_compiler/frame_graph_builder.cpp
    graph_compiler/scene_binding_compiler.cpp
    graph_compiler/fused_pixel_program.cpp
    graph_compiler/compiled_template_program.cpp
    optimizer/graph_optimizer.cpp
)
set_target_properties(chronon3d_compiler PROPERTIES UNITY_BUILD OFF)
set_source_files_properties(
    graph_compiler/frame_graph_builder.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

target_include_directories(chronon3d_compiler
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_SOURCE_DIR}/src/render_graph
            ${CMAKE_SOURCE_DIR}/src/render_graph/nodes
            ${CMAKE_SOURCE_DIR}/src/render_graph/builder
            ${CMAKE_SOURCE_DIR}/src/render_graph/compiler
            ${CMAKE_SOURCE_DIR}/src/compiler/graph_builder
            ${CMAKE_SOURCE_DIR}/src/compiler/graph_builder/passes
            ${CMAKE_SOURCE_DIR}/src/compiler/graph_compiler
)

target_link_libraries(chronon3d_compiler
    PUBLIC
        chronon3d_graph_core
        chronon3d_graph_nodes
)
