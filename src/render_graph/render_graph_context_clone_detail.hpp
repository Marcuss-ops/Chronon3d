// =============================================================================
// render_graph_context_clone.inc — Context cloning and asset-path resolution.
// =============================================================================

#include <chronon3d/render_graph/render_graph_context.hpp>

namespace chronon3d::graph {

RenderGraphContext RenderGraphContext::clone_for_node_execution() const {
    // OPT-CLONE-BORROW — the per-node clone is the executor's most repeated
    // operation. Every shared_ptr field copied here costs an atomic refcount
    // RMW on a cache line that parallel node workers then bounce between
    // cores.
    //
    // `frame_input.static_context` has frame-scoped lifetime by contract: it
    // is assigned ONCE at context setup (scene_context_setup.cpp) and never
    // mutated per frame, and the parent context outlives every clone (node
    // execution is synchronous inside execute_internal's frame scope). The
    // executor's single-node path therefore repoints the clone's
    // static_context to a borrowed view in node_runner_single_node_detail.hpp
    // (FrameInput::static_context_ptr) instead of paying the refcount here.
    // This generic clone keeps value semantics: it is also used by paths that
    // outlive the parent frame scope (nested precomp execution).
    RenderGraphContext copy;
    copy.frame_input = frame_input;
    copy.policy      = policy;
    copy.backend = backend;
    copy.surface_registry = surface_registry;
    copy.text_render_resources = text_render_resources;
    copy.gpu_asset_cache = gpu_asset_cache;
    copy.gpu_glyph_atlas = gpu_glyph_atlas;
    copy.gpu_styled_glyph_cache = gpu_styled_glyph_cache;
    copy.gpu_text_prepared_cache = gpu_text_prepared_cache;
    copy.image_cache = image_cache;
    copy.node_cache = node_cache;
    copy.framebuffer_pool = framebuffer_pool;
    copy.registry = registry;
    copy.video_decoder = video_decoder;
    copy.effect_catalog = effect_catalog;
    copy.transition_catalog = transition_catalog;
    copy.precomp_builder = precomp_builder;
    copy.execution = execution;
    copy.frame_parameters = frame_parameters;
    copy.build_dependencies = build_dependencies;
    copy.registry_generation = registry_generation;
    copy.processor_snapshot_required = processor_snapshot_required;
    copy.runtime = runtime;
    copy.compiled_graph_cache = compiled_graph_cache;
    copy.scheduler = scheduler;
    copy.asset_resolver = asset_resolver;
    copy.session = session;
    copy.program_fingerprint = program_fingerprint;

    copy.node_exec.counters         = node_exec.counters;
    copy.node_exec.clip_rect        = node_exec.clip_rect;
    copy.node_exec.active_tile_clip = node_exec.active_tile_clip;
    copy.node_exec.dirty_rect       = node_exec.dirty_rect;
    copy.node_exec.shared_dof_depth = node_exec.shared_dof_depth
        ? node_exec.shared_dof_depth
        : const_cast<std::vector<float>*>(&node_exec.dof_depth);
    copy.node_exec.shared_dof_source_coverage =
        node_exec.shared_dof_source_coverage
            ? node_exec.shared_dof_source_coverage
            : const_cast<DofSourceCoverage*>(&node_exec.dof_source_coverage);
    copy.node_exec.current_identity = node_exec.current_identity;
    copy.node_exec.planned_physical_slot = node_exec.planned_physical_slot;
    copy.node_exec.terminal_video_encode_physical_slot =
        node_exec.terminal_video_encode_physical_slot;
    copy.node_exec.terminal_video_encode_surface = node_exec.terminal_video_encode_surface;
    copy.node_exec.sampled_layer = node_exec.sampled_layer;
    copy.node_exec.current_shape_processor = node_exec.current_shape_processor;
    copy.node_exec.current_shape_processors = node_exec.current_shape_processors;
    copy.node_exec.current_effect_processors = node_exec.current_effect_processors;
    copy.node_exec.processor_snapshot = node_exec.processor_snapshot;
    copy.node_exec.processor_bindings_compiled = node_exec.processor_bindings_compiled;
    // Per-node scratch views intentionally remain default-initialized.
    copy.frame_error = frame_error;  // non-owning: same frame-scoped slot
    return copy;
}

std::string RenderGraphContext::resolve_asset(const std::string& relative_path) const {
    if (relative_path.empty()) return relative_path;
    if (!relative_path.empty() && relative_path[0] == '/') return relative_path;
    const auto& root = frame_input.assets_root();
    if (root.empty()) return relative_path;
    if (root.back() == '/') {
        return root + relative_path;
    }
    return root + "/" + relative_path;
}

} // namespace chronon3d::graph
