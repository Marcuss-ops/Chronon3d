#pragma once

// Backward-compatible umbrella for legacy builder parameter DTOs.
//
// The canonical definitions now live under <chronon3d/text/>.  This header
// is preserved only as a compatibility shim; new code should include the
// canonical text/ headers directly.

#include <chronon3d/text/text_appearance_spec.hpp>
#include <chronon3d/text/text_content.hpp>
#include <chronon3d/text/text_layout_spec.hpp>
#include <chronon3d/text/text_spec.hpp>
#include <chronon3d/text/text_run_spec.hpp>

#include <chronon3d/scene/model/shape/shape.hpp>  // TextShadow

#include <cstdint>
#include <memory>
#include <string>

namespace chronon3d {

// Source-compatible aliases retained until the external migration closes.
using TextParams [[deprecated("Use TextSpec directly from <chronon3d/text/text_spec.hpp>")]] = TextSpec;

using TextRunParams [[deprecated("Use TextRunSpec directly from <chronon3d/text/text_run_spec.hpp>")]] = TextRunSpec;

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
