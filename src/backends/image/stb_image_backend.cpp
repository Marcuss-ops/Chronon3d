#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/image/exr_mmap.hpp>

#include <filesystem>
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

    fs::path current = fs::current_path();
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
        return load_exr_mmap(resolved_path);
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

} // namespace chronon3d::image
