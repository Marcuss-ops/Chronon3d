#pragma once

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <functional>

namespace chronon3d::content::minimalist {

using MinimalistTextSetup = std::function<void(LayerBuilder&)>;

struct MinimalistTextOptions {
    f32 font_size{100.0f};
    f32 tracking{6.0f};
    bool glow{true};
};

Composition make_minimalist_text(
    const char* name,
    const char* text,
    MinimalistTextSetup setup,
    MinimalistTextOptions options = {});

} // namespace chronon3d::content::minimalist
