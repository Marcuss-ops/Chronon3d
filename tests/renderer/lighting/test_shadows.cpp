#include <doctest/doctest.h>
#include <xxhash.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/framebuffer_analysis.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <algorithm>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

u64 framebuffer_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

float average_luma_rect(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    double sum = 0.0;
    int count = 0;
    for (int y = std::max(0, y0); y < std::min(fb.height(), y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(fb.width(), x1); ++x) {
            const Color c = fb.get_pixel(x, y);
            if (c.a <= 0.0f) continue;
            sum += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / count) : 0.0f;
}

// Scene with a light-gray floor (accepts_shadows) and a red card above (casts_shadows).
Composition make_shadow_scene(bool casts, bool accepts,
                               Vec3 light_dir = {-0.4f, 1.0f, -0.6f}) {
    return composition({
        .name = "ShadowScene",
        .width = 320,
        .height = 240,
        .duration = 1
    }, [casts, accepts, light_dir](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0, 0, -800}).zoom(800).look_at({0, 0, 0});
        s.ambient_light(Color{1, 1, 1, 1}, 0.15f);
        s.directional_light(light_dir, Color{1, 1, 1, 1}, 0.85f);

        s.layer("floor", [accepts](LayerBuilder& l) {
            l.enable_3d().position({0, 60, 150}).accepts_shadows(accepts);
            l.rect("fill", {.size = {280, 100}, .color = {0.8f, 0.8f, 0.8f, 1}});
        });

        s.layer("card", [casts](LayerBuilder& l) {
            l.enable_3d().position({0, -20, 0}).casts_shadows(casts);
            l.rect("fill", {.size = {80, 80}, .color = {1, 0.2f, 0.1f, 1}});
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("Shadow builder: casts_shadows sets material flag") {
    LayerBuilder lb("test");
    lb.enable_3d().casts_shadows(true);
    Layer l = lb.build();
    CHECK(l.material.casts_shadows == true);
}

TEST_CASE("Shadow builder: accepts_shadows sets material flag") {
    LayerBuilder lb("test");
    lb.enable_3d().accepts_shadows(false);
    Layer l = lb.build();
    CHECK(l.material.accepts_shadows == false);
}
