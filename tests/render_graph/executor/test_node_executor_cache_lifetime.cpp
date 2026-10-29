#include <doctest/doctest.h>

#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/color.hpp>

#include "src/render_graph/executor/node_executor.hpp"
#include "src/render_graph/executor/cache_evaluator.hpp"
#include "src/render_graph/native_surface_frame_lifetime.hpp"
#include "src/render_graph/nodes/native_surface.hpp"
#include <chronon3d/render_graph/nodes/clear_node.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>

#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::graph;

TEST_CASE("frame-scoped surface lifetime includes non-video native backends") {
    class NativeSurfaceOnlyStats final : public RenderBackendStatsApi {
    public:
        bool supports_native_surfaces() const noexcept override { return true; }
        bool supports_native_video_surface() const noexcept override { return false; }
    } native_surface_only;
    class NativeVideoOnlyStats final : public RenderBackendStatsApi {
    public:
        bool supports_native_surfaces() const noexcept override { return false; }
        bool supports_native_video_surface() const noexcept override { return true; }
    } native_video_only;
    class SoftwareStats final : public RenderBackendStatsApi {
    public:
        bool supports_native_surfaces() const noexcept override { return false; }
        bool supports_native_video_surface() const noexcept override { return false; }
    } software;

    CHECK(chronon3d::graph::detail::has_frame_scoped_native_surfaces(&native_surface_only));
    CHECK(chronon3d::graph::detail::has_frame_scoped_native_surfaces(&native_video_only));
    CHECK_FALSE(chronon3d::graph::detail::has_frame_scoped_native_surfaces(&software));
    CHECK_FALSE(chronon3d::graph::detail::has_frame_scoped_native_surfaces(nullptr));
}

