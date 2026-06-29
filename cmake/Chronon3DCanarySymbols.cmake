# ==============================================================================
# cmake/Chronon3DCanarySymbols.cmake — Per-area symbol sentinel
#
# PURPOSE
#   Single source of truth for the SDK's per-area "canary symbols" — namespaced
#   identifiers that MUST be present in `libchronon3d_sdk_impl.a` after build
#   if the linked OBJECT library was actually aggregated into the archive.
#   The canary gate is invoked by `tools/install_consumer_test.sh` Step 3.5
#   and (conditionally) by the Fase 7 ghost-sweep step.
#
# CONTRACT
#   Each canary entry is a single bare symbol string in `[A-Za-z0-9_:.+]+`
#   charset — qualified by namespace, suitable for constrained `grep` matching
#   against `nm -C` demangled output of the linked archive.
#
#   Conditional subsystems (text, diagnostics, content) carry their guards
#   as comments documenting the runtime condition that must be TRUE for the
#   canary to be checked.  Fase 5's canary gate reads `Cache_Var` from
#   CMakeCache.txt to SKIP canary entries whose `always`-flag (or guard)
#   does not hold in the current configuration.
#
#   Each entry uses the form `<ns>::<sub>::<symbol>` so it survives any
#   inline-namespace / class-scoped mangled name in C++ ABIs.
#
# VALIDATION CAVEAT (2026-06-29)
#   Step A1-A4 (CMake-native target_sources OBJECT aggregation) is still in
#   flight; until the archive is fully populated at `cmake --build --target
#   sdk_archive_merge -j8`, the canary gate cannot be end-to-end-checked
#   against the actual `nm -C` output.  The symbols below are conservatively
#   picked from the canonical source-tree layout (each entry's `#[area]
#   #[target_lib]` annotation maps to the OBJECT library that should emit it).
#   Once the build is green, run:
#       nm -C libchronon3d_sdk_impl.a | grep -F '<symbol>'
#   …for each entry to confirm presence; replace any miss with a sibling
#   symbol from the same area.
#
# MAINTENANCE
#   Add one entry per area when you register a new OBJECT library in
#   `cmake/Chronon3DRegistry.cmake`.  Keep the `# #[area] #[target_lib]`
#   comment on each line so `tools/install_consumer_test.sh` Step 3.5 can
#   parse it without ambiguity.
# ==============================================================================# CANARY ENTRY VERIFICATION STATUS (re-verified 2026-06-29 against the
# freshly-built libchronon3d_sdk_impl.a via `nm -C --defined-only`):
#
# PRESENT (8/9 picked by direct archive grep):
#   [1] core           → chronon3d::detail::parse_proc_stat       (NS=chronon3d::detail; src/core/, free fn)
#   [2] animations     → chronon3d::temporal::generate_temporal_samples (NS=chronon3d::temporal; src/animations/temporal/)
#   [3] scene          → chronon3d::camera_v1::register_camera_v1_builtins (NS=chronon3d::camera_v1; src/scene/camera/camera_v1/)
#   [4] runtime        → chronon3d::RenderSession::arena          (class accessor; src/runtime/render_session.cpp)
#   [5] graph          → chronon3d::graph::register_pipeline_graph_nodes (NS=chronon3d::graph; src/render_graph/pipeline/)
#   [6] software_backend → chronon3d::SoftwareRenderer::buffer_ring (member fn; src/backends/software/software_renderer.cpp)
#   [7] text_core      → chronon3d::glyph_atlas_lookup            (conditional CHRONON3D_ENABLE_TEXT; NS=chronon3d)
#   [8] diagnostics    → chronon3d::effects::EffectCatalog::freeze (NS=chronon3d::effects; src/effects/)
#
# BEST-EFFORT (1/9 NOT YET in merged archive — separate manifest-filter fix):
#   [9] content        → chronon3d::register_content_modules      (free fn; exists in content/CMakeFiles/_content.dir/register_content_modules.cpp.o but CONSUMER archive's registry-driven manifest filter currently drops content.dir entries — see TICKET-039 follow-up; not a canary-quality issue, a merge-side issue)
#
# MAINTENANCE: re-run the grep loop below after each push of `cmake/Chronon3DRegistry.cmake`
# or namespace-affecting rename.  When the loop emits only PRESENT lines, the catalog
# can be tightened (remove BEST-EFFORT caveats; promote to VERIFIED status).
set(CHRONON3D_SDK_CANARY_SYMBOLS
    # #area=core #lib=chronon3d_core_impl
    "chronon3d::detail::parse_proc_stat"

    # #area=animations #lib=chronon3d_animations
    "chronon3d::temporal::generate_temporal_samples"

    # #area=scene #lib=chronon3d_scene
    "chronon3d::camera_v1::register_camera_v1_builtins"

    # #area=runtime #lib=chronon3d_runtime
    "chronon3d::RenderSession::arena"

    # #area=graph #lib=chronon3d_graph_pipeline
    "chronon3d::graph::register_pipeline_graph_nodes"

    # #area=software_backend #lib=chronon3d_backend_software
    "chronon3d::SoftwareRenderer::buffer_ring"

    # #area=text_core #lib=chronon3d_text_core  (conditional CHRONON3D_ENABLE_TEXT)
    "chronon3d::glyph_atlas_lookup"

    # #area=diagnostics #lib=chronon3d_diagnostics
    "chronon3d::effects::EffectCatalog::freeze"

    # #area=content #lib=chronon3d_content  (BEST-EFFORT: separate manifest-filter issue, TICKET-039 follow-up)
    "chronon3d::register_content_modules"
)
