#include "scene_tile_execution.hpp"

// TICKET-038/TXT-00 — canonical include for sw_renderer->runtime().
// RenderRuntime (in <chronon3d/runtime/render_runtime.hpp>) is the SOLE
// owner of GraphExecutor per TICKET-011.  Calling
// sw_renderer->runtime().executor().execute(...) below
// needs the FULL type of RenderRuntime (the .executor() accessor +
// the GraphExecutor& return — only declared in the full type).
// graph_executor.hpp transitively forward-declares RenderRuntime; the
// include here resolves the type for this TU.  See commit 91debc36
// (TXT-00 closure of ROT 1 on src/render_graph/pipeline/) for the
// duplicate-fix precedent — the audit-comment block above mirrors
// the wording exactly.
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>     // PR 6.4 — typed scope plumbing
#include <chronon3d/core/memory/arena.hpp>              // PR 6.4 — explicit child FrameArena
#include <algorithm>
#include <stdexcept>

namespace chronon3d::graph::detail {

// ── Execute a single dirty region: set up clip context, run graph, copy to output.
[[nodiscard]] static TileExecutionResult execute_single_dirty_region(
    CompiledFrameGraph& compiled,
    const RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer,
    const raster::BBox& region_bbox,
    Framebuffer& output_fb,
    const ExecutionScope& root_scope
) {
    RenderGraphContext tile_ctx = ctx;
    tile_ctx.node_exec.clip_rect = region_bbox;
    tile_ctx.node_exec.dirty_rect = region_bbox;
    // Per-tile execution must not recursively restore the previous surface;
    // the parent FrameExecutionPlan already selected SparseTiles and the
    // caller supplied the preserved destination framebuffer.
    tile_ctx.policy.reuse_prev_framebuffer = false;
    tile_ctx.policy.tile_execution_enabled = true;
    tile_ctx.node_exec.active_tile_clip = region_bbox;
    tile_ctx.policy.skip_initial_clear = false;
    tile_ctx.node_exec.early_exit_skip.clear();

    // PR 6.2 — root_scope is constructed once in render_scene_via_graph()
    // and threaded through.  Each tile region creates a child Tile scope
    // with its own FrameArena (per-region isolation) but borrows the
    // root scope's session (shared across all tile regions).  The
    // parent chain is: root_scope → tile_scope, so child teardown
    // (ArenaGuard on tile_scope.arena()) never touches the root arena.
    //
    // The sw_renderer gate selects the executor/scheduler (renderer-owned
    // vs local), not the session — both paths use root_scope.session().
    std::shared_ptr<Framebuffer> tile_fb;
    FrameArena child_arena;   // PR 6.4 — distinct child arena per region
    // FASE 5 closure: the 5-arg explicit ctor is now private.  Tile regions
    // are never recursive precomp loops (no owner_key), so the only realistic
    // `make_child` rejection here is `ScopeErrorCode::ChainLimitExceeded`.
    // Surface it as a zero-valued TileExecutionResult so the caller observes
    // the failure structurally (rather than via a deprecated ctor's silent
    // clamp).
    auto tile_scope_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, root_scope.session(),
        child_arena, compiled.graph_instance_id, &root_scope);
    if (!tile_scope_res) {
        return TileExecutionResult{
            .dirty_count     = 0,
            .pixels_rendered = 0ull
        };
    }
    ExecutionScope tile_scope = tile_scope_res.value();
    // Section 5 canonical-owner invariant: tile execution is selected only
    // when ExecutionResolver has a renderer with a runtime-owned executor.
    // Do not construct a second GraphExecutor here; a missing runtime is a
    // violated pipeline precondition and must fail loudly.
    if (!sw_renderer || !sw_renderer->has_runtime()) {
        throw std::logic_error(
            "tile execution requires the RenderRuntime-owned GraphExecutor");
    }
    auto& tile_scheduler = ctx.services.scheduler
        ? *ctx.services.scheduler
        : sw_renderer->scheduler();
    tile_fb = sw_renderer->runtime().executor().execute(
        compiled, tile_ctx, tile_scope,
        tile_scheduler).framebuffer;

    if (tile_fb) {
        for (i32 y = region_bbox.y0; y < region_bbox.y1; ++y) {
            std::copy(
                tile_fb->pixels_row(y) + region_bbox.x0,
                tile_fb->pixels_row(y) + region_bbox.x1,
                output_fb.pixels_row(y) + region_bbox.x0);
        }
    }

    return TileExecutionResult{
        .dirty_count = 1,
        .pixels_rendered =
            static_cast<uint64_t>(region_bbox.x1 - region_bbox.x0) *
            static_cast<uint64_t>(region_bbox.y1 - region_bbox.y0)
    };
}

TileExecutionResult execute_dirty_tiles(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer,
    const FrameExecutionPlan& execution_plan,
    Framebuffer& output_fb,
    i32 width,
    i32 height,
    bool parallel,
    ExecutionScope& root_scope
) {
    if (!execution_plan.dirty_tiles || execution_plan.dirty_regions.empty()) {
        return TileExecutionResult{};
    }

    // The resolver has already coalesced the FrameDeltaCompiler tile mask.
    // Execution consumes the immutable plan and must not recalculate it.
    const auto& dirty_tiles = *execution_plan.dirty_tiles;
    const auto& regions = execution_plan.dirty_regions;
    const int original_dirty = dirty_tiles.dirty_count();
    const int coalesced_count = static_cast<int>(regions.size());

    TileExecutionResult result;
    result.regions_executed = coalesced_count;

    // PR-1 — use the authoritative scheduler's for_each_index() instead
    // of the raw tbb parallel_for.  The scheduler handles Sequential/TbbFixed/
    // TbbAutomatic modes internally.  When scheduler is null (test paths
    // without a wired scheduler), fall back to sequential execution.
    if (ctx.services.scheduler && parallel && coalesced_count > 1) {
        std::vector<uint64_t> per_region(regions.size(), 0);
        ctx.services.scheduler->for_each_index(
            regions.size(), [&](std::size_t i) {
                if (regions[i].is_empty()) return;
                auto region_result = execute_single_dirty_region(
                    compiled, ctx, sw_renderer, regions[i], output_fb, root_scope);
                per_region[i] = region_result.pixels_rendered;
            });
        for (auto px : per_region) {
            result.pixels_rendered += px;
        }
    } else {
        for (const auto& region : regions) {
            if (region.is_empty()) continue;
            auto region_result = execute_single_dirty_region(
                compiled, ctx, sw_renderer, region, output_fb, root_scope);
            result.pixels_rendered += region_result.pixels_rendered;
        }
    }

    // Report original tile count for performance counters.
    result.dirty_count = original_dirty;

    return result;
}

} // namespace chronon3d::graph::detail
