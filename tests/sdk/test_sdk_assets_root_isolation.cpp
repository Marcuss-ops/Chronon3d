#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/authoring/asset.hpp>
#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/authoring/scene.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace c3d = chronon3d;
namespace fs = std::filesystem;

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 128;

class CwdGuard final {
public:
    CwdGuard() : original_(fs::current_path()) {}
    CwdGuard(const CwdGuard&) = delete;
    CwdGuard& operator=(const CwdGuard&) = delete;

    ~CwdGuard() { restore(); }

    void restore() noexcept {
        if (restored_) return;
        std::error_code error;
        fs::current_path(original_, error);
        restored_ = true;
    }

private:
    fs::path original_;
    bool restored_{false};
};

struct Rgb8 {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
};

fs::path unique_test_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("chronon3d-sdk-assets-root-" + std::to_string(stamp));
}

bool write_solid_png(const fs::path& path, c3d::Color color) {
    fs::create_directories(path.parent_path());
    c3d::Framebuffer framebuffer{16, 16};
    framebuffer.clear(color);
    return c3d::save_png(framebuffer, path.string());
}

bool write_font_probe(const fs::path& root, const std::string& marker) {
    const fs::path path = root / "fonts" / "Inter.ttf";
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    if (!stream.good()) return false;
    stream << marker;
    return stream.good();
}

c3d::Composition make_image_composition() {
    return c3d::Composition{
        c3d::CompositionSpec{
            .name = "sdk_assets_root_isolation",
            .width = kWidth,
            .height = kHeight,
            .frame_rate = c3d::FrameRate{30, 1},
            .duration = c3d::Frame{1},
            .assets_root = {},
        },
        [](const c3d::FrameContext& context) -> c3d::Scene {
            c3d::SceneBuilder builder{context};
            c3d::authoring::Scene scene{builder, context};
            scene.layer("fixture", [](c3d::authoring::Layer& layer) {
                layer.kind(c3d::LayerKind::Shape);
                (void)layer.image(
                    "fixture_image",
                    c3d::authoring::asset("fixture.png"));
            });
            return builder.build();
        },
    };
}

c3d::sdk::RenderSettings deterministic_settings() {
    c3d::sdk::RenderSettings settings{};
    settings.width = kWidth;
    settings.height = kHeight;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    settings.dirty_rects = false;
    settings.motion_blur = false;
    settings.max_threads = 1;
    return settings;
}

Rgb8 pixel_at(const c3d::sdk::RenderOutput& output, int x, int y) {
    if (output.pixels == nullptr || x < 0 || y < 0 ||
        x >= output.width || y >= output.height) {
        throw std::out_of_range("pixel_at received an invalid RenderOutput coordinate");
    }

    const std::size_t stride = output.bytes_per_row > 0
        ? static_cast<std::size_t>(output.bytes_per_row)
        : static_cast<std::size_t>(output.width) * 4u;
    const std::size_t offset = static_cast<std::size_t>(y) * stride
        + static_cast<std::size_t>(x) * 4u;

    if (output.format == c3d::sdk::PixelFormat::Bgra8) {
        return Rgb8{
            output.pixels[offset + 2],
            output.pixels[offset + 1],
            output.pixels[offset + 0],
        };
    }
    return Rgb8{
        output.pixels[offset + 0],
        output.pixels[offset + 1],
        output.pixels[offset + 2],
    };
}

void remove_test_root(const fs::path& root) {
    std::error_code error;
    fs::remove_all(root, error);
    CHECK_FALSE(error);
}

} // namespace

TEST_CASE("SDK asset roots remain isolated between RenderEngine instances") {
    const fs::path test_root = unique_test_root();
    const fs::path root_a = fs::absolute(test_root / "root-a");
    const fs::path root_b = fs::absolute(test_root / "root-b");
    const fs::path unrelated_cwd = fs::absolute(test_root / "unrelated-cwd");

    fs::create_directories(unrelated_cwd);
    REQUIRE(write_solid_png(
        root_a / "fixture.png", c3d::Color{1.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(write_solid_png(
        root_b / "fixture.png", c3d::Color{0.0f, 1.0f, 0.0f, 1.0f}));
    REQUIRE(write_font_probe(root_a, "font-a"));
    REQUIRE(write_font_probe(root_b, "font-b"));

    {
        CwdGuard cwd_guard;
        fs::current_path(unrelated_cwd);

        const c3d::Composition composition = make_image_composition();
        const auto settings = deterministic_settings();

        c3d::sdk::RenderEngine engine_a{settings};
        c3d::sdk::RenderEngine engine_b{settings};
        engine_a.set_assets_root(root_a);
        engine_b.set_assets_root(root_b);

        auto result_a = engine_a.render(composition, c3d::sdk::Frame{0});
        auto result_b = engine_b.render(composition, c3d::sdk::Frame{0});
        REQUIRE(result_a.has_value());
        REQUIRE(result_b.has_value());

        const Rgb8 pixel_a = pixel_at(result_a.value(), 20, 20);
        const Rgb8 pixel_b = pixel_at(result_b.value(), 20, 20);
        CHECK(pixel_a.r > 200);
        CHECK(pixel_a.g < 40);
        CHECK(pixel_b.g > 200);
        CHECK(pixel_b.r < 40);

        // Rendering through B must not mutate A's resolver or cache identity.
        auto result_a_again = engine_a.render(composition, c3d::sdk::Frame{0});
        REQUIRE(result_a_again.has_value());
        const Rgb8 pixel_a_again = pixel_at(result_a_again.value(), 20, 20);
        CHECK(pixel_a_again.r > 200);
        CHECK(pixel_a_again.g < 40);

        // The same logical font path resolves independently and ignores CWD.
        c3d::assets::AssetResolver resolver_a;
        c3d::assets::AssetResolver resolver_b;
        resolver_a.mount(root_a);
        resolver_b.mount(root_b);
        const auto font_a = resolver_a.resolve("fonts/Inter.ttf");
        const auto font_b = resolver_b.resolve("fonts/Inter.ttf");
        REQUIRE(font_a.has_value());
        REQUIRE(font_b.has_value());
        CHECK(font_a.value() != font_b.value());

        cwd_guard.restore();
    }

    remove_test_root(test_root);
}

TEST_CASE("AssetResolver fails closed for missing relative assets") {
    const fs::path test_root = unique_test_root();
    const fs::path empty_root = fs::absolute(test_root / "empty-root");
    const fs::path unrelated_cwd = fs::absolute(test_root / "cwd");
    fs::create_directories(empty_root);
    fs::create_directories(unrelated_cwd / "fonts");

    // A file with the requested name exists in CWD. It must not be used.
    {
        std::ofstream accidental(unrelated_cwd / "fonts" / "Inter.ttf");
        REQUIRE(accidental.good());
        accidental << "cwd-fallback-must-not-be-used";
    }

    {
        CwdGuard cwd_guard;
        fs::current_path(unrelated_cwd);

        c3d::assets::AssetResolver resolver;
        CHECK_FALSE(resolver.resolve("fonts/Inter.ttf").has_value());
        resolver.mount(empty_root);
        CHECK_FALSE(resolver.resolve("fonts/Inter.ttf").has_value());
        CHECK(resolver.resolve_lexical("fonts/Inter.ttf").has_value());
        CHECK_FALSE(resolver.resolve("../escape.ttf").has_value());

        cwd_guard.restore();
    }

    remove_test_root(test_root);
}
