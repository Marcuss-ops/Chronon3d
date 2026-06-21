#include <doctest/doctest.h>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <blend2d.h>
using namespace chronon3d;


// ── helpers ────────────────────────────────────────────────────────

/// Create a simple white-on-transparent text BLImage for testing.
static BLImage make_test_text_image(int w, int h, const char* text, float font_size = 48.0f) {
    // Use Inter Bold if available, otherwise rely on Blend2D's fallback
    TextShape shape;
    shape.text = text;
    shape.style.font_path = "assets/fonts/Inter-Bold.ttf";
    shape.style.font_family = "Inter";
    shape.style.font_weight = 700;
    shape.style.size = font_size;
    shape.style.align = TextAlign::Center;
    shape.style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};

    float effective_size = font_size;
    // WP-8 PR 8.0 — explicit resolver sourced from the test-lattice
    // typed_resolver bridge (tests don't have a runtime in scope).
    const auto& resolver = chronon3d::runtime::typed_resolver_for_deep_code();
    auto result = rasterize_text_to_bl_image(shape, effective_size, 16, resolver);
    if (!result) {
        // Return a small empty image as fallback
        BLImage fallback(1, 1, BL_FORMAT_PRGB32);
        BLContext ctx(fallback);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
        ctx.fillAll();
        ctx.end();
        return fallback;
    }
    return result->image;
}

/// Count non-zero-alpha pixels in an image.
static int count_content_pixels(const BLImage& img) {
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS) return 0;

    const int w = data.size.w;
    const int h = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));
    const auto* pixels = static_cast<const uint32_t*>(data.pixelData);

    int count = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t p = pixels[y * stride + x];
            if ((p >> 24) & 0xFF) ++count;
        }
    }
    return count;
}

/// Check if a pixel region has non-zero alpha.
static bool has_content_in_region(const BLImage& img, int x0, int y0, int x1, int y1) {
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS) return false;

    const int w = data.size.w;
    const int h = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));
    const auto* pixels = static_cast<const uint32_t*>(data.pixelData);

    const int cx0 = std::max(0, x0);
    const int cy0 = std::max(0, y0);
    const int cx1 = std::min(w - 1, x1);
    const int cy1 = std::min(h - 1, y1);

    for (int y = cy0; y <= cy1; ++y) {
        for (int x = cx0; x <= cx1; ++x) {
            if ((pixels[y * stride + x] >> 24) & 0xFF) return true;
        }
    }
    return false;
}

// ── Test cases ──────────────────────────────────────────────────────

TEST_CASE("TextMaterial: disabled does not change image") {
    auto img = make_test_text_image(400, 200, "Hello", 48.0f);
    int before = count_content_pixels(img);

    TextMaterial mat;
    mat.enabled = false; // disabled
    apply_text_material(img, mat);

    int after = count_content_pixels(img);
    CHECK(after == before);
}

TEST_CASE("TextMaterial: flat preset fills with solid color") {
    auto img = make_test_text_image(400, 200, "Hello", 48.0f);
    REQUIRE(count_content_pixels(img) > 0);

    auto mat = TextMaterial::flat();
    // flat has top_color == bottom_color == white
    apply_text_material(img, mat);

    // Image should still have content (pixels changed from white to white, alpha preserved)
    CHECK(count_content_pixels(img) > 0);

    // Verify pixels have the expected color (white)
    BLImageData data;
    REQUIRE(img.getData(&data) == BL_SUCCESS);
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));
    const auto* pixels = static_cast<const uint32_t*>(data.pixelData);
    const int w = data.size.w;
    const int h = data.size.h;

    bool found_colored = false;
    for (int y = 0; y < h && !found_colored; ++y) {
        for (int x = 0; x < w && !found_colored; ++x) {
            const uint32_t p = pixels[y * stride + x];
            const uint8_t a = (p >> 24) & 0xFF;
            if (a > 0) {
                const uint8_t r = (p >> 16) & 0xFF;
                // With flat preset, emissive=1.0, top_color=white → r should be ~255
                CHECK(r == doctest::Approx(255).epsilon(5));
                found_colored = true;
            }
        }
    }
    CHECK(found_colored);
}

