# src/text/CMakeLists.txt — Core text module (non-backend-specific)
# Exposes: chronon3d_text_core

add_library(chronon3d_text_core OBJECT
    animated_text_document.cpp
    animation/glyph_instance_state.cpp
    animation/text_animator_evaluator.cpp
    animation/text_animator_compile.cpp
    animation/text_animator_stack.cpp
    animation/text_property_applier.cpp
    animation/text_tracking_evaluator.cpp
    animation/text_pre_shaping.cpp
    every_line_composer.cpp
    glyph_selector.cpp
    glyph_selector_math.cpp
    glyph_selector_compile.cpp
    glyph_selector_random.cpp
    path_sampler.cpp
    pending_text_run_impl.cpp
    single_line_composer.cpp
    text_document.cpp
    text_document_builder.cpp
    timed_text_document.cpp
    timed_text_adapter_srt.cpp
    timed_text_adapter_vtt.cpp
    timed_text_adapter_json.cpp
    text_path_composer.cpp

    resolver/text_run_resolver.cpp
    resolver/text_span_resolver.cpp
    resolver/text_bidi_resolver.cpp
    resolver/text_font_resolver.cpp

    text_run_hash.cpp
    text_layout_cache.cpp
    text_layout_identity.cpp
    text_run_builder.cpp

    # Canonical compile_text_layout stage implementations.
    compiler/text_compile_validation.cpp
    compiler/text_run_shaping.cpp
    compiler/text_run_composition.cpp
    compiler/text_font_span_builder.cpp
    compiler/text_compile_orchestrator_helpers.cpp
    compiler/text_document_compiler.cpp

    text_run_driver.cpp
    driver/text_state_sampler.cpp
    driver/text_font_state.cpp
    driver/text_layout_rebuild.cpp
    text_run_geometry.cpp
    text_visibility_audit.cpp
    alpha_bbox_scanner.cpp
    text_visibility_reporting.cpp
    text_definition.cpp
    resolve_text_placement.cpp
    text_scramble_helpers.cpp
    unicode/utf8_decoder.cpp
    unicode/whitespace.cpp
    boundary_resolver/text_unit_map.cpp
    boundary_resolver/text_unit_map_lookups.cpp
    internal/text_resolver_helpers.cpp
)

# These sources must remain separate TUs because they contain colliding
# anonymous-namespace helpers or intentionally distinct TextUnitMap models.
set_source_files_properties(
    timed_text_document.cpp
    boundary_resolver/text_unit_map.cpp
    boundary_resolver/text_unit_map_lookups.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

target_link_libraries(chronon3d_text_core
    PUBLIC chronon3d
    PRIVATE chronon3d_cache
)

target_include_directories(chronon3d_text_core PRIVATE ${CMAKE_SOURCE_DIR})
