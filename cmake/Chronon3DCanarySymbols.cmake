# ==============================================================================
# cmake/Chronon3DCanarySymbols.cmake — Per-area symbol sentinel
#
# PURPOSE
#   Single source of truth for the SDK's per-area "canary symbols" — namespaced
#   identifiers that MUST be present in `libchronon3d_sdk_impl.a` after build
#   if the linked OBJECT library was actually aggregated into the archive.
#   The canary gate is invoked by `tools/sdk/check_archive_canaries.sh` (Step 3.5)
#   and (conditionally) by `tools/sdk/check_feature_ghosts.sh` (Step 2.5).
#
# CONTRACT (Phase-1.2 update — 4-tuple AREA|SYMBOL|GUARD|TARGET)
#   Each canary entry is a single quoted string in the form
#       "AREA|SYMBOL|GUARD|TARGET"
#   where:
#     AREA   ∈ [a-z_]+             canonical area name (lowercase + underscore)
#     SYMBOL ∈ [a-zA-Z0-9_:.]+     fully-qualified demangled symbol that
#                                  must substring-match against `nm -C`
#                                  demangled output of the linked archive
#     GUARD  ∈ {"always",
#               CHRONON3D_ENABLE_TEXT,
#               CHRONON3D_BUILD_DIAGNOSTICS,
#               CHRONON3D_BUILD_CONTENT}
#                                  if GUARD evaluates OFF (cached value of
#                                  the CMake variable is not "ON"), the
#                                  canary is SKIPPED — never failed
#     TARGET ∈ [a-zA-Z0-9_]+        cmake OBJECT library target that emits
#                                  the .o (for forensic traceability only;
#                                  the canary gate does not gate on it)
#
#   This 4-tuple format matches the `grep -oE` regex in
#   `tools/sdk/check_archive_canaries.sh:79`.  Pre-Phase-1.2 entries used a
#   1-field bare-symbol format that matched ZERO catalog entries and
#   forced the canary gate to exit 1 with "no canary entries parsed" —
#   that latent bug is resolved by this update.
#
# VALIDATION CAVEAT (2026-06-29)
#   Step A1-A4 (CMake-native target_sources OBJECT aggregation) is still in
#   flight; until the archive is fully populated at `cmake --build --target
#   chronon3d_sdk_impl -j8`, the canary gate cannot be end-to-end-checked
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
#   comment on each line so a future `tools/sdk/lint_canaries.py` can
#   cross-reference without ambiguity.
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
    # #area=core #lib=chronon3d_core_impl  # GUARD=always
    "core|chronon3d::detail::parse_proc_stat|always|chronon3d_core_impl"

    # #area=animations #lib=chronon3d_animations  # GUARD=always
    "animations|chronon3d::temporal::generate_temporal_samples|always|chronon3d_animations"

    # #area=scene #lib=chronon3d_scene  # GUARD=always
    "scene|chronon3d::camera_v1::register_camera_v1_builtins|always|chronon3d_scene"

    # #area=runtime #lib=chronon3d_runtime  # GUARD=always
    "runtime|chronon3d::RenderSession::arena|always|chronon3d_runtime"

    # #area=graph #lib=chronon3d_graph_pipeline  # GUARD=always
    "graph|chronon3d::graph::register_pipeline_graph_nodes|always|chronon3d_graph_pipeline"

    # #area=software_backend #lib=chronon3d_backend_software  # GUARD=always
    "software_backend|chronon3d::SoftwareRenderer::buffer_ring|always|chronon3d_backend_software"

    # #area=text_core #lib=chronon3d_text_core  # GUARD=CHRONON3D_ENABLE_TEXT
    # Skip when text is OFF (`CHRONON3D_ENABLE_TEXT != ON`); matches
    # the consumer's `try_compile(CHRONON3D_HAVE_TEXT_CORE, …)` probe
    # in `tests/install_consumer/CMakeLists.txt`.
    "text_core|chronon3d::glyph_atlas_lookup|CHRONON3D_ENABLE_TEXT|chronon3d_text_core"

    # #area=diagnostics #lib=chronon3d_diagnostics  # GUARD=CHRONON3D_BUILD_DIAGNOSTICS
    # Skip when diagnostics are OFF (`CHRONON3D_BUILD_DIAGNOSTICS != ON`);
    # companion to the OFF ghost-sweep in `tools/sdk/check_feature_ghosts.sh`
    # that asserts no diagnostics .cpp.o leaks into an OFF archive.
    "diagnostics|chronon3d::effects::EffectCatalog::freeze|CHRONON3D_BUILD_DIAGNOSTICS|chronon3d_diagnostics"

    # #area=content #lib=chronon3d_content  # GUARD=CHRONON3D_BUILD_CONTENT
    # Skip when content is OFF (`CHRONON3D_BUILD_CONTENT != ON`).
    # BEST-EFFORT: separate manifest-filter issue (TICKET-039 follow-up);
    # the consumer archive's registry-driven filter currently drops
    # content.dir entries from the merged archive, so this canary will
    # still be `FAIL` on consumer builds until the manifest filter is
    # fixed upstream.
    "content|chronon3d::register_content_modules|CHRONON3D_BUILD_CONTENT|chronon3d_content"

    # #area=sdk #lib=chronon3d_runtime  # GUARD=always
    # TICKET-GATE-10-PHASE-4-BLACK regression lock: substring matches all
    # ctor/dtor/render/set_* demangled symbols; gates the SDK archive
    # aggregation that the external consumer (Phase 4) needs to link.
    "sdk|chronon3d::sdk::RenderEngine|always|chronon3d_runtime"
)
