#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/chronon3d.hpp>
#include <filesystem>
#include <cmath>

using namespace chronon3d;

namespace {

SoftwareRenderer make_renderer() {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    return renderer;
}

std::shared_ptr<Framebuffer> load_png_as_framebuffer(const std::string& path) {
    image::StbImageBackend backend;
    auto buf = backend.load_image(path);
    if (!buf) return nullptr;
    auto fb = std::make_shared<Framebuffer>(buf->width, buf->height);
    for (int y = 0; y < buf->height; ++y) {
        for (int x = 0; x < buf->width; ++x) {
            int idx = (y * buf->width + x) * 4;
            Color c{
                static_cast<float>(buf->pixels[idx]) / 255.0f,
                static_cast<float>(buf->pixels[idx+1]) / 255.0f,
                static_cast<float>(buf->pixels[idx+2]) / 255.0f,
                static_cast<float>(buf->pixels[idx+3]) / 255.0f
            };
            fb->set_pixel(x, y, c);
        }
    }
    return fb;
}

bool colors_near(const Color& c1, const Color& c2, float tolerance = 0.05f) {
    return std::abs(c1.r - c2.r) <= tolerance &&
           std::abs(c1.g - c2.g) <= tolerance &&
           std::abs(c1.b - c2.b) <= tolerance &&
           std::abs(c1.a - c2.a) <= tolerance;
}

} // namespace

TEST_CASE("Test 17.1 — Golden image baseline and pixel-by-pixel validation") {
    auto renderer = make_renderer();
    Composition comp({.width = 64, .height = 64}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("red_box", {.size={30, 30}, .color=Color::red(), .pos={32, 32, 0}});
        s.circle("blue_dot", {.radius=8.0f, .color=Color::blue(), .pos={32, 32, 0}});
        return s.build();
    });

    auto rendered = renderer.render_frame(comp, 0);
    REQUIRE(rendered != nullptr);

    const std::filesystem::path golden_dir = "test_renders/golden";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / "shapes_golden.png";

    // 1. Generate golden baseline if it does not exist
    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(*rendered, golden_path.string()));
    }

    // 2. Load golden image and compare pixel-by-pixel with tolerance
    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);
    REQUIRE(golden->width() == rendered->width());
    REQUIRE(golden->height() == rendered->height());

    bool matched = true;
    for (int y = 0; y < rendered->height(); ++y) {
        for (int x = 0; x < rendered->width(); ++x) {
            if (!colors_near(rendered->get_pixel(x, y), golden->get_pixel(x, y))) {
                matched = false;
                break;
            }
        }
    }
    CHECK(matched);
}

TEST_CASE("Test 17.2 — Framebuffer dimension and float boundary comparisons") {
    auto renderer = make_renderer();
    Composition comp({.width = 128, .height = 64}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("box", {.size={60, 30}, .color=Color::white(), .pos={64, 32, 0}});
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 128);
    CHECK(fb->height() == 64);

    // Float checks on specific pixel offsets (interpolation values near boundaries)
    Color boundary_left = fb->get_pixel(34, 32);
    Color boundary_right = fb->get_pixel(93, 32);

    CHECK(boundary_left.r > 0.9f);
    CHECK(boundary_right.r > 0.9f);
}

TEST_CASE("Test 17.3 — Pixel-level difference reporting on mismatch") {
    auto renderer = make_renderer();
    Composition comp({.width = 32, .height = 32}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("green_box", {.size={20, 20}, .color=Color::green(), .pos={16, 16, 0}});
        return s.build();
    });

    auto rendered = renderer.render_frame(comp, 0);
    REQUIRE(rendered != nullptr);

    // We intentionally create a mismatched fake "golden" image (completely black)
    Framebuffer fake_golden(32, 32);
    fake_golden.clear(Color::black());

    // Generate diff image highlighting mismatches
    Framebuffer diff(32, 32);
    diff.clear(Color::transparent());

    bool match = true;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            Color r_pixel = rendered->get_pixel(x, y);
            Color g_pixel = fake_golden.get_pixel(x, y);
            if (!colors_near(r_pixel, g_pixel)) {
                match = false;
                // Highlight different pixel in red
                diff.set_pixel(x, y, Color::red());
            } else {
                diff.set_pixel(x, y, Color{0, 0, 0, 0.2f}); // match shade
            }
        }
    }

    CHECK_FALSE(match); // Should fail comparison as expected

    // Write difference report image to disk
    const std::filesystem::path diff_dir = "output/debug";
    std::filesystem::create_directories(diff_dir);
    const std::filesystem::path diff_path = diff_dir / "diff_shapes.png";
    std::filesystem::remove(diff_path);

    REQUIRE(save_png(diff, diff_path.string()));
    CHECK(std::filesystem::exists(diff_path));
}
