#include "command_mask_propagate_detail.hpp"
#include <chronon3d/backends/image/image_loader.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <chronon3d/media/tracking/temporal_mask_propagator.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <system_error>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace chronon3d::cli::mask_propagation_detail {
using namespace chronon3d;
namespace {
std::shared_ptr<Framebuffer> load_frame_impl(const std::string& path) {
const auto extension = std::filesystem::path(path).extension().string();
std::string lower = extension;
std::transform(lower.begin(), lower.end(), lower.begin(),
               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
if (lower != ".png" && lower != ".jpg" && lower != ".jpeg")
    throw std::runtime_error("only 8-bit PNG/JPEG input is supported: " + path);
auto frame = io::load_image_as_framebuffer(path);
if (!frame) throw std::runtime_error("could not decode frame: " + path);
return frame;
}
} // namespace
bool write_gray(const std::string& path, std::span<const float> values,
                std::uint32_t width, std::uint32_t height) {
    Framebuffer output(static_cast<i32>(width), static_cast<i32>(height));
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y) * width + x;
            const float coverage = std::clamp(values[index], 0.0f, 1.0f);
            output.set_pixel(static_cast<i32>(x), static_cast<i32>(y),
                             Color{coverage, coverage, coverage, 1.0f});
        }
    }
    const std::filesystem::path destination(path);
    if (destination.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) return false;
    }
    return chronon3d::save_png(output, path);
}
std::shared_ptr<Framebuffer> load_frame(const std::string& path) {
    return load_frame_impl(path);
}
std::shared_ptr<Framebuffer> load_tracking_frame(const std::string& path,
                                              const std::string& transfer) {
if (std::filesystem::path(path).extension() == ".chrmaskframe") {
    std::ifstream input(path, std::ios::binary);
    constexpr std::array<char, 8> expected_magic{'C', 'H', 'M', 'F', '3', '2', '\0', '\0'};
    std::array<char, 8> magic{};
    std::uint32_t width = 0, height = 0, channels = 0;
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    input.read(reinterpret_cast<char*>(&width), sizeof(width));
    input.read(reinterpret_cast<char*>(&height), sizeof(height));
    input.read(reinterpret_cast<char*>(&channels), sizeof(channels));
    if (!input || magic != expected_magic || channels != 4 || width == 0 || height == 0 ||
        static_cast<std::uint64_t>(width) * height > 8'000'000)
        throw std::runtime_error("invalid linear tracking spool frame: " + path);
    const auto expected_size = 20ULL + static_cast<std::uint64_t>(width) * height *
        4ULL * sizeof(float);
    std::error_code size_error;
    if (std::filesystem::file_size(path, size_error) != expected_size || size_error)
        throw std::runtime_error("linear tracking spool frame has a truncated or invalid size: " + path);
    auto frame = std::make_shared<Framebuffer>(static_cast<i32>(width), static_cast<i32>(height));
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::array<float, 4> rgba{};
            input.read(reinterpret_cast<char*>(rgba.data()),
                       static_cast<std::streamsize>(sizeof(rgba)));
            if (!input || !std::all_of(rgba.begin(), rgba.end(),
                    [](float value) { return std::isfinite(value); }) ||
                rgba[3] < 0.0f || rgba[3] > 1.0f)
                throw std::runtime_error("linear tracking spool contains invalid pixels: " + path);
            frame->set_pixel(static_cast<i32>(x), static_cast<i32>(y),
                Color{rgba[0], rgba[1], rgba[2], rgba[3]});
        }
    }
    return frame;
}
auto frame = load_frame_impl(path);
if (transfer != "rec709") return frame;
// The image loader decodes PNG/JPEG as sRGB. Recover the encoded code
// values and decode them with the Rec.709 transfer declared by the
// extracted source video, preserving alpha and premultiplication.
for (i32 y = 0; y < frame->height(); ++y) {
    for (i32 x = 0; x < frame->width(); ++x) {
        auto pixel = frame->get_pixel(x, y);
        const auto straight = pixel.unpremultiplied();
        pixel.r = color::rec709_to_linear_float(
            color::linear_srgb_float(straight.r)) * pixel.a;
        pixel.g = color::rec709_to_linear_float(
            color::linear_srgb_float(straight.g)) * pixel.a;
        pixel.b = color::rec709_to_linear_float(
            color::linear_srgb_float(straight.b)) * pixel.a;
        frame->set_pixel(x, y, pixel);
    }
}
return frame;
}
} // namespace chronon3d::cli::mask_propagation_detail
