// ==============================================================================
// tests/render_graph/features/test_clip_transition.cpp
//
// TRN-07 — ClipTransitionNode certification (Cut + Dissolve only).
// Verifies that a transition between two full-frame clips behaves as:
//   Cut:      output = A for p < 1, output = B for p >= 1
//   Dissolve: output = A*(1-p) + B*p in premultiplied alpha space
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/nodes/clip_transition_node.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <tests/helpers/test_utils.hpp>

#include <array>
#include <memory>

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

RenderGraphContext make_ctx(int w, int h) {
    RenderGraphContext ctx;
    ctx.frame_input = chronon3d::test::make_render_frame_info(w, h, 0);
    ctx.services.framebuffer_pool = cache::FramebufferPool::create_shared(64 * 1024 * 1024);
    return ctx;
}

std::shared_ptr<Framebuffer> make_fb(RenderGraphContext& ctx, int w, int h, const Color& color) {
    auto fb = ctx.services.framebuffer_pool->acquire_shared(w, h, /*clear=*/true);
    fb->clear(color);
    return fb;
}

auto execute_at_frame(ClipTransitionNode& node,
                       RenderGraphContext& ctx,
                       const std::array<FramebufferRef, 2>& inputs,
                       Frame frame) {
    ctx.frame_input.frame = frame;
    std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
    auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.ok());
    return std::move(result.value());
}

} // namespace

TEST_CASE("ClipTransitionNode: Cut returns A before the boundary and B at/after") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Cut;
    ClipTransitionNode node("cut", spec, Frame{0}, Frame{30});

    // p = 0 (frame 0) -> A
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{0});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(1.0f));
        CHECK(c.b == doctest::Approx(0.0f));
    }

    // p = 0.5 (frame 15) -> still A for Cut
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{15});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(1.0f));
        CHECK(c.b == doctest::Approx(0.0f));
    }

    // p = 1 (frame 30) -> B
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{30});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(1.0f));
    }
}

TEST_CASE("ClipTransitionNode: Dissolve blends A and B linearly at 0/0.5/1") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Dissolve;
    spec.easing = Easing::Linear;
    ClipTransitionNode node("dissolve", spec, Frame{0}, Frame{30});

    // p = 0 -> A
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{0});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(1.0f));
        CHECK(c.g == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(0.0f));
    }

    // p = 0.5 -> 0.5*A + 0.5*B = magenta (0.5, 0, 0.5)
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{15});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.5f).epsilon(0.01f));
        CHECK(c.g == doctest::Approx(0.0f).epsilon(0.01f));
        CHECK(c.b == doctest::Approx(0.5f).epsilon(0.01f));
        CHECK(c.a == doctest::Approx(1.0f).epsilon(0.01f));
    }

    // p = 1 -> B
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{30});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.g == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(1.0f));
    }
}

TEST_CASE("ClipTransitionNode: Cut boundary is exactly at p == 1.0") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Cut;
    ClipTransitionNode node("cut_boundary", spec, Frame{0}, Frame{30});

    // One frame before the end: still A.
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{29});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(1.0f));
        CHECK(c.b == doctest::Approx(0.0f));
    }

    // Exactly at the end (p == 1.0): B.
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{30});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(1.0f));
    }
}

TEST_CASE("ClipTransitionNode: Dissolve applies easing to progress") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Dissolve;
    spec.easing = Easing::InQuad;
    ClipTransitionNode node("dissolve_inquad", spec, Frame{0}, Frame{30});

    // p = 0.5 with InQuad -> eased progress = 0.25
    auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{15});
    auto c = out->get_pixel(32, 32);
    CHECK(c.r == doctest::Approx(0.75f).epsilon(0.02f));
    CHECK(c.g == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(c.b == doctest::Approx(0.25f).epsilon(0.02f));
    CHECK(c.a == doctest::Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("ClipTransitionNode: Dissolve respects premultiplied alpha") {
    auto ctx = make_ctx(64, 64);

    // A: 50% red, premultiplied -> stored as (0.5, 0, 0, 0.5)
    auto a = make_fb(ctx, 64, 64, Color::transparent());
    a->clear(Color{0.5f, 0.0f, 0.0f, 0.5f});

    // B: 50% blue, premultiplied -> stored as (0, 0, 0.5, 0.5)
    auto b = make_fb(ctx, 64, 64, Color::transparent());
    b->clear(Color{0.0f, 0.0f, 0.5f, 0.5f});

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Dissolve;
    spec.easing = Easing::Linear;
    ClipTransitionNode node("dissolve_premul", spec, Frame{0}, Frame{30});

    // p = 0.5 -> 0.5*A + 0.5*B = (0.25, 0, 0.25, 0.5) in premultiplied space.
    auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{15});
    auto c = out->get_pixel(32, 32);
    CHECK(c.r == doctest::Approx(0.25f).epsilon(0.01f));
    CHECK(c.g == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(c.b == doctest::Approx(0.25f).epsilon(0.01f));
    CHECK(c.a == doctest::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("ClipTransitionNode: mismatched input dimensions return structured error when requested") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 32, 32, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Dissolve;
    spec.fit = ClipTransitionFitPolicy::ErrorOnMismatch;
    ClipTransitionNode node("dissolve_mismatch", spec, Frame{0}, Frame{30});

    std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
    std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
    ctx.frame_input.frame = Frame{15};
    auto result = node.execute(ctx, inputs, bboxes);

    REQUIRE(!result.ok());
    CHECK(result.error().backend_code == RenderBackendErrorCode::InvalidInput);
}

TEST_CASE("ClipTransitionNode: mismatched input dimensions scale to fit the output canvas") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 32, 32, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Cut;
    spec.fit = ClipTransitionFitPolicy::ScaleToFit;
    ClipTransitionNode node("cut_scale", spec, Frame{0}, Frame{30});

    // p = 0 -> A (64x64 red) fills the output.
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{0});
        CHECK(out->width() == 64);
        CHECK(out->height() == 64);
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(1.0f));
        CHECK(c.g == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(0.0f));
    }

    // p = 1 -> B (32x32 blue) scaled up to 64x64.
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{30});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.g == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(1.0f));
    }

    // p = 0.5 -> blend of full-resolution A and scaled B.
    {
        ClipTransitionSpec dissolve_spec;
        dissolve_spec.kind = ClipTransitionKind::Dissolve;
        dissolve_spec.easing = Easing::Linear;
        ClipTransitionNode dissolve_node("dissolve_scale", dissolve_spec, Frame{0}, Frame{30});
        auto out = execute_at_frame(dissolve_node, ctx, {a.get(), b.get()}, Frame{15});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.5f).epsilon(0.02f));
        CHECK(c.g == doctest::Approx(0.0f).epsilon(0.01f));
        CHECK(c.b == doctest::Approx(0.5f).epsilon(0.02f));
    }
}
