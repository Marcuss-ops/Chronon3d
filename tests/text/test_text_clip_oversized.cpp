// ============================================================================
// test_text_clip_oversized.cpp
//
// TICKET-FALSE-GREEN-TEST-AUDIT — anti-false-green clip regression lock.
//
// Renders deliberately oversized text inside a small clip box and verifies
// that the rendered ink is contained by the box. This ensures the clip policy
// is not a no-op and protects against future regressions that silently allow
// text to spill outside its declared box.
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

using namespace chronon3d;

TEST_CASE("TICKET-FALSE-GREEN-TEST-AUDIT: clip policy contains oversized text") {
    auto renderer = test::make_renderer();
    const float box_w = 200.0f;
    const float box_h = 100.0f;

    auto comp = composition(
        {.name = "ClipOversized",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [&renderer, box_w, box_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("clip_oversized_layer", [&renderer, box_w, box_h](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text("clip_oversized_text", TextSpec{
                    .content = {.value = "OVERSIZED"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, Vec2{960.0f, 540.0f}},
                    .font = {.font_path = chronon3d::test::bundled_font_path("assets/fonts/Inter-Bold.ttf"),
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 200.0f},
                    .layout = {.box = {box_w, box_h},
                               .anchor = TextAnchor::Center,
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle,
                               .overflow = TextOverflow::Clip},
                    .appearance = {.color = Color::white()}
                });
            });
            return s.build();
        });

    auto fb = renderer.render(comp, Frame{0});
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
    CHECK(bbox.x0 >= static_cast<int>(left) - 1);
    CHECK(bbox.y0 >= static_cast<int>(top) - 1);
    CHECK(bbox.x1 <= static_cast<int>(right) + 1);
    CHECK(bbox.y1 <= static_cast<int>(bottom) + 1);
}
