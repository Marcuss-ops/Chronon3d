#pragma once

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/frame.hpp>
#include <memory>

namespace chronon3d::software_internal {

std::unique_ptr<Framebuffer> render_frame(
    SoftwareRenderer& renderer,
    const Composition& comp,
    Frame frame
);

} // namespace chronon3d::software_internal
