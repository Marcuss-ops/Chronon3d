// ==============================================================================
// tests/render_graph/features/test_clip_transition.cpp
//
// TRN-07 — ClipTransitionNode certification (Cut + Dissolve only).
// Verifies that a transition between two full-frame clips behaves as:
//   Cut:     p < 1 -> A, p >= 1 -> B
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

RenderGraphContext make_ctx(Frame frame, int w, int h) {
    RenderGraphContext ctx;
    ctx.frame_input = chronon3d::test::make_render_frame_info(w, h, static_cast<int>(frame.integral()));
    ctx.services.framebuffer_pool = cache::FramebufferPool::create_shared(64 * 1024 * 1024);
    return ctx;
}

std::shared_ptr<Framebuffer> make_fb(RenderGraphContext& ctx, int w, int h, const Color& color) {
    auto fb = ctx.services.framebuffer_pool->acquire_shared(w, h, /*clear=*/true);
    fb->clear(color);
    return fb;
}

} // namespace

TEST_CASE("ClipTransitionNode: Cut returns A before the boundary and B at/after") {
    auto ctx = make_ctx(Frame{5}, 64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Cut;
    // Transition window: frames 0..30 (30 frames at 30 fps).
    ClipTransitionNode node("cut", spec, Frame{0}, Frame{30});

    // p = 0 (frame 0) -> A
    {
        ctx.frame_input.frame = Frame{0};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        CHECK(result.value()->get_pixel(32, 32).r == doctest::Approx(1.0f));
        CHECK(result.value()->get_pixel(32, 32).b == doctest::Approx(0.0f));
    }

    // p = 0.5 (frame 15) -> still A for Cut
    {
        ctx.frame_input.frame = Frame{15};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        CHECK(result.value()->get_pixel(32, 32).r == doctest::Approx(1.0f));
        CHECK(result.value()->get_pixel(32, 32).b == doctest::Approx(0.0f));
    }

    // p = 1 (frame 30) -> B
    {
        ctx.frame_input.frame = Frame{30};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        CHECK(result.value()->get_pixel(32, 32).r == doctest::Approx(0.0f));
        CHECK(result.value()->get_pixel(32, 32).b == doctest::Approx(1.0f));
    }
}

TEST_CASE("ClipTransitionNode: Dissolve blends A and B linearly at 0/0.5/1") {
    auto ctx = make_ctx(Frame{15}, 64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Dissolve;
    spec.easing = Easing::Linear;
    // Transition window: frames 0..30 (30 frames at 30 fps).
    ClipTransitionNode node("dissolve", spec, Frame{0}, Frame{30});

    // p = 0 -> A
    {
        ctx.frame_input.frame = Frame{0};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(1.0f));
        CHECK(c.g == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(0.0f));
    }

    // p = 0.5 -> 0.5*A + 0.5*B = magenta (0.5, 0, 0.5)
    {
        ctx.frame_input.frame = Frame{15};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.5f).epsilon(0.01f));
        CHECK(c.g == doctest::Approx(0.0f).epsilon(0.01f));
        CHECK(c.b == doctest::Approx(0.5f).epsilon(0.01f));
        CHECK(c.a == doctest::Approx(1.0f).epsilon(0.01f));
    }

    // p = 1 -> B
    {
        ctx.frame_input.frame = Frame{30};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.g == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(1.0f));
    }
}

TEST_CASE("ClipTransitionNode: mismatched input dimensions return structured error when requested") {
    auto ctx = make_ctx(Frame{0}, 64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 32, 32, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Dissolve;
    spec.fit = ClipTransitionFitPolicy::ErrorOnMismatch;
    ClipTransitionNode node("dissolve_mismatch", spec, Frame{0}, Frame{30});

    std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
    std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
    auto result = node.execute(ctx, inputs, bboxes);

    CHECK(!result.ok());
    REQUIRE(!result.ok());
    CHECK(result.error().backend_code == RenderBackendErrorCode::InvalidInput);
}

TEST_CASE("ClipTransitionNode: Push moves A out and B in") {
    auto ctx = make_ctx(Frame{15}, 64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Push;
    spec.easing = Easing::Linear;
    spec.direction = TransitionDirection::Right;
    ClipTransitionNode node("push", spec, Frame{0}, Frame{30});

    // p = 0 -> A centered
    {
        ctx.frame_input.frame = Frame{0};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(1.0f));
        CHECK(c.b == doctest::Approx(0.0f));
    }

    // p = 1 -> B centered
    {
        ctx.frame_input.frame = Frame{30};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(1.0f));
    }
}

TEST_CASE("ClipTransitionNode: Slide brings B in over A") {
    auto ctx = make_ctx(Frame{15}, 64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Slide;
    spec.easing = Easing::Linear;
    spec.direction = TransitionDirection::Right;
    ClipTransitionNode node("slide", spec, Frame{0}, Frame{30});

    // p = 0 -> A only
    {
        ctx.frame_input.frame = Frame{0};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(1.0f));
        CHECK(c.b == doctest::Approx(0.0f));
    }

    // p = 1 -> B centered
    {
        ctx.frame_input.frame = Frame{30};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(1.0f));
    }
}

