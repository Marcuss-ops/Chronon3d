// =============================================================================
// precomp_node_execute.cpp — PrecompNode::execute() (WP 5: identity-driven)
//
// WP 5 — PrecompNode no longer owns SceneProgramCache, GraphExecutor, or
// an auto-tune counter.  All of those are now centralised on the parent
// RenderSession (accessed via `ctx.services.session`).
//
// Flow:
//   0. Parent session must be wired; bail with empty FB otherwise.
//   1. Composition must exist in the registry.
//   2. Calculate nested frame time; bail out-of-range with empty FB.
//   3. Fetch nested composition & build nested context.
//   4. Compute SceneStructureKey for cache lookup.
//   5. WP 5.1 — derive PrecompInstanceKey from
//      `ctx.node_exec.current_identity` (PR 4.3-then-populated by
//      GraphExecutor).  Two siblings → distinct keys → distinct
//      SceneProgramStore buckets.
//   6. WP 5.0 — `session->program_store().acquire(instance_key, ...)`;
//      cache HIT: reuse; cache MISS: lambda compile via
//      PrecompBuilderService.
//   7. WP 5.0 — borrow executor from `session->services.executor`
//      (NO local `GraphExecutor local_executor`; GraphExecutor is
//      stateless and borrowed for the duration of this inner call).
//   8. Refresh per-frame payloads + warm up param block, then execute.
//
// WP 5.2 — `lease.program` is a `shared_ptr<CompiledSceneProgram>`.  An
// in-flight lease keeps the program alive even across a concurrent
// `clear()` of the store (the LRU dropping its reference does not drop
// ours).
// =============================================================================

#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/core/scene_hasher.hpp>
#include <chronon3d/render_graph/layer/layer_resolver.hpp>
#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>
#include <chronon3d/render_graph/builder/precomp_builder_service.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace chronon3d::graph {

// ── Constructor ───────────────────────────────────────────────────────────────

PrecompNode::PrecompNode(std::string comp_name, Frame start_frame, Frame duration,
                         Frame cache_frame, PrecompCachePolicy cache_policy)
    : RenderGraphNode(static_memory_cache("precomp"))
    , m_comp_name(std::move(comp_name))
    , m_full_name("Precomp:" + m_comp_name)
    , m_start_frame(start_frame)
    , m_duration(duration)
    , m_cache_frame(cache_frame)
    , m_cache_policy(std::move(cache_policy))
{
}

PrecompNode::~PrecompNode() = default;

// ── instance_key(ctx) ─────────────────────────────────────────────────────────

PrecompInstanceKey PrecompNode::instance_key(const RenderGraphContext& ctx) const noexcept {
    // WP 5.1 — precomp owner key is derived from the executor-driven
    // identity for the surrounding compiled graph.  `GraphExecutor`
    // populated `ctx.node_exec.current_identity` immediately before
    // calling this node's execute(); if the field is invalid (test
    // path that bypasses the executor) the key is the all-zero sentinel
    // and the SceneProgramStore will treat it as a single ambient bucket
    // — the test fixture must pre-set the field explicitly to drive
    // sibling-isolation assertions.
    return make_precomp_key(
        ctx.node_exec.current_identity.graph,
        ctx.node_exec.current_identity.node);
}

// ── set_on_evict() ───────────────────────────────────────────────────────────

void PrecompNode::set_on_evict(cache::ProgramEvictCallback cb) {
    m_on_evict = std::move(cb);
    // Review #3 — clear the bucket-pointer marker so the next execute()
    // re-forwards the (possibly new) callback onto whatever fresh bucket
    // acquire() produces.  Survives store-level `clear()` cycles because
    // execute() compares the new bucket pointer against this marker.
    m_on_evict_wired_to = nullptr;
}

// ── execute() ────────────────────────────────────────────────────────────────

