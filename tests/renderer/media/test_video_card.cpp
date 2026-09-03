#include <chronon3d/scene/builders/scene_builder.hpp>
#include <doctest/doctest.h>
#include <xxhash.h>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/test_utils.hpp>
#include <atomic>
#include <chrono>
#include <fstream>
using namespace chronon3d;

using namespace chronon3d::test;

namespace {

// AssetPreflight validates the manifest before the injected mock decoder is
// reached.  Keep the test self-contained by providing a real, empty fixture;
// the mock decoder deliberately does not inspect the file contents.
struct DummyVideoAsset {
    DummyVideoAsset() {
        static std::atomic<unsigned long long> sequence{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("chronon3d_video_card_dummy_" + std::to_string(nonce) + "_" +
             std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".mp4");
        std::ofstream output(path, std::ios::binary);
        REQUIRE(output.good());
    }

    ~DummyVideoAsset() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    std::filesystem::path path;
};

// Mock decoder: returns a solid color at the requested size.
// Canonical contract: MediaFrameProvider::decode_frame_at(path, RationalTime, w, h)
// → DecodeResult (DecodedFrame | DecodeEndOfStream | DecodeFailure).
class MockVideoDecoder final : public video::VideoFrameDecoder {
public:
    explicit MockVideoDecoder(Color color = {0.2f, 0.6f, 1.0f, 1.0f}) : m_color(color) {}

    media::DecodeResult decode_frame_at(
        const std::string&, chronon3d::RationalTime, int width, int height) override
    {
        auto fb = std::make_shared<Framebuffer>(width, height);
        for (int y = 0; y < height; ++y) {
            Color* row = fb->pixels_row(y);
            for (int x = 0; x < width; ++x) row[x] = m_color;
        }
        return media::DecodedFrame{std::move(fb)};
    }

    Color m_color;
};

std::shared_ptr<Framebuffer> render_video_comp(const Composition& comp, Color mock_color = {0.2f, 0.6f, 1.0f, 1.0f}) {
    auto renderer = test::make_renderer();
    auto decoder = std::make_shared<MockVideoDecoder>(mock_color);
    renderer.set_video_decoder(decoder);
    return renderer.render(comp, 0);
}

Composition make_video_card_comp(
    float rotate_y,
    const std::string& video_path,
    Vec2 card_size = {320, 180}) {
    return composition({
        .name = "VideoCard", .width = 640, .height = 360, .duration = 1
    }, [rotate_y, card_size, video_path](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
        s.camera().enable(true).position({0, 0, -800}).zoom(800).look_at({0, 0, 0});

        s.layer("video", [rotate_y, card_size, video_path](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, 0})
             .rotate({0, rotate_y, 0})
             .video(chronon3d::video::VideoSource{.path = video_path})
             .video_size(card_size);
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("VideoCard: mock decoder renders non-blank frame") {
    DummyVideoAsset asset;
    auto fb = render_video_comp(make_video_card_comp(0.0f, asset.path.string()));
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
        media::DecodeResult decode_frame_at(
            const std::string&, chronon3d::RationalTime, int w, int h) override {
            captured_w = w; captured_h = h;
            auto fb = std::make_shared<Framebuffer>(w, h);
            fb->clear(Color{0.5f, 0.5f, 0.5f, 1.0f});
            return media::DecodedFrame{std::move(fb)};
        }
    };

    DummyVideoAsset asset;
    auto comp = make_video_card_comp(0.0f, asset.path.string(), {320, 180});
    auto renderer = test::make_renderer();
    RenderSettings settings;
        renderer.set_settings(settings);
    auto decoder = std::make_shared<SizeCaptureDecoder>();
    renderer.set_video_decoder(decoder);
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);

    CHECK(decoder->captured_w == 320);
    CHECK(decoder->captured_h == 180);
}

TEST_CASE("VideoCard: 3D rotation changes output") {
    DummyVideoAsset asset;
    auto flat    = render_video_comp(make_video_card_comp(0.0f, asset.path.string()));
    auto rotated = render_video_comp(make_video_card_comp(45.0f, asset.path.string()));
    REQUIRE(flat    != nullptr);
    REQUIRE(rotated != nullptr);

    save_debug(*flat,    "output/debug/video_card/02_flat.png");
    save_debug(*rotated, "output/debug/video_card/03_y45.png");

    CHECK(framebuffer_hash(*flat) != framebuffer_hash(*rotated));
}