TEST_CASE("GPU-only owned framebuffer clones allocate and copy a distinct native surface") {
    class SurfaceBackend final : public RenderBackend {
    public:
        void apply_per_pixel_dof(Framebuffer&, std::span<const float>,
                                  const DepthOfFieldSettings&, const LensModel&,
                                  const std::optional<raster::BBox>&) override {}
        void apply_effect_stack(Framebuffer&, const EffectStack&,
                                const effects::EffectExecutionContext&) override {}
        void composite_layer(Framebuffer&, const Framebuffer&, BlendMode,
                             const std::optional<raster::BBox>&,
                             CompositeOperator) override {}
        void apply_blur(Framebuffer&, float,
                        const std::optional<raster::BBox>&) override {}
        bool supports_native_surfaces() const noexcept override { return true; }
        bool is_native_surface_valid(runtime::RenderSurfaceHandle handle) const noexcept override {
            return live.contains(handle);
        }
        RenderOpResult create_surface(runtime::RenderSurfaceHandle handle,
                                      const runtime::SurfaceDesc&) override {
            live.insert(handle);
            return RenderOpOutcome{};
        }
        RenderOpResult release_surface(runtime::RenderSurfaceHandle handle) override {
            live.erase(handle);
            return RenderOpOutcome{};
        }
        RenderOpResult copy_surface(runtime::RenderSurfaceHandle destination,
                                    runtime::RenderSurfaceHandle source,
                                    const std::optional<raster::BBox>&) override {
            if (!live.contains(destination) || !live.contains(source)) {
                return make_render_backend_error(RenderBackendErrorCode::InvalidInput,
                                                 "copy requires two live surfaces");
            }
            copies.emplace_back(destination, source);
            return RenderOpOutcome{};
        }
        std::unordered_set<runtime::RenderSurfaceHandle> live;
        std::vector<std::pair<runtime::RenderSurfaceHandle,
                              runtime::RenderSurfaceHandle>> copies;
    } backend;

    runtime::RenderSurfaceRegistry registry;
    const auto desc = runtime::SurfaceDesc::make(
        32, 24, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient);
    const auto source_handle = registry.create(desc);
    REQUIRE(source_handle != runtime::kInvalidRenderSurfaceHandle);
    REQUIRE(backend.create_surface(source_handle, desc).ok());
    auto source = std::make_shared<Framebuffer>(32, 24, static_cast<Color*>(nullptr));
    source->set_surface_handle(source_handle);
    source->mark_gpu_authoritative();

    RenderGraphContext ctx;
    ctx.backend = &backend;
    ctx.surface_registry = &registry;

    auto clone = ctx.acquire_owned_fb(*source);
    REQUIRE(clone != nullptr);
    CHECK(clone->surface_handle() != source_handle);
    CHECK(backend.is_native_surface_valid(clone->surface_handle()));
    CHECK(backend.copies.size() == 1);
    CHECK(backend.copies.front().first == clone->surface_handle());
    CHECK(backend.copies.front().second == source_handle);
    CHECK(backend.is_native_surface_valid(source_handle));

    OwnedFB planned_slot(
        new Framebuffer(32, 24, static_cast<Color*>(nullptr)),
        PoolFbDeleter(DeleteFramebuffer{}));
    REQUIRE(source->data() == nullptr);
    REQUIRE(source->surface_handle() == source_handle);
    REQUIRE_FALSE(source->is_cpu_authoritative());
    CHECK(ctx.node_exec.terminal_video_encode_surface ==
          runtime::kInvalidRenderSurfaceHandle);
    ctx.node_exec.planned_physical_slot = &planned_slot;
    REQUIRE(ctx.node_exec.planned_physical_slot == &planned_slot);
    auto planned_clone = ctx.acquire_owned_fb(*source);
    REQUIRE(planned_clone != nullptr);
    CHECK(ctx.node_exec.planned_physical_slot == nullptr);
    CHECK(planned_clone->surface_handle() != source_handle);
    CHECK(backend.is_native_surface_valid(planned_clone->surface_handle()));
    CHECK(backend.copies.size() == 2);
    CHECK(backend.copies.back().first == planned_clone->surface_handle());
    CHECK(backend.copies.back().second == source_handle);
    CHECK(backend.is_native_surface_valid(source_handle));

    auto moved = ctx.acquire_owned_fb(std::move(source));
    REQUIRE(moved != nullptr);
    CHECK(moved->surface_handle() != source_handle);
    CHECK(moved->surface_handle() != clone->surface_handle());
    CHECK(moved->surface_handle() != planned_clone->surface_handle());
    CHECK(backend.is_native_surface_valid(source_handle));
    CHECK(backend.copies.size() == 3);
}

TEST_CASE("CPU-owned framebuffer copy drops the source native handle") {
    Framebuffer source(17, 9, false);
    source.set_pixel(3, 4, Color::blue());
    source.set_surface_handle(0x1234U);
    source.mark_gpu_authoritative();

    RenderGraphContext direct_context;
    auto direct = direct_context.acquire_owned_fb(source);
    REQUIRE(direct != nullptr);
    CHECK(direct->data() != source.data());
    CHECK(direct->surface_handle() == runtime::kInvalidRenderSurfaceHandle);
    CHECK(direct->is_cpu_authoritative());
    CHECK(direct->get_pixel(3, 4).b == Color::blue().b);

    auto pool = FramebufferPool::create_shared(1024 * 1024);
    REQUIRE(pool != nullptr);
    RenderGraphContext pool_context;
    pool_context.framebuffer_pool = pool;
    auto pooled = pool_context.acquire_owned_fb(source);
    REQUIRE(pooled != nullptr);
    CHECK(pooled->surface_handle() == runtime::kInvalidRenderSurfaceHandle);
    CHECK(pooled->is_cpu_authoritative());
    CHECK(pooled->get_pixel(3, 4).b == Color::blue().b);
}

