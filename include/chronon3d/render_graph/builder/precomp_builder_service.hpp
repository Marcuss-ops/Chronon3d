#pragma once

// ---------------------------------------------------------------------------
// render_graph/builder/precomp_builder_service.hpp
//
// TICKET-010 — typed alternative to the legacy
// `::chronon3d::graph::RenderResourceContext::precomp_build` std::function.
//
// Ownership / wiring:
//   - `PipelineCatalogs::precomp_builder` owns the service instance
//     (unique_ptr<PrecompBuilderService>); constructed once at pipeline init
//     (typical default: `DefaultPrecompBuilder`).
//   - `wire_catalog_pointers(ctx, catalogs)` copies the service pointer
//     into `ctx.services.precomp_builder`.
//   - Consumers (currently `PrecompNode::execute()` on cache miss) invoke
//     `ctx.services.precomp_builder->build(nested_scene, nested_ctx)`.
//
// Replace the old `wire_precomp_build_factory(ctx, catalogs)` std::function
// wiring.  The service has a virtual destructor + a single virtual `build()`
// method; test doubles (custom compilers/profilers) can subclass for
// instrumentation without touching the catalog wiring.
// ---------------------------------------------------------------------------

#include <memory>

namespace chronon3d { class Scene; }

namespace chronon3d::graph {

class CompiledSceneProgram;
struct RenderGraphContext;

/// Interface for building + compiling nested scene programs.
/// Replaces the legacy std::function `precomp_build` in
/// `RenderResourceContext`.  Implementation is stateless w.r.t. job state
/// (per-frame state is threaded through `nested_ctx`); any long-lived state
/// (caches, etc.) lives in callees or in the service's own members.
class PrecompBuilderService {
public:
    virtual ~PrecompBuilderService() = default;

    /// Build the render graph for `scene` via `GraphBuilder`, then compile it
    /// via `FrameGraphCompiler`, returning a populated
    /// `CompiledSceneProgram` ready for the inner precomp cache.
    ///
    /// `nested_ctx` is the per-frame context to use for the nested build.
    /// The service must NOT assume ownership of pipeline state — any
    /// services it needs (framebuffer_pool, registry, video_decoder, etc.)
    /// are already in `nested_ctx.services` and must be re-used from there.
    ///
    /// Returns nullptr on failure.  PrecompNode already treats nullptr as
    /// "outer fail; return empty framebuffer".
    [[nodiscard]] virtual std::unique_ptr<CompiledSceneProgram>
    build(const ::chronon3d::Scene& scene,
          RenderGraphContext& nested_ctx) const = 0;
};

/// Behaviour-equivalent implementation of the legacy
/// `wire_precomp_build_factory` lambda:
///
///   GraphBuilder::build(scene, ctx)
///     -> FrameGraphCompiler::compile(graph, ctx, FrameGraphCompileOptions)
///        -> std::make_unique<CompiledSceneProgram>(compile_scene_program(...))
///
/// This is the default installed into `PipelineCatalogs::precomp_builder`
/// at pipeline init by `init_graph_pipeline_catalogs()` /
/// `populate_builtin_pipeline_catalogs()`.
class DefaultPrecompBuilder final : public PrecompBuilderService {
public:
    [[nodiscard]] std::unique_ptr<CompiledSceneProgram>
    build(const ::chronon3d::Scene& scene,
          RenderGraphContext& nested_ctx) const override;
};

} // namespace chronon3d::graph
