#include <doctest/doctest.h>
#include <xxhash.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/framebuffer_analysis.hpp>
#include <tests/helpers/render_fixtures.hpp>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

u64 framebuffer_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

std::unique_ptr<Framebuffer> render_dof_scene(f32 focus_z, f32 card_z, bool enabled, f32 aperture) {
    return render_modular(composition({
        .name = "DoFVisual",
        .width = 320,
        .height = 240,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f,
            .dof = DepthOfFieldSettings{
                .enabled = enabled,
                .focus_z = focus_z,
                .aperture = aperture,
                .max_blur = 24.0f
            }
        });

        s.rect("bg", {
            .size = {320, 240},
            .color = Color::black(),
            .pos = {0, 0, 0}
        });

        s.layer("card", [=](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, card_z})
             .rect("fill", {
                 .size = {100, 100},
                 .color = Color::red(),
                 .pos = {0, 0, 0}
             });
        });

        return s.build();
    }));
}

int bbox_width(const Framebuffer& fb) {
    auto bbox = renderer::bright_bbox(fb, 0.01f);
    REQUIRE(bbox.has_value());
    return bbox->max_x - bbox->min_x + 1;
}

} // namespace

TEST_CASE("DOF visual: focused layer stays sharper than far layer") {
    auto focused = render_dof_scene(0.0f, 0.0f, true, 0.02f);
    auto blurred = render_dof_scene(0.0f, 500.0f, true, 0.02f);

    REQUIRE(focused != nullptr);
    REQUIRE(blurred != nullptr);

    save_debug(*focused, "output/debug/25d/dof/01_focus_near_sharp.png");
    save_debug(*blurred, "output/debug/25d/dof/02_focus_far_blurred.png");

    CHECK(bbox_width(*blurred) > bbox_width(*focused));
}

TEST_CASE("DOF visual: changing focus changes output hash") {
    auto out_of_focus = render_dof_scene(0.0f, 500.0f, true, 0.02f);
    auto in_focus = render_dof_scene(500.0f, 500.0f, true, 0.02f);

    REQUIRE(out_of_focus != nullptr);
    REQUIRE(in_focus != nullptr);

    save_debug(*out_of_focus, "output/debug/25d/dof/03_focus_far_out_of_focus.png");
    save_debug(*in_focus, "output/debug/25d/dof/04_focus_far_in_focus.png");

    CHECK(framebuffer_hash(*out_of_focus) != framebuffer_hash(*in_focus));
}

TEST_CASE("DOF visual: disabled matches zero aperture") {
    auto disabled = render_dof_scene(0.0f, 500.0f, false, 0.02f);
    auto zero_aperture = render_dof_scene(0.0f, 500.0f, true, 0.0f);

    REQUIRE(disabled != nullptr);
    REQUIRE(zero_aperture != nullptr);

    save_debug(*disabled, "output/debug/25d/dof/05_dof_disabled.png");
    save_debug(*zero_aperture, "output/debug/25d/dof/06_dof_aperture_zero.png");

    CHECK(framebuffer_hash(*disabled) == framebuffer_hash(*zero_aperture));
}

