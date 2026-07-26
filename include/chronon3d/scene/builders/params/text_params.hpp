#pragma once

// Text parameter umbrella for builder-level common style types.
// The canonical definitions live under <chronon3d/text/>; this header exposes
// no legacy aliases.

#include <chronon3d/text/text_appearance_spec.hpp>
#include <chronon3d/text/text_content.hpp>
#include <chronon3d/text/text_layout_spec.hpp>
#include <chronon3d/text/text_defaults.hpp>
#include <chronon3d/text/text_run_definition.hpp>

#include <chronon3d/scene/model/shape/shape.hpp>  // TextShadow

#include <cstdint>
#include <memory>
#include <string>

namespace chronon3d {

struct ShadowStyle {
    TextShadow contact{
        .enabled = true,
        .offset = {0.0f, 6.0f},
        .blur = 14.0f,
        .opacity = 0.28f,
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    };
    TextShadow ambient{
        .enabled = true,
        .offset = {0.0f, 40.0f},
        .blur = 120.0f,
        .opacity = 0.08f,
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    };
};

} // namespace chronon3d
