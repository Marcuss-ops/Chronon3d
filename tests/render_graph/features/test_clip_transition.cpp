// ==============================================================================
// tests/render_graph/features/test_clip_transition.cpp
//
// TRN-07 + TRN-07-EXT — ClipTransitionNode certification.
// Verifies that a transition between two full-frame clips behaves as:
//   Cut:      output = A for p < 1, output = B for p >= 1
//   Dissolve: output = A*(1-p) + B*p in premultiplied alpha space
//   Push/Slide/Wipe/Iris/Zoom/Flash: endpoints are A at p=0 and B at p=1,
//   and the midpoint shows the expected transition behavior.
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/nodes/clip_transition_node.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <tests/helpers/test_utils.hpp>

#include <array>
#include <cmath>
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

TEST_CASE("ClipTransitionNode: Push endpoints and directional motion") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Push;
    spec.direction = TransitionDirection::Right;
    spec.easing = Easing::Linear;
    ClipTransitionNode node("push", spec, Frame{0}, Frame{30});

    // p = 0 -> A, p = 1 -> B.
    {
        auto out_a = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{0});
        auto ca = out_a->get_pixel(32, 32);
        CHECK(ca.r == doctest::Approx(1.0f));
        CHECK(ca.b == doctest::Approx(0.0f));

        auto out_b = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{30});
        auto cb = out_b->get_pixel(32, 32);
        CHECK(cb.r == doctest::Approx(0.0f));
        CHECK(cb.b == doctest::Approx(1.0f));
    }

    // p = 0.5 -> both inputs contribute and motion is directional.
    // Implementation samples A at (u + p) and B at (u - (1-p)), so for
    // Right the left side still shows A and the right side shows B.
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{15});
        auto left = out->get_pixel(8, 32);
        auto right = out->get_pixel(56, 32);
        CHECK((left.r + left.b) > 0.0f);
        CHECK((right.r + right.b) > 0.0f);
        CHECK(left.r > right.r);
        CHECK(left.b < right.b);
    }
}

TEST_CASE("ClipTransitionNode: Slide endpoints and B enters from direction") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Slide;
    spec.direction = TransitionDirection::Right;
    spec.easing = Easing::Linear;
    ClipTransitionNode node("slide", spec, Frame{0}, Frame{30});

    // p = 0 -> A, p = 1 -> B.
    {
        auto out_a = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{0});
        auto ca = out_a->get_pixel(32, 32);
        CHECK(ca.r == doctest::Approx(1.0f));
        CHECK(ca.b == doctest::Approx(0.0f));

        auto out_b = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{30});
        auto cb = out_b->get_pixel(32, 32);
        CHECK(cb.r == doctest::Approx(0.0f));
        CHECK(cb.b == doctest::Approx(1.0f));
    }

    // p = 0.5 -> left side is mostly A, right side is mostly B.
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{15});
        auto left = out->get_pixel(8, 32);
        auto right = out->get_pixel(56, 32);
        CHECK(left.r > right.r);
        CHECK(left.b < right.b);
    }
}

TEST_CASE("ClipTransitionNode: Wipe sweeps from direction with feather") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Wipe;
    spec.direction = TransitionDirection::Right;
    spec.feather = 0.05f;
    spec.easing = Easing::Linear;
    ClipTransitionNode node("wipe", spec, Frame{0}, Frame{30});

    // p = 0 -> A, p = 1 -> B.
    {
        auto out_a = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{0});
        auto ca = out_a->get_pixel(32, 32);
        CHECK(ca.r == doctest::Approx(1.0f));
        CHECK(ca.b == doctest::Approx(0.0f));

        auto out_b = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{30});
        auto cb = out_b->get_pixel(32, 32);
        CHECK(cb.r == doctest::Approx(0.0f));
        CHECK(cb.b == doctest::Approx(1.0f));
    }

    // p = 0.5 -> right side is A, left side is B, middle is blended.
    // The implementation sweeps the mask so that B appears from the left
    // when direction == Right.
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{15});
        auto left = out->get_pixel(8, 32);
        auto mid = out->get_pixel(32, 32);
        auto right = out->get_pixel(56, 32);
        CHECK(left.b > left.r);
        CHECK(right.r > right.b);
        CHECK(mid.r > 0.0f);
        CHECK(mid.b > 0.0f);
    }
}

TEST_CASE("ClipTransitionNode: Iris opens from center with feather") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Iris;
    spec.center = Vec2{0.5f, 0.5f};
    spec.feather = 0.05f;
    spec.easing = Easing::Linear;
    ClipTransitionNode node("iris", spec, Frame{0}, Frame{30});

    // p = 0 -> A, p = 1 -> B.
    {
        auto out_a = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{0});
        auto ca = out_a->get_pixel(32, 32);
        CHECK(ca.r == doctest::Approx(1.0f));
        CHECK(ca.b == doctest::Approx(0.0f));

        auto out_b = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{30});
        auto cb = out_b->get_pixel(32, 32);
        CHECK(cb.r == doctest::Approx(0.0f));
        CHECK(cb.b == doctest::Approx(1.0f));
    }

    // p = 0.5 -> center is B, corners are A.
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{15});
        auto center = out->get_pixel(32, 32);
        auto corner = out->get_pixel(4, 4);
        CHECK(center.b > center.r);
        CHECK(corner.r > corner.b);
    }
}

