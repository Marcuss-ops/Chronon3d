#include <doctest/doctest.h>
#include <xxhash.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <tests/helpers/render_fixtures.hpp>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

u64 framebuffer_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

// Mock decoder: returns a solid color at the requested size
class MockVideoDecoder final : public video::VideoFrameDecoder {
public:
    explicit MockVideoDecoder(Color color = {0.2f, 0.6f, 1.0f, 1.0f}) : m_color(color) {}

    std::shared_ptr<Framebuffer> decode_frame(
        const std::string&, Frame, i32 width, i32 height, f32) override
    {
        auto fb = std::make_shared<Framebuffer>(width, height);
        for (int y = 0; y < height; ++y) {
            Color* row = fb->pixels_row(y);
            for (int x = 0; x < width; ++x) row[x] = m_color;
        }
        return fb;
    }

    Color m_color;
};

std::unique_ptr<Framebuffer> render_video_comp(const Composition& comp, Color mock_color = {0.2f, 0.6f, 1.0f, 1.0f}) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    auto decoder = std::make_shared<MockVideoDecoder>(mock_color);
    renderer.set_video_decoder(decoder);
    return renderer.render_frame(comp, 0);
}

Composition make_video_card_comp(float rotate_y, Vec2 card_size = {320, 180}) {
    return composition({
        .name = "VideoCard", .width = 640, .height = 360, .duration = 1
    }, [rotate_y, card_size](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0, 0, -800}).zoom(800).look_at({0, 0, 0});

        s.layer("video", [rotate_y, card_size](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, 0})
             .rotate({0, rotate_y, 0})
             .video("dummy.mp4")
             .video_size(card_size);
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("VideoCard: mock decoder renders non-blank frame") {
    auto fb = render_video_comp(make_video_card_comp(0.0f));
    REQUIRE(fb != nullptr);
    save_debug(*fb, "output/debug/video_card/01_flat_video_card.png");

    // At least some pixels should be the mock color (blue-ish)
    bool found_blue = false;
    for (int y = 0; y < fb->height() && !found_blue; ++y) {
        for (int x = 0; x < fb->width() && !found_blue; ++x) {
            const Color c = fb->get_pixel(x, y);
            if (c.b > 0.5f && c.a > 0.5f) found_blue = true;
        }
    }
    CHECK(found_blue);
}

TEST_CASE("VideoCard: video_size controls decoder dimensions") {
    // Mock decoder receives the size from VideoSource.size, not ctx dimensions
    // 320x180 video in a 640x360 canvas — decoder should get 320x180
    struct SizeCaptureDecoder final : public video::VideoFrameDecoder {
        i32 captured_w{0}, captured_h{0};
        std::shared_ptr<Framebuffer> decode_frame(
            const std::string&, Frame, i32 w, i32 h, f32) override {
            captured_w = w; captured_h = h;
            auto fb = std::make_shared<Framebuffer>(w, h);
            fb->clear(Color{0.5f, 0.5f, 0.5f, 1.0f});
            return fb;
        }
    };

    auto comp = make_video_card_comp(0.0f, {320, 180});
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    auto decoder = std::make_shared<SizeCaptureDecoder>();
    renderer.set_video_decoder(decoder);
    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    CHECK(decoder->captured_w == 320);
    CHECK(decoder->captured_h == 180);
}

TEST_CASE("VideoCard: 3D rotation changes output") {
    auto flat    = render_video_comp(make_video_card_comp(0.0f));
    auto rotated = render_video_comp(make_video_card_comp(45.0f));
    REQUIRE(flat    != nullptr);
    REQUIRE(rotated != nullptr);

    save_debug(*flat,    "output/debug/video_card/02_flat.png");
    save_debug(*rotated, "output/debug/video_card/03_y45.png");

    CHECK(framebuffer_hash(*flat) != framebuffer_hash(*rotated));
}
