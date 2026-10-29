// ============================================================================
// test_text_clip_oversized.cpp
//
// TICKET-TEXT-CLIP-BOX — renderer clipping regression lock.
//
// Renders deliberately oversized text inside a small Clip box. The renderer
// enforces pixel clipping without changing the authored font-size policy.
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/support/pixel_scan_helpers.hpp>
#include <chronon3d/text/text_definition.hpp>

using namespace chronon3d;

TEST_CASE("TICKET-TEXT-CLIP-BOX: Clip does not implicitly auto-fit oversized text") {
    auto renderer = test::make_renderer();
    auto unclipped_renderer = test::make_renderer();
    const float box_w = 100.0f;
    const float box_h = 50.0f;

    auto make_comp = [box_w, box_h](test::TestRenderer& target, TextOverflow overflow) {
      return composition(
        {.name = overflow == TextOverflow::Clip ? "ClipOversized" : "VisibleOversized",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [&target, box_w, box_h, overflow](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&target.font_engine());
            s.layer("clip_oversized_layer", [&target, box_w, box_h, overflow](LayerBuilder& l) {
                l.font_engine(&target.font_engine());
                l.text("clip_oversized_text", TextDefinition{
    .content = {.value = "OVERSIZED"},
    .style = {
        .font = {.font_path = chronon3d::test::bundled_font_path("assets/fonts/Inter-Bold.ttf"),
                             .font_family = "Inter",
                             .font_weight = 700,
                    .font_size = 200.0f},
        .color = Color::white()
    },
    .frame = {
        .size = {box_w, box_h},
        .placement = TextPlacement{TextPlacementKind::Absolute, Vec2{960.0f, 540.0f}},
        .anchor = TextAnchor::Center,
        .align = TextAlign::Center,
        .vertical_align = VerticalAlign::Middle,
        .overflow = overflow
    }
});
            });
            return s.build();
        });
    };

    auto unclipped_fb = unclipped_renderer.render(
        make_comp(unclipped_renderer, TextOverflow::Visible), Frame{0});
    REQUIRE(unclipped_fb != nullptr);
    const auto unclipped_bbox = chronon3d::test::completeness::alpha_bbox(*unclipped_fb);
    REQUIRE_FALSE(unclipped_bbox.empty());

    auto fb = renderer.render(make_comp(renderer, TextOverflow::Clip), Frame{0});
    REQUIRE(fb != nullptr);

    const auto bbox = chronon3d::test::completeness::alpha_bbox(*fb);
    INFO("clip oversized ink bbox: (", bbox.x0, ",", bbox.y0, ")-(",
         bbox.x1, ",", bbox.y1, ")");
    CHECK_FALSE(bbox.empty());
    CHECK(chronon3d::test::completeness::count_visible_pixels(*fb) > 50);

    // The text is centered at (960, 540); the clip box is 200x100.
    const float left   = 960.0f - box_w * 0.5f;
    const float top    = 540.0f - box_h * 0.5f;
    const float right  = 960.0f + box_w * 0.5f;
    const float bottom = 540.0f + box_h * 0.5f;
    CHECK((unclipped_bbox.x0 < static_cast<int>(left) - 1 ||
          unclipped_bbox.y0 < static_cast<int>(top) - 1 ||
          unclipped_bbox.x1 > static_cast<int>(right) + 1 ||
          unclipped_bbox.y1 > static_cast<int>(bottom) + 1));
    // Clip does not opt into auto-fit; the rendered ink stays inside the
    // authored box while remaining visible.
    CHECK(bbox.x0 >= static_cast<int>(left) - 1);
    CHECK(bbox.y0 >= static_cast<int>(top) - 1);
    CHECK(bbox.x1 <= static_cast<int>(right) + 1);
    CHECK(bbox.y1 <= static_cast<int>(bottom) + 1);
}
