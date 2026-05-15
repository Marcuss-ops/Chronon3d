#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <stb_image.h>
#include <string>
#include <vector>
#include <filesystem>
#include <doctest/doctest.h>

namespace chronon3d {
namespace test {

/**
 * Compares a framebuffer with a "golden" image on disk.
 * If the image doesn't exist, it can optionally be created (for first-time setup).
 */
inline void check_golden(const Framebuffer& fb, const std::string& name) {
    namespace fs = std::filesystem;
    // Base path for tests is the project root (assuming running from build dir or similar)
    // Actually, it's better to use a relative path from the binary or a define.
    // For now, let's assume a "data/golden" directory in the test source folder.
    fs::path golden_dir = fs::path(__FILE__).parent_path() / "data" / "golden";
    fs::path golden_path = golden_dir / (name + ".png");
    fs::path actual_path = fs::path(__FILE__).parent_path() / "data" / "actual" / (name + ".png");

    if (!fs::exists(golden_dir)) {
        fs::create_directories(golden_dir);
    }
    
    fs::create_directories(actual_path.parent_path());

    // Save actual for debugging
    save_png(fb, actual_path.string());

    if (!fs::exists(golden_path)) {
        // Auto-generate golden if it doesn't exist (only for manual runs)
        save_png(fb, golden_path.string());
        MESSAGE("Generated golden image: " << golden_path);
        return;
    }

    // Load golden image
    int width, height, channels;
    unsigned char* data = stbi_load(golden_path.string().c_str(), &width, &height, &channels, 4);
    REQUIRE(data != nullptr);
    REQUIRE(width == fb.width());
    REQUIRE(height == fb.height());

    // Compare
    bool match = true;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Color actual_linear = fb.get_pixel(x, y);
            Color actual_srgb = actual_linear.to_srgb();
            
            size_t idx = (y * width + x) * 4;
            uint8_t gr = data[idx + 0];
            uint8_t gg = data[idx + 1];
            uint8_t gb = data[idx + 2];
            uint8_t ga = data[idx + 3];

            uint8_t ar = static_cast<uint8_t>(std::clamp(actual_srgb.r * 255.0f, 0.0f, 255.0f));
            uint8_t ag = static_cast<uint8_t>(std::clamp(actual_srgb.g * 255.0f, 0.0f, 255.0f));
            uint8_t ab = static_cast<uint8_t>(std::clamp(actual_srgb.b * 255.0f, 0.0f, 255.0f));
            uint8_t aa = static_cast<uint8_t>(std::clamp(actual_srgb.a * 255.0f, 0.0f, 255.0f));

            if (std::abs(ar - gr) > 1 || std::abs(ag - gg) > 1 || std::abs(ab - gb) > 1) {
                match = false;
                break;
            }
        }
        if (!match) break;
    }

    stbi_image_free(data);
    CHECK_MESSAGE(match, "Framebuffer does not match golden image: " << name);
}

} // namespace test
} // namespace chronon3d
