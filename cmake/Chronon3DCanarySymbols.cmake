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
# ==============================================================================# CANARY ENTRY VERIFICATION STATUS (re-verified 2026-06-29 from source-tree
# evidence; real nm -C cross-check BLOCKED until Phase 1/8 target_sources
# fix lands — see header caveat above):
#
# VERIFIED via src-header grep:
#   [1] core           → chronon3d::detail::system_metrics_parse (found in src/core/system_metrics_parse.hpp, NS=chronon3d::detail)
#   [2] animations     → chronon3d::temporal::generate_temporal_samples  (NS=chronon3d::temporal)
#   [3] scene          → chronon3d::camera_v1::register_camera_v1_builtins  (NS=chronon3d::camera_v1)
#   [4] graph          → chronon3d::graph::register_pipeline_graph_nodes (NS=chronon3d::graph)
#   [7] text_core      → chronon3d::glyph_atlas_lookup  (NS=chronon3d, no text_core:: sub-namespace)
#
# BEST-EFFORT (no direct grep hit; deferred to Fase 5 nm -C validation):
#   [5] runtime        → chronon3d::render_session (class RenderSession at src/runtime/render_session.cpp; NS=chronon3d)
#   [6] backend_software → chronon3d::backends::software::SoftwareRenderer::set_diagnostic_mode (member fn, src/backends/software/software_renderer.cpp:82)
#   [8] diagnostics    → chronon3d::EffectCatalog::freeze (registry header mirror pattern; src/registry/effect_catalog.cpp existence unverified in current checkout)
#   [9] content        → chronon3d::register_content_modules (root-level symbol from content/register_content_modules.cpp)
#
# MAINTENANCE: when Phase 1/8 lands and build is green, run:
#   for s in $(grep -oE '"[A-Za-z0-9_:.+]+"' cmake/Chronon3DCanarySymbols.cmake | tr -d '"'); do
#     nm -C libchronon3d_sdk_impl.a | grep -F "$s" || echo "MISS: $s"; done
# Any MISS line replaces the canary with the closest real emission from the same area.
set(CHRONON3D_SDK_CANARY_SYMBOLS
    # #area=core #lib=chronon3d_core_impl
    "chronon3d::detail::system_metrics_parse"

    # #area=animations #lib=chronon3d_animations
    "chronon3d::temporal::generate_temporal_samples"

    # #area=scene #lib=chronon3d_scene
    "chronon3d::camera_v1::register_camera_v1_builtins"

    # #area=runtime #lib=chronon3d_runtime (best-effort: class name on flat NS=chronon3d)
    "chronon3d::render_session"

    # #area=graph #lib=chronon3d_graph_pipeline
    "chronon3d::graph::register_pipeline_graph_nodes"

    # #area=software_backend #lib=chronon3d_backend_software (best-effort member-fn symbol)
    "chronon3d::backends::software::SoftwareRenderer::set_diagnostic_mode"

    # #area=text_core #lib=chronon3d_text_core (NS=chronon3d, NOT chronon3d::text_core)
    "chronon3d::glyph_atlas_lookup"

    # #area=diagnostics #lib=chronon3d_diagnostics (best-effort registry mirror)
    "chronon3d::EffectCatalog::freeze"

    # #area=content #lib=chronon3d_content (best-effort root-level symbol)
    "chronon3d::register_content_modules"
)
