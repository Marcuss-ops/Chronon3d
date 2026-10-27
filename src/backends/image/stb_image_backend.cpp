// M1.5#7 — shrink stb_image.h AST by removing unused decoders.
// Test suite & CLI only requires PNG (and JPEG for general web fixtures).
// BMP/PSD/TGA/GIF/PIC/PNM/HDR modules are removed. ~70% AST reduction.
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#ifdef CHRONON3D_ENABLE_EXR
#include <chronon3d/backends/image/exr_mmap.hpp>
#endif

#include <filesystem>
#include <limits>
#include <spdlog/spdlog.h>

namespace chronon3d::image {

namespace {

std::string resolve_existing_path(const std::string& path) {
    namespace fs = std::filesystem;

    const fs::path input(path);
    if (input.is_absolute() && fs::exists(input)) {
        return input.lexically_normal().string();
    }

    if (fs::exists(input)) {
        return input.lexically_normal().string();
    }

    fs::path current = fs::absolute(".");
    for (int depth = 0; depth < 8; ++depth) {
        const fs::path candidate = current / input;
        if (fs::exists(candidate)) {
            return candidate.lexically_normal().string();
        }
        if (!current.has_parent_path()) {
            break;
        }
        current = current.parent_path();
    }

    return path;
}

} // namespace

std::unique_ptr<ImageBuffer> StbImageBackend::load_image(const std::string& path) {
    const std::string resolved_path = resolve_existing_path(path);

    if (resolved_path.length() >= 4 && resolved_path.substr(resolved_path.length() - 4) == ".exr") {
#ifdef CHRONON3D_ENABLE_EXR
        const auto framebuffer = load_exr_as_framebuffer_float(resolved_path);
        if (!framebuffer) return nullptr;
        auto buffer = std::make_unique<ImageBuffer>();
        buffer->width = framebuffer->width();
        buffer->height = framebuffer->height();
        buffer->channels = 4;
        const auto pixel_count = static_cast<std::size_t>(buffer->width) * buffer->height;
        buffer->linear_pixels = std::make_unique<float[]>(pixel_count * 4);
        for (int y = 0; y < buffer->height; ++y) {
            const Color* row = framebuffer->pixels_row(y);
            for (int x = 0; x < buffer->width; ++x) {
                const auto index = (static_cast<std::size_t>(y) * buffer->width + x) * 4;
                buffer->linear_pixels[index + 0] = row[x].r;
                buffer->linear_pixels[index + 1] = row[x].g;
                buffer->linear_pixels[index + 2] = row[x].b;
                buffer->linear_pixels[index + 3] = row[x].a;
            }
        }
        return buffer;
#else
        spdlog::warn("[image] EXR support not enabled (recompile with CHRONON3D_ENABLE_EXR=ON)");
        return nullptr;
#endif
    }

    int w, h, ch;
    unsigned char* pixels = stbi_load(resolved_path.c_str(), &w, &h, &ch, 4);
    if (!pixels) {
        spdlog::warn(
            "[image] stbi_load failed for '{}' (resolved='{}'): {}",
            path,
            resolved_path,
            stbi_failure_reason() ? stbi_failure_reason() : "unknown"
        );
        return nullptr;
    }

    auto buffer = std::make_unique<ImageBuffer>();
    buffer->width = w;
    buffer->height = h;
    buffer->channels = 4;
    buffer->pixels = std::make_unique<u8[]>(static_cast<size_t>(w) * h * 4);
    std::memcpy(buffer->pixels.get(), pixels, static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);

    return buffer;
}

std::unique_ptr<ImageBuffer> StbImageBackend::load_image_from_memory(
    std::span<const std::byte> encoded) {
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max()))
        return nullptr;
    const auto* input = reinterpret_cast<const stbi_uc*>(encoded.data());
    int width = 0, height = 0, channels = 0;
    constexpr std::size_t kMaxPixels = 16U * 1024U * 1024U;
    if (!stbi_info_from_memory(input, static_cast<int>(encoded.size()),
                               &width, &height, &channels) ||
        width <= 0 || height <= 0 ||
        static_cast<std::size_t>(width) > kMaxPixels /
            static_cast<std::size_t>(height))
        return nullptr;
    auto* pixels = stbi_load_from_memory(
        input, static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    if (!pixels) return nullptr;
    const auto pixel_count = static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);
    if (width <= 0 || height <= 0 || pixel_count >
            std::numeric_limits<std::size_t>::max() / 4U) {
        stbi_image_free(pixels);
        return nullptr;
    }
    auto buffer = std::make_unique<ImageBuffer>();
    buffer->width = width;
    buffer->height = height;
    buffer->channels = 4;
    buffer->pixels = std::make_unique<u8[]>(pixel_count * 4U);
    std::memcpy(buffer->pixels.get(), pixels, pixel_count * 4U);
    stbi_image_free(pixels);
    return buffer;
}

} // namespace chronon3d::image
