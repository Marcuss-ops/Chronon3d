// Standalone installed-SDK consumer for the focused typed asset surface.
// Includes are explicit by design: no umbrella header is permitted.

#include <chronon3d/assets/asset_ref.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace c3d = chronon3d;
namespace fs = std::filesystem;

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 128;
constexpr int kImageProbeX = 50;
constexpr int kImageProbeY = 70;

struct Rgb8 {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
};

bool write_solid_png(const fs::path& path, c3d::Color color) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) return false;

    c3d::Framebuffer framebuffer{16, 16};
    framebuffer.clear(color);
    return c3d::save_png(framebuffer, path.string());
}

bool copy_font(const fs::path& source, const fs::path& root) {
    std::error_code error;
    const fs::path destination = root / "fonts" / "Inter-Bold.ttf";
    fs::create_directories(destination.parent_path(), error);
    if (error) return false;
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, error);
    return !error;
}

Rgb8 pixel_at(const c3d::sdk::RenderOutput& output, int x, int y) {
    if (output.pixels == nullptr || x < 0 || y < 0 ||
        x >= output.width || y >= output.height) {
        return {};
    }

    const std::size_t stride = output.bytes_per_row > 0
        ? static_cast<std::size_t>(output.bytes_per_row)
        : static_cast<std::size_t>(output.width) * 4u;
    const std::size_t offset = static_cast<std::size_t>(y) * stride
        + static_cast<std::size_t>(x) * 4u;

    // SDK only reports canonical RGBA8 (Bgra8 spelling retired with the
    // duplicate sdk enum); no byte-swap branch needed.
    return {output.pixels[offset + 0], output.pixels[offset + 1],
            output.pixels[offset + 2]};
}

c3d::Composition make_composition() {
    const c3d::CompositionSpec spec{
        .name = "installed_typed_assets",
        .width = kWidth,
        .height = kHeight,
        .frame_rate = c3d::FrameRate{30, 1},
        .duration = c3d::Frame{1},
    };

    return c3d::composition(
        spec,
        [](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::SceneBuilder scene(ctx);
            scene.layer("logo", [](c3d::LayerBuilder& layer) {
                layer.kind(c3d::LayerKind::Shape);
                layer.image("logo", c3d::ImageParams{
                    .source = c3d::assets::ImageRef{"images/logo.png"},
                    .size = {64.0f, 64.0f},
                });
            });
            scene.layer("headline", [](c3d::LayerBuilder& layer) {
                layer.kind(c3d::LayerKind::Text);
                layer.text("headline", c3d::TextDefinition{
                    .content = {.value = "HELLO"},
                    .style = {
                        .font = {.font_path = "fonts/Inter-Bold.ttf",
                                 .font_family = "Inter",
                                 .font_weight = 700,
                                 .font_size = 28.0f},
                        .color = c3d::Color{1.0f, 1.0f, 1.0f, 1.0f},
                    },
                    .frame = {
                        .size = {kWidth, kHeight},
                        .placement = c3d::TextPlacement{
                            c3d::TextPlacementKind::Absolute, {8.0f, 8.0f}},
                        .align = c3d::TextAlign::Left,
                        .vertical_align = c3d::VerticalAlign::Top,
                    },
                });
            });
            return scene.build();
        });
}

