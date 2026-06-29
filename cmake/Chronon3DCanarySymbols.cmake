# ==============================================================================
# cmake/Chronon3DCanarySymbols.cmake — Canary symbol catalog
#
# PURPOSE
#   One external-linkage C++ symbol per required SDK area.  Each entry is
#   stable in the libchronon3d_sdk_impl.a archive when the corresponding area
#   is compiled in.  Used by the Fase-5 verification gate (companion script)
#   to fail the build when a required area is missing its canary — a small
#   but high-signal check that complements the manifest count.
#
# FORMAT
#   CHRONON3D_SDK_CANARY_SYMBOLS is a semicolon-separated list of
#       "AREA|SYMBOL|GUARD|TARGET"
#   tuples:
#     - AREA   : short tag used in log/status messages (e.g. "core",
#                "render_graph", "text_core").
#     - SYMBOL : demangled C++ symbol that 'nm -C' should report when the
#                area is compiled.  Write it WITHOUT `()` or with `()` to
#                mean "function with default args / overload set" — the
#                gate uses substring match.
#     - GUARD  : one of:
#                  "always"                              → unconditional
#                  "CHRONON3D_ENABLE_TEXT"               → text_core only
#                  "CHRONON3D_ENABLE_TEXT_AND_BLEND2D"   → backend_text only
#                  "CHRONON3D_BUILD_DIAGNOSTICS"         → diagnostics only
#                Custom guards can be added in the gate.
#     - TARGET : registry target name (cmake/Chronon3DRegistry.cmake entry)
#                used to cross-reference ARCHIVE_OBJECT closure.
#
# SYMBOL-SELECTION RULES
#   * External-linkage C++ function (not inline, not static, namespace-scope
#     OR class-method on a public class).
#   * Distinct namespace when possible.  Where only the root `chronon3d`
#     namespace is available (core / text_core / backend_text), distinct
#     function names disambiguate.
#   * Emitted by .cpp files in that area (not just header-declared).
#   * Stable public API (NO `detail::` or `internal::` — registry-coupled).
#
# CONVENTIONS
#   * Each entry is preceded by `# #[area]` so a human can scan the catalog.
#   * Entries are kept in the order required by the user's spec to keep
#     diffs against reviews readable.
#   * Updating an existing canary = edit the SYMBOL line + add a follow-up
#     note (do NOT silently rename; one-line aliasing only).
# ==============================================================================

set(CHRONON3D_SDK_CANARY_SYMBOLS
    # #core ── target chronon3d_core_impl (src/core/)
    #   Free function.  Declared in
    #     include/chronon3d/core/composition/register_builtin_compositions.hpp:18
    #   Namespace: chronon3d.  Definition lives in src/core/composition/.
    "core|chronon3d::register_dark_grid_background|always|chronon3d_core_impl"

    # #animations ── target chronon3d_animations (src/animations/)
    #   Free function.  Declared in
    #     include/chronon3d/animation/temporal/temporal_samples.hpp:101
    #   Defined in
    #     src/animations/temporal/temporal_samples.cpp:96
    #   Distinct namespace: chronon3d::temporal.
    "animations|chronon3d::temporal::generate_temporal_samples|always|chronon3d_animations"

    # #scene ── target chronon3d_scene (src/scene/)
    #   Free function.  Declared in
    #     include/chronon3d/scene/camera/camera_v1/register_camera_v1.hpp:40
    #   Distinct namespace: chronon3d::camera_v1.
    "scene|chronon3d::camera_v1::register_camera_v1_builtins_into|always|chronon3d_scene"

    # #runtime ── target chronon3d_runtime (src/runtime/)
    #   TRUE free function in a distinct sub-namespace.  Declared in
    #     include/chronon3d/runtime/render_runtime.hpp:274
    #   Defined in
    #     src/runtime/render_runtime.cpp:178
    #     (file opens with `namespace chronon3d::runtime {` — confirmed
    #      via `^namespace` grep in src/runtime/)
    #   Distinct namespace: chronon3d::runtime.
    "runtime|chronon3d::runtime::set_process_wide_assets_root|always|chronon3d_runtime"

    # #render_graph ── target chronon3d_graph_pipeline (one of 8 graph_* registered)
    #   Free function.  Declared in
    #     include/chronon3d/render_graph/pipeline/pipeline_catalogs.hpp:71
    #   Defined in
    #     src/render_graph/pipeline/register_pipeline_nodes.cpp:106
    #   Distinct namespace: chronon3d::graph.
    "render_graph|chronon3d::graph::init_graph_pipeline_catalogs|always|chronon3d_graph_pipeline"

    # #software_backend ── target chronon3d_backend_software (src/backends/software/)
    #   Free function.  Declared in
    #     include/chronon3d/backends/software/builtin_processors.hpp:7
    #   Defined in src/backends/software/processors/builtin_processors.cpp.
    #   Used by the runtime at src/runtime/render_runtime.cpp:115 as
    #     renderer::register_builtin_processors(*m_owned_software_registry)
    #   Distinct namespace: chronon3d::renderer.
    "software_backend|chronon3d::renderer::register_builtin_processors|always|chronon3d_backend_software"

    # #text_core ── target chronon3d_text_core (src/text/) ── guard CHRONON3D_ENABLE_TEXT=ON
    #   Free function.  Declared in
    #     include/chronon3d/text/glyph_atlas.hpp:36
    #   Definition lives in src/text/glyph_atlas.cpp.
    #   Namespace: chronon3d.  Disambiguated by function name vs backend_text
    #   (also root chronon3d) and core.
    "text_core|chronon3d::glyph_atlas_lookup|CHRONON3D_ENABLE_TEXT|chronon3d_text_core"

    # #backend_text ── target chronon3d_backend_text (src/backends/text/) ── guard CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D
    #   Free function.  Declared in
    #     include/chronon3d/backends/text/text_rasterizer_ink.hpp:10
    #   Distinct by signature overload (BLImage& + boxed coords) vs text_core
    #   root-namespace neighbours.
    "backend_text|chronon3d::compute_text_ink_bbox|CHRONON3D_ENABLE_TEXT_AND_BLEND2D|chronon3d_backend_text"

    # #diagnostics ── target chronon3d_backend_software_diagnostics (src/backends/software/diagnostics/) ── guard CHRONON3D_BUILD_DIAGNOSTICS=ON
    #   Free function.  Declared in
    #     src/backends/software/diagnostics/nulls_overlay.hpp:20
    #   Definition lives in src/backends/software/diagnostics/nulls_overlay.cpp.
    #   Distinct namespace: chronon3d::renderer::diagnostics.
    "diagnostics|chronon3d::renderer::diagnostics::draw_null_overlay|CHRONON3D_BUILD_DIAGNOSTICS|chronon3d_backend_software_diagnostics"
)
