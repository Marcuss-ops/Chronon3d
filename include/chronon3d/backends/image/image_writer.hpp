#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <string>

namespace chronon3d {

/**
 * Saves a Framebuffer to a PNG file.
 * Returns true on success, false otherwise.
 */
bool save_png(const Framebuffer& framebuffer, const std::string& path);

} // namespace chronon3d