TEST_CASE("unmaterialized encode surface without CPU pixels fails native validation") {
    class UnmaterializedEncodeBackend final : public RenderBackend {
    public:
        void apply_per_pixel_dof(Framebuffer&, std::span<const float>,
                                  const DepthOfFieldSettings&, const LensModel&,
                                  const std::optional<raster::BBox>&) override {}
        void apply_effect_stack(Framebuffer&, const EffectStack&,
                                const effects::EffectExecutionContext&) override {}
        void composite_layer(Framebuffer&, const Framebuffer&, BlendMode,
                             const std::optional<raster::BBox>&,
                             CompositeOperator) override {}
        void apply_blur(Framebuffer&, float,
                        const std::optional<raster::BBox>&) override {}
        bool supports_native_surfaces() const noexcept override { return true; }
        bool is_native_surface_valid(runtime::RenderSurfaceHandle) const noexcept override {
            return false;
        }
    } backend;

    runtime::RenderSurfaceRegistry registry;
    RenderGraphContext ctx;
    ctx.backend = &backend;
    ctx.surface_registry = &registry;
    constexpr auto encode_handle = static_cast<runtime::RenderSurfaceHandle>(0x9876U);
    ctx.policy.native_video_encode_surface = encode_handle;
    Framebuffer framebuffer(16, 16, static_cast<Color*>(nullptr));
    framebuffer.set_surface_handle(encode_handle);
    framebuffer.mark_gpu_authoritative();

    REQUIRE(ctx.policy.native_video_encode_surface == framebuffer.surface_handle());
    REQUIRE(framebuffer.data() == nullptr);
    CHECK_FALSE(backend.is_native_surface_valid(encode_handle));
    CHECK_FALSE(ensure_native_surface(ctx, framebuffer, "test-encode-target"));
}

TEST_CASE("ClearNode output is frame-variant because compositing mutates it") {
    const ClearNode node;
    CHECK(node.cache_policy().frame_dependent());
    CHECK(cache_frame_for_policy(node.cache_policy(), Frame{0}) == Frame{0});
    CHECK(cache_frame_for_policy(node.cache_policy(), Frame{30}) == Frame{30});
}

TEST_CASE("cache evaluation production path retains sub-millisecond timing") {
    RenderCounters counters;
    RenderGraphContext context;
    context.node_exec.counters = &counters;
    ClearNode node;
    for (int i = 0; i < 1000; ++i)
        (void)evaluate_cache(node, context, 0, false, false, 0, false);
    CHECK(counters.cache_eval_wall_ns.load() > 0);
    CHECK(counters.cache_eval_wall_ms.load() == counters.cache_eval_wall_ns.load() / 1'000'000);
    CHECK(counters.cache_eval_wall_us.load() == counters.cache_eval_wall_ns.load() / 1000);
    context.node_exec.counters = nullptr;
    const auto previous = counters.cache_eval_wall_ns.load();
    (void)evaluate_cache(node, context, 0, false, false, 0, false);
    CHECK(counters.cache_eval_wall_ns.load() == previous);
}

TEST_CASE("NodeCache lifetime policy rejects arena-backed framebuffer storage") {
    FramebufferArena arena(4096);
    auto* pixels = static_cast<Color*>(arena.allocate(16 * 16 * sizeof(Color)));
    REQUIRE(pixels != nullptr);

    Framebuffer framebuffer(16, 16, pixels);
    PoolFbDeleter arena_deleter(DeleteFramebuffer{});

    CHECK(framebuffer.is_arena_allocated());
    CHECK_FALSE(node_output_is_persistently_cacheable(framebuffer, arena_deleter));
}

TEST_CASE("NodeCache lifetime policy accepts independently owned framebuffer storage") {
    Framebuffer framebuffer(16, 16);
    PoolFbDeleter pool_deleter(DeleteFramebuffer{});

    CHECK_FALSE(framebuffer.is_arena_allocated());
    CHECK(node_output_is_persistently_cacheable(framebuffer, pool_deleter));
}

TEST_CASE("NodeCache lifetime policy rejects renderer-owned and native-only outputs") {
    Framebuffer framebuffer(16, 16);
    PoolFbDeleter renderer_deleter(RendererOwned{});
    CHECK_FALSE(node_output_is_persistently_cacheable(framebuffer, renderer_deleter));

    framebuffer.set_surface_handle(1);
    PoolFbDeleter owned_deleter(DeleteFramebuffer{});
    CHECK_FALSE(node_output_is_persistently_cacheable(framebuffer, owned_deleter));
}
