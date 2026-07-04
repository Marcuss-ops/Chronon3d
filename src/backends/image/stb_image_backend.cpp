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
#ifdef CHRONON3D_ENABLE_EXR
        return load_exr_mmap(resolved_path);
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

} // namespace chronon3d::image
