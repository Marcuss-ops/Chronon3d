#include <doctest/doctest.h>

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── Replicates minimalist_image_tracking_breathing() from ─────────────
// ── content/Minimalist/minimalist_image_presets.cpp                  ──

const std::string IMAGE_PATH = "assets/images/minimalist_landscape.png";
const Vec2 IMAGE_SIZE = {800.0f, 450.0f};

void add_common_background(SceneBuilder& s) {
    s.layer("background", [](auto& l) {
        l.cache_static();
        l.pin_to(Anchor::Center);
        l.grid_background("grid_bg", {
            .size = {1920.0f, 1080.0f},
            .offset = {0.0f, 0.0f},
            .bg_color = {0.025f, 0.027f, 0.031f, 1.0f},
            .grid_color = {0.58f, 0.61f, 0.66f, 0.045f},
            .spacing = 136.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.0f,
            .major_every = 4,
            .centered = true
        });
    });
}

void add_image_border(LayerBuilder& l, Vec2 size) {
    l.rounded_rect("image_backdrop", {
        .size = size + Vec2{24.0f, 24.0f},
        .radius = 16.0f,
        .color = Color{0.0f, 0.0f, 0.0f, 0.35f},
        .pos = {0.0f, 0.0f, 0.0f}
    });
    l.rounded_rect("image_border", {
        .size = size + Vec2{2.0f, 2.0f},
        .radius = 10.0f,
        .color = Color{0.25f, 0.27f, 0.31f, 0.8f},
        .pos = {0.0f, 0.0f, 0.0f}
    });
}

Composition make_breathing_comp() {
    return composition(
        {.name = "MinimalistImageTrackingBreathing", .duration = 150},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_common_background(s);
            s.layer("image_layer", [](auto& l) {
                l.pin_to(Anchor::Center);
                l.tracking_breathing(1.04f, Frame{120});
                add_image_border(l, IMAGE_SIZE);
                l.image("img", {
                    .path = IMAGE_PATH,
                    .size = IMAGE_SIZE,
                    .radius = 8.0f
                });
            });
            return s.build();
        }
    );
}

// ── Comparison helpers ─────────────────────────────────────────────────

struct CompareResult {
    bool success{true};
    float max_channel_diff{0.0f};
    float mean_error{0.0f};
    float mismatch_pct{0.0f};
    std::string error_message;
};

CompareResult compare_framebuffers(const Framebuffer& rendered,
                                   const Framebuffer& golden) {
    CompareResult res;
    if (rendered.width() != golden.width() ||
        rendered.height() != golden.height()) {
        res.success = false;
        res.error_message =
            "Size mismatch: " + std::to_string(rendered.width()) + "x" +
            std::to_string(rendered.height()) + " vs " +
            std::to_string(golden.width()) + "x" +
            std::to_string(golden.height());
        return res;
    }

    double total_err = 0.0;
    int mismatched = 0;
    const int total = rendered.width() * rendered.height();

    for (int y = 0; y < rendered.height(); ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            // Both rendered (linear) and golden (sRGB PNG loaded as [0,1]) must be
        // compared in the same color space.  save_png() converts linear→sRGB
        // before writing, so convert rendered to sRGB to match.
        const Color a = rendered.get_pixel(x, y).to_srgb();
            const Color b = golden.get_pixel(x, y);
            const float dr = std::abs(a.r - b.r);
            const float dg = std::abs(a.g - b.g);
            const float db = std::abs(a.b - b.b);
            const float da = std::abs(a.a - b.a);
            const float max_d = std::max({dr, dg, db, da});
            res.max_channel_diff = std::max(res.max_channel_diff, max_d);
            total_err += dr + dg + db + da;
            if (max_d > 3.0f / 255.0f) ++mismatched;
        }
    }

    res.mean_error = static_cast<float>(total_err / (total * 4));
    res.mismatch_pct = static_cast<float>(mismatched) / static_cast<float>(total);

    // Tolerances: allow up to 0.5% mismatched pixels (floating-point
    // differences in SIMD vs scalar paths are possible).
    if (res.mismatch_pct > 0.005f || res.mean_error >= 0.005f ||
        res.max_channel_diff >= 5.0f / 255.0f) {
        res.success = false;
        res.error_message =
            "Threshold exceeded: mismatched=" +
            std::to_string(res.mismatch_pct * 100.0f) +
            "%, meanError=" + std::to_string(res.mean_error) +
            ", maxChannelErr=" +
            std::to_string(res.max_channel_diff * 255.0f) + "/255";
    }
    return res;
}