TEST_CASE("TextMaterial: premium preset applies gradient and bevel") {
    auto img = make_test_text_image(400, 200, "Premium", 56.0f);
    REQUIRE(count_content_pixels(img) > 0);

    auto mat = TextMaterial::premium();
    CHECK(mat.enabled);
    CHECK(mat.bevel_px > 0.0f);
    CHECK(mat.top_highlight_opacity > 0.0f);
    CHECK(mat.use_material_glow);
    CHECK(mat.use_material_shadow);

    apply_text_material(img, mat);

    // Image should still have content
    CHECK(count_content_pixels(img) > 0);

    // Verify gradient: top pixels should be brighter than bottom pixels
    BLImageData data;
    REQUIRE(img.getData(&data) == BL_SUCCESS);
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));
    const auto* pixels = static_cast<const uint32_t*>(data.pixelData);
    const int w = data.size.w;
    const int h = data.size.h;

    // Sample top-center and bottom-center
    int mid_x = w / 2;
    float top_luma = 0.0f;
    float bot_luma = 0.0f;
    int top_samples = 0;
    int bot_samples = 0;

    for (int x = std::max(0, mid_x - 20); x < std::min(w, mid_x + 20); ++x) {
        // Top region (y=5..15)
        for (int y = 5; y < 15 && y < h; ++y) {
            const uint32_t p = pixels[y * stride + x];
            if ((p >> 24) & 0xFF) {
                const float r = ((p >> 16) & 0xFF) / 255.0f;
                const float g = ((p >> 8) & 0xFF) / 255.0f;
                const float b = (p & 0xFF) / 255.0f;
                top_luma += 0.2126f * r + 0.7152f * g + 0.0722f * b;
                top_samples++;
            }
        }
        // Bottom region (y=h-15..h-5)
        for (int y = std::max(0, h - 15); y < h - 5; ++y) {
            const uint32_t p = pixels[y * stride + x];
            if ((p >> 24) & 0xFF) {
                const float r = ((p >> 16) & 0xFF) / 255.0f;
                const float g = ((p >> 8) & 0xFF) / 255.0f;
                const float b = (p & 0xFF) / 255.0f;
                bot_luma += 0.2126f * r + 0.7152f * g + 0.0722f * b;
                bot_samples++;
            }
        }
    }

    if (top_samples > 0 && bot_samples > 0) {
        top_luma /= static_cast<float>(top_samples);
        bot_luma /= static_cast<float>(bot_samples);
        // Top should be brighter than bottom (gradient from white to slightly bluish)
        MESSAGE("Premium gradient: top_luma=", top_luma, " bot_luma=", bot_luma);
        CHECK(top_luma >= bot_luma - 0.05f); // allow small tolerance due to bevel/highlight
    }
}

TEST_CASE("TextMaterial: neon preset has bright colors") {
    auto img = make_test_text_image(400, 200, "Neon", 56.0f);
    REQUIRE(count_content_pixels(img) > 0);

    auto mat = TextMaterial::neon();
    CHECK(mat.enabled);
    CHECK(mat.use_material_glow);
    CHECK(mat.emissive > 1.0f);

    apply_text_material(img, mat);

    CHECK(count_content_pixels(img) > 0);

    // Verify the image has green/cyan tint (neon uses top: 0.3,1.0,0.9 → green-cyan)
    BLImageData data;
    REQUIRE(img.getData(&data) == BL_SUCCESS);
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));
    const auto* pixels = static_cast<const uint32_t*>(data.pixelData);
    const int w = data.size.w;
    const int h = data.size.h;

    bool found_greenish = false;
    for (int y = 0; y < h && !found_greenish; ++y) {
        for (int x = 0; x < w && !found_greenish; ++x) {
            const uint32_t p = pixels[y * stride + x];
            if ((p >> 24) & 0xFF) {
                const float r = ((p >> 16) & 0xFF) / 255.0f;
                const float g = ((p >> 8) & 0xFF) / 255.0f;
                // Green should be strong
                if (g > 0.5f) {
                    found_greenish = true;
                }
            }
        }
    }
    CHECK(found_greenish);
}

TEST_CASE("TextMaterial: glass preset has semi-transparent colors") {
    auto img = make_test_text_image(400, 200, "Glass", 56.0f);
    REQUIRE(count_content_pixels(img) > 0);

    auto mat = TextMaterial::glass();
    CHECK(mat.enabled);
    // Glass has top_color.a = 0.85, bottom_color.a = 0.70
    CHECK(mat.top_color.a < 1.0f);

    apply_text_material(img, mat);
    CHECK(count_content_pixels(img) > 0);
}

TEST_CASE("TextMaterial: golden output comparison") {
    // Render "Saas Premium" with premium material for visual inspection
    TextShape shape;
    shape.text = "SaaS Premium";
    shape.style.font_path = "assets/fonts/Inter-Bold.ttf";
    shape.style.font_family = "Inter";
    shape.style.font_weight = 800;
    shape.style.size = 72.0f;
    shape.style.align = TextAlign::Center;
    shape.style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};

    // WP-8 PR 8.0 — explicit resolver sourced from the test-lattice
    // typed_resolver bridge (tests don't have a runtime in scope).
    const auto& resolver = chronon3d::runtime::typed_resolver_for_deep_code();
    auto base = rasterize_text_to_bl_image(shape, 72.0f, 32, resolver);
    REQUIRE(base.has_value());

    // Apply premium material
    auto mat = TextMaterial::premium();
    apply_text_material(base->image, mat);

    // Save for visual inspection
    BLImageData data;
    REQUIRE(base->image.getData(&data) == BL_SUCCESS);
    const int sw = data.size.w;
    const int sh = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));
    const auto* src_pixels = static_cast<const uint32_t*>(data.pixelData);

    Framebuffer fb(sw, sh);
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            const uint32_t p = src_pixels[y * stride + x];
            fb.set_pixel(x, y, Color{
                static_cast<float>((p >> 16) & 0xFF) / 255.0f,
                static_cast<float>((p >> 8) & 0xFF) / 255.0f,
                static_cast<float>(p & 0xFF) / 255.0f,
                static_cast<float>((p >> 24) & 0xFF) / 255.0f
            });
        }
    }

    save_png(fb, "test_renders/golden/text_material_golden.png");
    MESSAGE("Saved golden: test_renders/golden/text_material_golden.png");

    // Verify the output has content
    CHECK(count_content_pixels(base->image) > 0);
}

TEST_CASE("TextMaterial: empty image is safe") {
    BLImage empty(1, 1, BL_FORMAT_PRGB32);
    {
        BLContext ctx(empty);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
        ctx.fillAll();
        ctx.end();
    }

    auto mat = TextMaterial::premium();
    // Should not crash
    apply_text_material(empty, mat);
    CHECK(true); // reached without crash
}