c3d::sdk::RenderSettings make_settings() {
    c3d::sdk::RenderSettings settings{};
    settings.width = kWidth;
    settings.height = kHeight;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    settings.dirty_rects = false;
    settings.motion_blur = false;
    return settings;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path shared_assets = argc > 1 ? fs::path{argv[1]} : fs::path{"assets"};
    const fs::path font_source = shared_assets / "fonts" / "Inter-Bold.ttf";
    if (!fs::is_regular_file(font_source)) {
        std::fprintf(stderr, "[ASSETS-FAIL] source font missing: %s\n",
                     font_source.string().c_str());
        return 1;
    }

    const fs::path work_root = fs::absolute("sdk_authoring_asset_roots");
    const fs::path root_a = work_root / "root-a";
    const fs::path root_b = work_root / "root-b";
    const fs::path unrelated_cwd = work_root / "unrelated-cwd";
    std::error_code error;
    fs::remove_all(work_root, error);
    error.clear();
    fs::create_directories(unrelated_cwd, error);
    if (error ||
        !write_solid_png(root_a / "images" / "logo.png",
                         c3d::Color{1.0f, 0.0f, 0.0f, 1.0f}) ||
        !write_solid_png(root_b / "images" / "logo.png",
                         c3d::Color{0.0f, 1.0f, 0.0f, 1.0f}) ||
        !copy_font(font_source, root_a) ||
        !copy_font(font_source, root_b)) {
        std::fprintf(stderr, "[ASSETS-FAIL] cannot prepare asset roots\n");
        return 1;
    }

    const c3d::assets::ImageRef logical_image{"images/logo.png"};
    const c3d::assets::FontRef logical_font{"fonts/Inter-Bold.ttf"};
    c3d::assets::AssetResolver resolver_a;
    c3d::assets::AssetResolver resolver_b;
    resolver_a.mount(root_a);
    resolver_b.mount(root_b);
    const auto image_a = logical_image.resolve(resolver_a);
    const auto image_b = logical_image.resolve(resolver_b);
    const auto font_a = logical_font.resolve(resolver_a);
    const auto font_b = logical_font.resolve(resolver_b);
    if (!image_a || !image_b || !font_a || !font_b ||
        *image_a == *image_b || *font_a == *font_b) {
        std::fprintf(stderr,
                     "[ASSETS-FAIL] logical asset refs did not remain root-local\n");
        return 1;
    }

    const fs::path original_cwd = fs::current_path();
    fs::current_path(unrelated_cwd, error);
    if (error) {
        std::fprintf(stderr, "[ASSETS-FAIL] cannot switch to unrelated CWD\n");
        return 1;
    }

    const c3d::Composition composition = make_composition();
    const auto settings = make_settings();
    c3d::sdk::RenderEngine engine_a{settings};
    c3d::sdk::RenderEngine engine_b{settings};
    engine_a.set_assets_root(root_a);
    engine_b.set_assets_root(root_b);

    auto output_a = engine_a.render(composition, c3d::sdk::Frame{0});
    auto output_b = engine_b.render(composition, c3d::sdk::Frame{0});
    auto output_a_again = engine_a.render(composition, c3d::sdk::Frame{0});

    fs::current_path(original_cwd, error);
    if (error || !output_a || !output_b || !output_a_again) {
        std::fprintf(stderr,
                     "[ASSETS-FAIL] render failed or CWD could not be restored\n");
        return 1;
    }

    const Rgb8 a = pixel_at(output_a.value(), kImageProbeX, kImageProbeY);
    const Rgb8 b = pixel_at(output_b.value(), kImageProbeX, kImageProbeY);
    const Rgb8 a_again = pixel_at(
        output_a_again.value(), kImageProbeX, kImageProbeY);
    const bool isolated = a.r > 200 && a.g < 40 &&
                          b.g > 200 && b.r < 40 &&
                          a_again.r > 200 && a_again.g < 40;
    if (!isolated) {
        std::fprintf(stderr,
                     "[ASSETS-FAIL] render roots leaked: A=(%u,%u,%u) "
                     "B=(%u,%u,%u) A2=(%u,%u,%u)\n",
                     a.r, a.g, a.b, b.r, b.g, b.b,
                     a_again.r, a_again.g, a_again.b);
        return 1;
    }

    fs::remove_all(work_root, error);
    if (error) {
        std::fprintf(stderr, "[ASSETS-FAIL] cleanup failed: %s\n",
                     error.message().c_str());
        return 1;
    }

    std::printf(
        "[ASSETS-OK] installed typed asset headers compiled; logical image/font "
        "paths resolved per engine; CWD ignored; A=red B=green A2=red\n");
    return 0;
}
