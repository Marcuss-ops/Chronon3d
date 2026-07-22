# Blend2D/Text-dependent sources for chronon3d_core_tests.
set(CORE_BLEND2D_TESTS "")
if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    list(APPEND CORE_BLEND2D_TESTS
        text/test_text_layout.cpp
        text/test_text_bounds.cpp
        text/test_text_cache_key.cpp
        text/test_text_style_presets.cpp
        text/test_font_engine.cpp
        text/test_text_quality_glyph.cpp
        text/test_text_quality_shaping.cpp
        text/test_text_quality_tracking.cpp
        text/test_text_quality_arabic.cpp
        text/test_text_shaping_corpus.cpp
        text/test_text_bidi.cpp
        text/test_text_font_determinism.cpp
        text/test_resolve_placed_glyph_run_cluster_golden.cpp
        text/test_text_font_resolver_golden.cpp
        text/test_text_unit_map.cpp
        text/test_text_unit_map_8level.cpp
        text/test_selector_shapes.cpp
        text/test_selector_evaluate.cpp
        text/test_selector_combine.cpp
        text/test_effective_text_state.cpp
        text/test_freetype_face_cache_concurrency.cpp
        text/test_text_pool_concurrency.cpp
        text/test_font_io_fence.cpp
        text/test_crossfade_stroke.cpp
        text/test_draw_text_run_crossfade_stroke.cpp
        text/test_draw_text_run_scratch_state.cpp
        text/test_glyph_atlas_metadata.cpp
        text/test_compile_text_layout_errors.cpp
        text/test_compile_text_layout_identity.cpp
        text/test_auto_fit_font_size.cpp
        text/test_rich_text_paragraph_preservation.cpp
        text/test_compile_text_layout_per_paragraph_failure.cpp
        text/test_text_unit_map_joint_include.cpp
        text/test_layout_cache_collision.cpp
        text/test_font_identity_contract.cpp
        text/test_character_offset_pre_shaping.cpp
        text/test_text_document_builder.cpp
        text/test_timed_text_document.cpp
        text/test_text_definition.cpp
        text/test_anim_typewriter_error_path.cpp
        text/test_typewriter_cluster_window.cpp
    )
endif()
