#include <doctest/doctest.h>

#include <chronon3d/backends/text/text_scratch_pool.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <blend2d.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace chronon3d;

namespace {

BLImage make_small_glyph_mask(int width, int height) {
    BLImage image;
    if (image.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS) return image;
    BLImageData data;
    if (image.getData(&data) != BL_SUCCESS || !data.pixelData) return {};
    auto* pixels = static_cast<std::uint32_t*>(data.pixelData);
    const int stride = static_cast<int>(data.stride / sizeof(std::uint32_t));
    std::fill_n(pixels, static_cast<std::size_t>(stride) * height, 0u);

    // Compact block glyphs with narrow counters and diagonals. They preserve
    // enough edge detail to exercise sub-2px bevels without depending on a
    // font installation or a platform-specific rasterizer.
    for (int y = 2; y < height - 2; ++y) {
        for (int x = 2; x < width - 2; ++x) {
            const int gx = x % std::max(5, width / 8);
            const int gy = y % std::max(7, height - 4);
            const bool stem = gx < 2 || gx > width / 8 - 3;
            const bool bar = gy < 2 || gy > height - 6;
            if (stem || bar || std::abs(gx - gy) <= 1)
                pixels[y * stride + x] = 0xFFFFFFFFu;
        }
    }
    return image;
}

std::vector<std::uint32_t> pixels_of(const BLImage& image) {
    BLImageData data;
    if (image.getData(&data) != BL_SUCCESS || !data.pixelData) return {};
    const int stride = static_cast<int>(data.stride / sizeof(std::uint32_t));
    const auto* pixels = static_cast<const std::uint32_t*>(data.pixelData);
    std::vector<std::uint32_t> result;
    result.reserve(static_cast<std::size_t>(data.size.w) * data.size.h);
    for (int y = 0; y < data.size.h; ++y)
        result.insert(result.end(), pixels + y * stride, pixels + y * stride + data.size.w);
    return result;
}

} // namespace

TEST_CASE("premium text bevel remains bounded at small raster scales") {
    const TextMaterial premium = TextMaterial::premium();
    REQUIRE(premium.enabled);
    REQUIRE(premium.bevel_px > 0.0f);

    for (const auto [width, height] : {std::pair{36, 16}, std::pair{81, 36}}) {
        BLImage image = make_small_glyph_mask(width, height);
        REQUIRE(image.width() == width);
        TextScratchState scratch;
        apply_text_material(image, premium, scratch);
        const auto pixels = pixels_of(image);
        REQUIRE(pixels.size() == static_cast<std::size_t>(width * height));
        CHECK(std::any_of(pixels.begin(), pixels.end(), [](std::uint32_t p) {
            return (p >> 24u) != 0u && (p & 0x00FFFFFFu) != 0x00FFFFFFu;
        }));
        CHECK(std::all_of(pixels.begin(), pixels.end(), [](std::uint32_t p) {
            const auto a = (p >> 24u) & 0xFFu;
            const auto r = (p >> 16u) & 0xFFu;
            const auto g = (p >> 8u) & 0xFFu;
            const auto b = p & 0xFFu;
            return r <= a && g <= a && b <= a; // PRGB32 stays valid.
        }));
    }
}

TEST_CASE("premium text material keeps gradient and bevel deterministic") {
    BLImage first = make_small_glyph_mask(96, 40);
    BLImage second = make_small_glyph_mask(96, 40);
    REQUIRE(first.width() == 96);

    TextMaterial material = TextMaterial::premium();
    material.top_color = Color{1.0f, 0.2f, 0.1f, 1.0f};
    material.bottom_color = Color{0.1f, 0.2f, 1.0f, 1.0f};
    TextScratchState scratch_a;
    TextScratchState scratch_b;
    apply_text_material(first, material, scratch_a);
    apply_text_material(second, material, scratch_b);

    const auto a = pixels_of(first);
    const auto b = pixels_of(second);
    REQUIRE(a.size() == b.size());
    CHECK(a == b);

    const auto find_colored_pixel = [](const std::vector<std::uint32_t>& pixels,
                                       int width, int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 8; x < width - 8; ++x) {
                const auto p = pixels[static_cast<std::size_t>(y * width + x)];
                if ((p >> 24u) > 200u) return p;
            }
        return std::uint32_t{0};
    };
    const auto top = find_colored_pixel(a, 96, 3, 14);
    const auto bottom = find_colored_pixel(a, 96, 25, 38);
    REQUIRE(top != 0u);
    REQUIRE(bottom != 0u);
    CHECK(((top >> 16u) & 0xFFu) > (top & 0xFFu));
    CHECK((bottom & 0xFFu) > ((bottom >> 16u) & 0xFFu));
}

TEST_CASE("premium bevel text stays deterministic under camera shutter sampling") {
    auto renderer = test::make_renderer();
    const auto scene = composition({.name = "PremiumTextShutter", .width = 320,
                                    .height = 180, .duration = 40},
        [&renderer](const FrameContext& ctx) {
            SceneBuilder builder(ctx);
            builder.font_engine(&renderer.font_engine());
            camera_v1::CameraDescriptor camera;
            camera.id = "premium-text-shutter-camera";
            camera.base.enabled = true;
            camera.base.projection = camera_v1::ZoomProjection{AnimatedValue<f32>{700.0f}};
            camera.base.motion_blur.mode = MotionBlurMode::TemporalAccumulation;
            camera.base.motion_blur.samples = 8;
            camera_v1::PoseTracksSource motion;
            motion.position.set(Vec3{0, 0, -700});
            motion.position.key(0, Vec3{0, 0, -700});
            motion.position.key(40, Vec3{0, 0, -360});
            motion.target.set(Vec3{0, 0, 0});
            motion.use_target = true;
            camera.source = motion;
            camera.orientation = camera_v1::LookAtPoint{Vec3{0, 0, 0}};
            builder.camera_descriptor(std::move(camera));

            TextDefinition title;
            title.content.value = "HUMANITY";
            title.style.font = {.font_path = std::string(CHRONON3D_SOURCE_DIR) +
                                               "/assets/fonts/Inter-Bold.ttf",
                                .font_family = "Inter", .font_weight = 700,
                                .font_size = 64.0f};
            title.style.color = Color::white();
            title.style.material = TextMaterial::premium();
            title.frame.size = {300.0f, 96.0f};
            title.frame.placement = TextPlacement{TextPlacementKind::CanvasCenter, {0, 0}};
            title.frame.anchor = TextAnchor::Center;
            title.frame.align = TextAlign::Center;
            title.frame.vertical_align = VerticalAlign::Middle;
            builder.layer("premium-title", [&renderer, title](LayerBuilder& layer) {
                layer.font_engine(&renderer.font_engine());
                layer.enable_3d().text("humanity", title);
            });
            return builder.build();
        });
    const auto first = renderer.render(scene, Frame{20});
    const auto repeated = renderer.render(scene, Frame{20});
    REQUIRE(first);
    REQUIRE(repeated);
    CHECK(test::framebuffer_hash(*first) == test::framebuffer_hash(*repeated));

    std::size_t visible_pixels = 0;
    for (int y = 0; y < first->height(); ++y)
        for (int x = 0; x < first->width(); ++x) {
            const Color p = first->get_pixel(x, y);
            if (p.r > 0.02f || p.g > 0.02f || p.b > 0.02f) ++visible_pixels;
        }
    CHECK(visible_pixels > 100);
}