TEST_CASE("ClipTransitionNode: Wipe reveals B from right to left at midpoint") {
    auto ctx = make_ctx(Frame{15}, 64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Wipe;
    spec.easing = Easing::Linear;
    spec.direction = TransitionDirection::Right;
    spec.feather = 0.0f;
    ClipTransitionNode node("wipe", spec, Frame{0}, Frame{30});

    ctx.frame_input.frame = Frame{15};
    std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
    std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
    auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.value() != nullptr);

    // Direction Right reveals B from left to right, so the left half is B
    // and the right half is A at the midpoint.
    auto left = result.value()->get_pixel(10, 32);
    auto right = result.value()->get_pixel(54, 32);
    CHECK(left.r == doctest::Approx(0.0f));
    CHECK(left.b == doctest::Approx(1.0f));
    CHECK(right.r == doctest::Approx(1.0f));
    CHECK(right.b == doctest::Approx(0.0f));
}

TEST_CASE("ClipTransitionNode: Iris reveals B from center at midpoint") {
    auto ctx = make_ctx(Frame{15}, 64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Iris;
    spec.easing = Easing::Linear;
    spec.feather = 0.0f;
    ClipTransitionNode node("iris", spec, Frame{0}, Frame{30});

    ctx.frame_input.frame = Frame{15};
    std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
    std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
    auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.value() != nullptr);

    // Center should be B, corner should be A.
    auto center = result.value()->get_pixel(32, 32);
    auto corner = result.value()->get_pixel(4, 4);
    CHECK(center.r == doctest::Approx(0.0f));
    CHECK(center.b == doctest::Approx(1.0f));
    CHECK(corner.r == doctest::Approx(1.0f));
    CHECK(corner.b == doctest::Approx(0.0f));
}

TEST_CASE("ClipTransitionNode: Zoom fades from A to scaled B") {
    auto ctx = make_ctx(Frame{15}, 64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Zoom;
    spec.easing = Easing::Linear;
    spec.zoom_scale = 2.0f;
    ClipTransitionNode node("zoom", spec, Frame{0}, Frame{30});

    // p = 0 -> A
    {
        ctx.frame_input.frame = Frame{0};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(1.0f));
        CHECK(c.b == doctest::Approx(0.0f));
    }

    // p = 1 -> B
    {
        ctx.frame_input.frame = Frame{30};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(1.0f));
    }
}

TEST_CASE("ClipTransitionNode: Flash transitions through flash color at midpoint") {
    auto ctx = make_ctx(Frame{15}, 64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 64, 64, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Flash;
    spec.easing = Easing::Linear;
    spec.flash_color = Color::yellow();
    ClipTransitionNode node("flash", spec, Frame{0}, Frame{30});

    ctx.frame_input.frame = Frame{15};
    std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
    std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
    auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.value() != nullptr);
    auto c = result.value()->get_pixel(32, 32);

    // At p = 0.5 the output should be the flash color (yellow).
    CHECK(c.r == doctest::Approx(1.0f).epsilon(0.05f));
    CHECK(c.g == doctest::Approx(1.0f).epsilon(0.05f));
    CHECK(c.b == doctest::Approx(0.0f).epsilon(0.05f));
}

TEST_CASE("ClipTransitionNode: mismatched input dimensions scale to fit the output canvas") {
    auto ctx = make_ctx(Frame{0}, 64, 64);
    auto a = make_fb(ctx, 64, 64, Color::red());
    auto b = make_fb(ctx, 32, 32, Color::blue());

    ClipTransitionSpec spec;
    spec.kind = ClipTransitionKind::Cut;
    spec.fit = ClipTransitionFitPolicy::ScaleToFit;
    ClipTransitionNode node("cut_scale", spec, Frame{0}, Frame{30});

    // p = 0 -> A (64x64 red) fills the output.
    {
        ctx.frame_input.frame = Frame{0};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        CHECK(result.value()->width() == 64);
        CHECK(result.value()->height() == 64);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(1.0f));
        CHECK(c.g == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(0.0f));
    }

    // p = 1 -> B (32x32 blue) scaled up to 64x64.
    {
        ctx.frame_input.frame = Frame{30};
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.g == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(1.0f));
    }

    // p = 0.5 -> blend of full-resolution A and scaled B.
    {
        ctx.frame_input.frame = Frame{15};
        ClipTransitionSpec dissolve_spec;
        dissolve_spec.kind = ClipTransitionKind::Dissolve;
        dissolve_spec.easing = Easing::Linear;
        ClipTransitionNode dissolve_node("dissolve_scale", dissolve_spec, Frame{0}, Frame{30});
        std::array<FramebufferRef, 2> inputs = {a.get(), b.get()};
        std::array<std::optional<raster::BBox>, 2> bboxes = {std::nullopt, std::nullopt};
        auto result = dissolve_node.execute(ctx, inputs, bboxes);
        REQUIRE(result.value() != nullptr);
        auto c = result.value()->get_pixel(32, 32);
        CHECK(c.r == doctest::Approx(0.5f).epsilon(0.02f));
        CHECK(c.g == doctest::Approx(0.0f).epsilon(0.01f));
        CHECK(c.b == doctest::Approx(0.5f).epsilon(0.02f));
    }
}
