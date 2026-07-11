// content/experimental/ref-2d5/glow_test_common.hpp
#pragma once
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <string>

namespace chronon3d::content::effects {

static constexpr i32 kW = 1536;
static constexpr i32 kH = 1024;
static constexpr f32 kHW = kW * 0.5f;  // 768
static constexpr f32 kHH = kH * 0.5f;  // 512

inline void deep_bg(SceneBuilder& s, Color top, Color bot) {
    s.layer("_bg", [=](LayerBuilder& l) {
        l.cache_static().rect("bg", {
            .size  = {(f32)kW, (f32)kH},
            .color = top,
            .pos   = {0.f, 0.f, 0.f},
            .fill  = FillStyle::linear({0,0},{0,1},{{0.f,top},{1.f,bot}})
        });
    });
}

inline void bottom_label(SceneBuilder& s, const std::string& text, Color col = Color{0.55f,0.72f,0.90f,1.f}) {
    s.layer("_label", [=](LayerBuilder& l) {
        l.position({0, kHH - 60.f, 0});
        l.text("t", TextSpec{.content = {.value = text},.position = {0, 0.0f, 0.0f},.font = {.font_size = 18.f},.layout = {.box = {1200, 44}, .anchor = TextAnchor::Center, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle},.appearance = {.color = col},});
    });
}

} // namespace chronon3d::content::effects