void verify_golden_or_create(const Framebuffer& rendered,
                              const std::string& filename) {
    const std::filesystem::path golden_dir = "test_renders/golden/breathing";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / filename;

    if (!std::filesystem::exists(golden_path)) {
        // First run: save the golden image
        REQUIRE(save_png(rendered, golden_path.string()));
        MESSAGE("Golden image created: " << golden_path.string());
        return;
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);

    const auto cmp = compare_framebuffers(rendered, *golden);
    INFO(cmp.error_message);
    CHECK(cmp.success);
}

} // namespace

TEST_CASE("Golden: MinimalistImageTrackingBreathing frame 50") {
    REQUIRE_MESSAGE(
        std::filesystem::exists(IMAGE_PATH),
        "Asset not found: " << IMAGE_PATH << ". "
        "This test requires the minimalist landscape image to render.");

    auto renderer = make_renderer();
    const auto comp = make_breathing_comp();

    const Frame test_frame{50};
    auto rendered = renderer.render_frame(comp, test_frame);
    REQUIRE(rendered != nullptr);

    // Verify dimensions
    CHECK(rendered->width() == 1920);
    CHECK(rendered->height() == 1080);

    // Verify the image is not blank (breathing ~1.02 scale, image visible)
    const float avg_luma = average_luma_rect(*rendered, 0, 0, 1920, 1080);
    CHECK(avg_luma > 0.01f);

    // Golden comparison
    verify_golden_or_create(*rendered, "breathing_frame_050.png");
}

TEST_CASE("Golden: MinimalistImageTrackingBreathing frame 1 (static check)") {
    if (!std::filesystem::exists(IMAGE_PATH)) {
        MESSAGE("Skipping: asset not found: " << IMAGE_PATH);
        return;
    }

    auto renderer = make_renderer();
    const auto comp = make_breathing_comp();

    const Frame test_frame{1};
    auto rendered = renderer.render_frame(comp, test_frame);
    REQUIRE(rendered != nullptr);

    // Frame 1: tracking_breathing at scale ~1.0003 (barely started)
    // Image should be non-blank
    const float avg_luma = average_luma_rect(*rendered, 0, 0, 1920, 1080);
    CHECK(avg_luma > 0.01f);

    verify_golden_or_create(*rendered, "breathing_frame_001.png");
}

TEST_CASE("Golden: consecutive frames produce different output (animation)") {
    if (!std::filesystem::exists(IMAGE_PATH)) {
        MESSAGE("Skipping: asset not found: " << IMAGE_PATH);
        return;
    }

    auto renderer = make_renderer();
    const auto comp = make_breathing_comp();

    auto fb0 = renderer.render_frame(comp, Frame{0});
    auto fb25 = renderer.render_frame(comp, Frame{25});
    auto fb50 = renderer.render_frame(comp, Frame{50});

    REQUIRE(fb0 != nullptr);
    REQUIRE(fb25 != nullptr);
    REQUIRE(fb50 != nullptr);

    // All three frames should have different pixel hashes
    // (tracking_breathing is animating scale across frames)
    const u64 h0 = framebuffer_hash(*fb0);
    const u64 h25 = framebuffer_hash(*fb25);
    const u64 h50 = framebuffer_hash(*fb50);

    CHECK(h0 != h25);
    CHECK(h25 != h50);
    CHECK(h0 != h50);
}
