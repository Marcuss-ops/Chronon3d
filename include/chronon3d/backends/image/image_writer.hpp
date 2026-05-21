#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <string>

namespace chronon3d {

enum class ImageFormat {
    Png,
    Exr,
    Unknown
};

struct ImageWriteOptions {
    ImageFormat format{ImageFormat::Unknown};

    // PNG should convert linear framebuffer to sRGB 8-bit.
    // EXR should usually preserve linear float data.
    bool convert_png_to_srgb{true};

    // For V1 keep EXR as 32-bit float.
    bool exr_half_float{false};
};

ImageFormat image_format_from_path(const std::string& path);

const char* image_format_name(ImageFormat format);

/**
 * Saves a Framebuffer to a PNG file.
 * Returns true on success, false otherwise.
 */
bool save_png(const Framebuffer& framebuffer, const std::string& path);

/**
 * Saves a Framebuffer to an OpenEXR file (linear float RGBA).
 * Returns true on success, false otherwise.
 */
bool save_exr(const Framebuffer& framebuffer,
              const std::string& path,
              const ImageWriteOptions& options = {});

/**
 * Saves a Framebuffer to an image file, detecting format from extension.
 * Returns true on success, false otherwise.
 */
bool save_image(const Framebuffer& framebuffer,
                const std::string& path,
                const ImageWriteOptions& options = {});

} // namespace chronon3d
