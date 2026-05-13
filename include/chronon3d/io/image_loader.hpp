#pragma once

#include <chronon3d/renderer/software/framebuffer.hpp>
#include <memory>
#include <string>

namespace chronon3d::io {

/**
 * Loads an image from disk and returns it as a Framebuffer.
 * The image is resized/padded to fit the target width and height if they are specified (> 0).
 */
std::shared_ptr<Framebuffer> load_image_as_framebuffer(
    const std::string& path,
    i32 target_width = -1,
    i32 target_height = -1
);

} // namespace chronon3d::io