TEST_CASE("ClipTransitionNode: Zoom fades and scales B from center") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Zoom;
    spec.center = Vec2{0.5f, 0.5f};
    spec.zoom_scale = 2.0f;
    spec.easing = Easing::Linear;
    ClipTransitionNode node("zoom", spec, Frame{0}, Frame{30});

    // p = 0 -> A, p = 1 -> B.
    {
        auto out_a = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{0});
        auto ca = out_a->get_pixel(32, 32);
        CHECK(ca.r == doctest::Approx(1.0f));
        CHECK(ca.b == doctest::Approx(0.0f));

        auto out_b = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{30});
        auto cb = out_b->get_pixel(32, 32);
        CHECK(cb.r == doctest::Approx(0.0f));
        CHECK(cb.b == doctest::Approx(1.0f));
    }

    // p = 0.5 -> output is a blend of A and B.
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{15});
        auto c = out->get_pixel(32, 32);
        CHECK(c.r > 0.0f);
        CHECK(c.b > 0.0f);
    }
}

TEST_CASE("ClipTransitionNode: Flash peaks at midpoint with configured color") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Flash;
    spec.flash_color = Color::green();
    spec.easing = Easing::Linear;
    ClipTransitionNode node("flash", spec, Frame{0}, Frame{30});

    // p = 0 -> A, p = 1 -> B.
    {
        auto out_a = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{0});
        auto ca = out_a->get_pixel(32, 32);
        CHECK(ca.r == doctest::Approx(1.0f));
        CHECK(ca.b == doctest::Approx(0.0f));

        auto out_b = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{30});
        auto cb = out_b->get_pixel(32, 32);
        CHECK(cb.r == doctest::Approx(0.0f));
        CHECK(cb.b == doctest::Approx(1.0f));
    }

    // p = 0.5 -> output is dominated by the flash color.
    {
        auto out = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{15});
        auto c = out->get_pixel(32, 32);
        CHECK(c.g > c.r);
        CHECK(c.g > c.b);
    }
}

TEST_CASE("ClipTransitionNode: one-frame Flash is an instantaneous safe pass") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Flash;
    spec.easing = Easing::Linear;
    ClipTransitionNode node("flash_one_frame", spec, Frame{20}, Frame{1});

    const auto before = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{19});
    const auto first = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{20});
    const auto after = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{21});

    for (const auto* framebuffer : {before.get(), first.get(), after.get()}) {
        REQUIRE(framebuffer != nullptr);
        const auto pixel = framebuffer->get_pixel(32, 32);
        CHECK(std::isfinite(pixel.r));
        CHECK(std::isfinite(pixel.g));
        CHECK(std::isfinite(pixel.b));
        CHECK(std::isfinite(pixel.a));
        CHECK(pixel.a >= 0.0f);
        CHECK(pixel.a <= 1.0f);
    }

    CHECK(first->get_pixel(32, 32).r == doctest::Approx(1.0f));
    CHECK(first->get_pixel(32, 32).b == doctest::Approx(0.0f));
    CHECK(after->get_pixel(32, 32).r == doctest::Approx(0.0f));
    CHECK(after->get_pixel(32, 32).b == doctest::Approx(1.0f));
}

TEST_CASE("ClipTransitionNode: Flash preserves transparent premultiplied endpoints") {
    auto ctx = make_ctx(64, 64);
    auto a = make_fb(ctx, 64, 64, Color::transparent());
    auto b = make_fb(ctx, 64, 64, Color::transparent());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Flash;
    spec.easing = Easing::Linear;
    ClipTransitionNode node("flash_alpha", spec, Frame{0}, Frame{2});

    const auto before = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{0});
    const auto midpoint = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{1});
    const auto after = execute_at_frame(node, ctx, {a.get(), b.get()}, Frame{2});

    const auto before_pixel = before->get_pixel(32, 32);
    CHECK(before_pixel.r == doctest::Approx(0.0f));
    CHECK(before_pixel.g == doctest::Approx(0.0f));
    CHECK(before_pixel.b == doctest::Approx(0.0f));
    CHECK(before_pixel.a == doctest::Approx(0.0f));

    const auto midpoint_pixel = midpoint->get_pixel(32, 32);
    CHECK(midpoint_pixel.r == doctest::Approx(1.0f));
    CHECK(midpoint_pixel.g == doctest::Approx(1.0f));
    CHECK(midpoint_pixel.b == doctest::Approx(1.0f));
    CHECK(midpoint_pixel.a == doctest::Approx(1.0f));

    const auto after_pixel = after->get_pixel(32, 32);
    CHECK(after_pixel.r == doctest::Approx(0.0f));
    CHECK(after_pixel.g == doctest::Approx(0.0f));
    CHECK(after_pixel.b == doctest::Approx(0.0f));
    CHECK(after_pixel.a == doctest::Approx(0.0f));
}
