#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/composition_registration.hpp>

#include <cstdint>
#include <memory>

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

u64 frame_signature(const Framebuffer& fb) {
    u64 h = 1469598103934665603ULL;
    for (int y = 0; y < fb.height(); y += 8) {
        for (int x = 0; x < fb.width(); x += 8) {
            const Color c = fb.get_pixel(x, y);
            const u64 v =
                (static_cast<u64>(c.r * 255.0f) << 24) ^
                (static_cast<u64>(c.g * 255.0f) << 16) ^
                (static_cast<u64>(c.b * 255.0f) << 8) ^
                static_cast<u64>(c.a * 255.0f);
            h ^= v;
            h *= 1099511628211ULL;
        }
    }
    return h;
}

std::size_t opaque_pixels(const Framebuffer& fb, float threshold = 0.08f) {
    std::size_t count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (fb.get_pixel(x, y).a > threshold) {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

TEST_CASE("MotionObjectProof renders visible animated content") {
    CompositionRegistry registry;
    auto comp = registry.create("MotionObjectProof");
    auto renderer = make_renderer();

    auto f0 = renderer.render_frame(comp, 0);
    auto f60 = renderer.render_frame(comp, 60);

    REQUIRE(f0 != nullptr);
    REQUIRE(f60 != nullptr);

    CHECK(opaque_pixels(*f0) > 25000);
    CHECK(opaque_pixels(*f60) > 30000);
    CHECK(frame_signature(*f0) != frame_signature(*f60));
}

TEST_CASE("MotionPresetGallery animates all presets and changes over time") {
    CompositionRegistry registry;
    auto comp = registry.create("MotionPresetGallery");
    auto renderer = make_renderer();

    auto f0 = renderer.render_frame(comp, 0);
    auto f120 = renderer.render_frame(comp, 120);

    REQUIRE(f0 != nullptr);
    REQUIRE(f120 != nullptr);

    CHECK(opaque_pixels(*f0) > 15000);
    CHECK(opaque_pixels(*f120) > 15000);
    CHECK(frame_signature(*f0) != frame_signature(*f120));
}

TEST_CASE("MotionDepthProof preserves depth layers and parallax") {
    CompositionRegistry registry;
    auto comp = registry.create("MotionDepthProof");
    auto renderer = make_renderer();

    auto f0 = renderer.render_frame(comp, 0);
    auto f89 = renderer.render_frame(comp, 89);

    REQUIRE(f0 != nullptr);
    REQUIRE(f89 != nullptr);

    CHECK(opaque_pixels(*f0) > 20000);
    CHECK(opaque_pixels(*f89) > 20000);
    CHECK(frame_signature(*f0) != frame_signature(*f89));
}
