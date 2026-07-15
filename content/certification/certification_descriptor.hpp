#pragma once

#include <chronon3d/timeline/composition_descriptor.hpp>

#include <string>
#include <utility>

namespace chronon3d::content::certification {

/// Canonical descriptor constructor for certification compositions.
/// Keeps category, frame-rate and metadata policy in one place while every
/// registration still enters CompositionRegistry through CompositionDescriptor.
template <typename Factory>
CompositionDescriptor certification_descriptor(
    std::string id,
    i32 width,
    i32 height,
    Frame duration,
    Factory&& factory,
    FrameRate fps = FrameRate{30, 1}) {
    return CompositionDescriptor{
        .id = std::move(id),
        .category = "Certification",
        .width = width,
        .height = height,
        .fps = fps,
        .duration = duration,
        .factory = std::forward<Factory>(factory)
    };
}

} // namespace chronon3d::content::certification
