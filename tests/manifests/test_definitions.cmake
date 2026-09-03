# Single ordered manifest of per-area test definition files.
set(CHRONON3D_TEST_DEFINITIONS
    mesh_disabled_gate.cmake
    backends_software_tests.cmake
    debug/CMakeLists.txt
    # Architecture include-graph + asset/backend hygiene gates (Python guards
    # + script linting). Dropped from registration by the slim top-level
    # orchestrator refactor (fd776350) and re-wired here.
    architecture_tests.cmake
    ocio_tests.cmake
    core_tests.cmake
    backend_registry_tests.cmake
    shader_abi_tests.cmake
    ipc_tests.cmake
    memory_tests.cmake
    c_abi_tests.cmake
    scene_tests.cmake
    cli_tests.cmake
    introspection_tests.cmake
    render_job_contract_tests.cmake
    optimizer_tests.cmake
    preflight_tests.cmake
    cache_tests.cmake
    compositor_tests.cmake
    benchmarks.cmake
    animation_tests.cmake
    renderer_tests.cmake
    io_tests.cmake
    breathing_golden_tests.cmake
    visual_tests.cmake
    graphics_tests.cmake
    gradient_visual_tests.cmake
    rounded_rect_visual_tests.cmake
    deterministic_tests.cmake
    text_production_v1_tests.cmake
    text_health_tests.cmake
    text_fallback_tests.cmake
    # LEGACY AUTHORING GOLDEN: this matrix still depends on the removed
    # TextPresetRegistry contract. Keep the sources for later v2 migration,
    # but do not register the obsolete authoring test in the standard build.
    # Legacy authoring golden matrices are intentionally excluded until their
    # useful assertions are migrated to concrete RenderPlan v2 fixtures.
    cache/parse_framebuffer_pool_clear_policy_tests.cmake
    diagnostic_overlay_tests.cmake
    precomp_focus_tests.cmake
    timeline_tests.cmake
    timeline_functional_v1_tests.cmake
    text_definition_tests.cmake
    safe_area_placement_tests.cmake
    text_rich_authoring_tests.cmake
    # Legacy subtitle authoring/registry tests are excluded with the same
    # migration gate; coverage must be re-authored against RenderPlan v2.
    animation_helpers_tests.cmake
    text/text_clip_policy_tests.cmake
    text_layout_advanced_tests.cmake
    bench_corpus/CMakeLists.txt
    video_tests.cmake
    media_tests.cmake
    sdk_tests.cmake
    simd/simd_parity_blend_tests.cmake
    simd/cpu_isa_tests.cmake
    render_graph/compiler/fusion_pass_tests.cmake
    render_graph/compiler/template_program_tests.cmake
    render_graph/compiler/segment_execution_tests.cmake
    runtime/template_program_cache_tests.cmake
    runtime/gpu_layer_batch_tests.cmake
    runtime/gpu_command_plan_tests.cmake
    runtime/resource_state_tracker_tests.cmake
    runtime/async_encoder_sink_tests.cmake
    runtime/semantic_core_tests.cmake
    render_graph/pipeline/glow_fullframe_audit_tests.cmake
    sabotage_tests.cmake
    # CapCut-grade parity test (TICKET-CAPCUT-REFERENCE-CORPUS, FU09 verdict CapCut-grade §Fase 9)
    reference/capcut/CMakeLists.txt
    # Isolated alignment + auto-fit regression locks (TICKET-ISOLATED-ALIGNMENT-TESTS, FU07 verdict CapCut-grade §Fase 7)
    text/CMakeLists.txt
    assets/CMakeLists.txt
    ipc_schema_documents_tests.cmake
)