OwnedFB PrecompNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>)
{
    // ── 0. Guard: parent session must be wired ──────────────────────────
    auto* session = ctx.services.session;
    if (!session) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    // ── 1. Guard: composition must exist in the registry ─────────────────
    if (!ctx.services.registry || !ctx.services.registry->contains(m_comp_name)) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    // ── 2. Calculate nested frame time ───────────────────────────────────
    const Frame nested_frame = ctx.frame_input.frame - m_start_frame;
    if (nested_frame < 0 || (m_duration > 0 && nested_frame >= m_duration)) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    // ── 3. Fetch nested composition & build nested context ───────────────
    const auto comp = ctx.services.registry->create(m_comp_name);

    RenderGraphContext nested_ctx = ctx;
    nested_ctx.frame_input.frame   = nested_frame;
    nested_ctx.frame_input.width   = comp.width();
    nested_ctx.frame_input.height  = comp.height();
    nested_ctx.frame_input.camera = comp.camera;

    const Scene nested_scene = comp.evaluate(nested_frame);

    // ── 4. Compute SceneStructureKey for cache lookup ────────────────────
    SceneHasher hasher;
    SceneStructureKey key;
    key.topology_hash      = hasher.compute_structure_fingerprint(nested_scene);
    key.active_set_hash    = hasher.compute_active_at_fingerprint(nested_scene, nested_frame);
    key.render_options_hash = hash_combine(0, static_cast<uint64_t>(nested_ctx.policy.ssaa_factor));
    key.width              = nested_ctx.frame_input.width;
    key.height             = nested_ctx.frame_input.height;
    key.ssaa_factor        = static_cast<int>(nested_ctx.policy.ssaa_factor);

    // ── 5. WP 5.1 — derive PrecompInstanceKey from the executor's identity ──
    const PrecompInstanceKey bucket_key = instance_key(ctx);

    // ── 6. Acquire program from the centralised store ────────────────────
    auto lease = session->program_store().acquire(
        bucket_key, key, m_cache_policy,
        [&]() -> std::unique_ptr<CompiledSceneProgram> {
            // Cache miss — delegate to the typed PrecompBuilderService.
            if (!ctx.services.precomp_builder) {
                return nullptr;
            }
            return ctx.services.precomp_builder->build(nested_scene, nested_ctx);
        });

    // Forward the user's eviction callback (kept on the node) to the
    // per-instance cache so it survives across clear()/acquire() cycles.
    // Review #3 — only do this once per freshly created bucket; the
    // pointer is reset by `set_on_evict()` when a new callback is
    // installed.  Comparing `lease.bucket` (non-owning, set by
    // acquire()) against `m_on_evict_wired_to` detects the new bucket
    // created by a `SceneProgramStore::clear()` (e.g. driven by
    // `RenderSession::reset_job()`) so the callback re-lands on the
    // fresh bucket instead of being stranded on the dropped one.
    if (m_on_evict && lease.bucket && lease.bucket != m_on_evict_wired_to) {
        session->program_store().set_on_evict(bucket_key, m_on_evict);
        m_on_evict_wired_to = lease.bucket;
    }

    auto program = lease.program;  // WP 5.2 — shared_ptr kept alive across uses.
    if (!program || program->empty()) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    // ── 7. Warm up parameter block (no-op if size unchanged) ─────────────
    m_param_block.warm_up(program->bindings.size());

    // ── 8. Refresh per-frame payloads ────────────────────────────────────
    const auto resolved = detail::resolve_layers(nested_scene, nested_ctx);
    detail::refresh_compiled_graph_payloads(program->frame_graph, nested_scene, nested_ctx, resolved);

    // ── 9. Execute via the BORROWED executor + scheduler ─────────────────
    // WP 5.0 — the GraphExecutor is stateless; production paths MUST
    // borrow it instead of constructing a local.  `services.executor`
    // is `chronon3d::graph::GraphExecutor*` (raw, non-owning) living
    // on `runtime::SessionServices`.  Fail-soft on missing wiring —
    // review #3/#4: tests / callers without wired services must
    // produce an empty FB rather than crash or throw (consistent with
    // the other guard paths above).  spdlog::warn marks the regression
    // so misconfigurations show up in observability.
    if (!session->services.executor) {
        spdlog::warn(
            "PrecompNode::execute(): session->services.executor is null — "
            "graph executor was not wired by make_session(); returning "
            "empty FB");
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }
    if (!ctx.services.scheduler) {
        spdlog::warn(
            "PrecompNode::execute(): ctx.services.scheduler is null — "
            "scheduler was not wired by the renderer; returning empty FB");
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }
    graph::GraphExecutor& executor = *session->services.executor;
    ExecutionScheduler& scheduler = *ctx.services.scheduler;

    // frame_graph is borrowed from the cached compiled program for the
    // duration of the inner execute() call — the shared_ptr above keeps
    // it alive even if a concurrent store clear() drops the bucket.
    auto nested_result = executor.execute(
        program->frame_graph, nested_ctx, *session, scheduler);

    if (nested_result) {
        return ctx.acquire_owned_fb(std::move(nested_result));
    }

    return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
}

} // namespace chronon3d::graph
