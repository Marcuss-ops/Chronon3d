#include <doctest/doctest.h>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/core/framebuffer.hpp>

#ifndef CHRONON_WITH_VIDEO
TEST_CASE("VideoNode is disabled without CHRONON_WITH_VIDEO") {
    CHECK(true);
}
#else
using namespace chronon3d;
using namespace chronon3d::graph;
using namespace chronon3d::video;

class MockDecoder : public VideoDecoder {
public:
    std::shared_ptr<Framebuffer> decode_frame(
        const std::string& path,
        Frame source_frame,
        i32 width,
        i32 height,
        f32
    ) override {
        auto fb = std::make_shared<Framebuffer>(width, height);
        // Fill with a color based on frame to verify mapping
        fb->clear(Color(static_cast<f32>(source_frame) / 100.0f, 0, 0, 1));
        return fb;
    }
};

TEST_CASE("VideoNode execution and frame mapping") {
    VideoSource src;
    src.path = "test.mp4";
    src.source_start = 10;
    src.duration = 100;
    
    MockDecoder decoder;
    VideoNode node(src, &decoder, 50); // layer starts at frame 50

    RenderGraphContext ctx;
    ctx.width = 100;
    ctx.height = 100;

    SUBCASE("Before layer starts") {
        ctx.frame = 40;
        auto res = node.execute(ctx, {});
        CHECK(res->get_pixel(0, 0).r == 0.0f);
        CHECK(res->get_pixel(0, 0).a == 0.0f);
    }

    SUBCASE("At layer start") {
        ctx.frame = 50; // local frame 0 -> source frame 10
        auto res = node.execute(ctx, {});
        CHECK(res->get_pixel(0, 0).r == doctest::Approx(0.1f));
    }

    SUBCASE("With mapping") {
        ctx.frame = 60; // local frame 10 -> source frame 20
        auto res = node.execute(ctx, {});
        CHECK(res->get_pixel(0, 0).r == doctest::Approx(0.2f));
    }
}
#endif
