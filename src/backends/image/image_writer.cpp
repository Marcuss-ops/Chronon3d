#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#ifdef CHRONON3D_ENABLE_EXR
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfTiledOutputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <Imath/half.h>
#endif

#include <vector>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <spdlog/spdlog.h>

namespace chronon3d {

namespace {

std::string lower_extension(const std::string& path) {
    std::filesystem::path p(path);
    std::string ext = p.extension().string();

    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return ext;
}

void ensure_parent_dir(const std::string& path) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
}

} // namespace

ImageFormat image_format_from_path(const std::string& path) {
    const std::string ext = lower_extension(path);

    if (ext == ".png") {
        return ImageFormat::Png;
    }

    if (ext == ".exr") {
        return ImageFormat::Exr;
    }

    return ImageFormat::Unknown;
}

bool save_png(const Framebuffer& framebuffer, const std::string& path) {
    CHRONON_ZONE_C("write_png", trace_category::kOutput);
    // Use lower compression (2 vs default 8) for ~3x faster encoding.
    // Thread-local to prevent race condition on stbi's global variable
    // when multiple frames are exported in parallel.
    thread_local int saved_compression = -1;
    if (saved_compression != 2) {
        stbi_write_png_compression_level = 2;
        saved_compression = 2;
    }
    ensure_parent_dir(path);

    i32 width = framebuffer.width();
    i32 height = framebuffer.height();
    
    // Convert Framebuffer (float RGBA) to byte RGBA
    std::vector<uint8_t> data(width * height * 4);

    for (i32 y = 0; y < height; ++y) {
        for (i32 x = 0; x < width; ++x) {
            Color linear_c = framebuffer.get_pixel(x, y);
            // NaN/Inf guard: skip corrupted pixels to prevent output file corruption
            if (std::isnan(linear_c.r) || std::isnan(linear_c.g) || std::isnan(linear_c.b) || std::isnan(linear_c.a) ||
                std::isinf(linear_c.r) || std::isinf(linear_c.g) || std::isinf(linear_c.b) || std::isinf(linear_c.a)) {
                linear_c = Color::transparent();
            }
            Color c = linear_c.to_srgb(); // Convert from linear to sRGB for saving
            usize index = (y * width + x) * 4;

            // Quantize to 8-bit with proper rounding.
            //
            // The previous code used `static_cast<uint8_t>(c.r * 255.0f)` which
            // TRUNCATES toward zero. At the top end of the sRGB curve
            // (`to_srgb(1.0)` returns ~0.99999994 in IEEE 754 single precision
            // because `1.055f - 0.055f` does not collapse to exactly 1.0f),
            // this produced 254 instead of 255 — visible as off-by-one banding
            // in install_consumer PNG outputs (TICKET-GATE-10-PHASE-4-BLACK,
            // M1.5#9 regression isolated via chronon3d_save_png_roundtrip_test).
            //
            // `std::round` (rounds-half-away-from-zero) is the canonical
            // IEEE 754 quantizer used by `Color::linear_to_srgb8_lut()`
            // construction at color.hpp:24 — mirroring the LUT behaviour
            // here keeps the two paths bit-equivalent.
            data[index + 0] = static_cast<uint8_t>(std::clamp(std::round(c.r * 255.0f), 0.0f, 255.0f));
            data[index + 1] = static_cast<uint8_t>(std::clamp(std::round(c.g * 255.0f), 0.0f, 255.0f));
            data[index + 2] = static_cast<uint8_t>(std::clamp(std::round(c.b * 255.0f), 0.0f, 255.0f));
            // Alpha stays in linear space (no sRGB curve) — apply the same
            // rounding so values just below 1.0 do not silently drop to 254.
            data[index + 3] = static_cast<uint8_t>(std::clamp(std::round(c.a * 255.0f), 0.0f, 255.0f));
        }
    }
    
    return stbi_write_png(path.c_str(), width, height, 4, data.data(), width * 4) != 0;
}

#ifdef CHRONON3D_ENABLE_EXR
bool save_exr(const Framebuffer& framebuffer,
              const std::string& path,
              const ImageWriteOptions& options) {
    CHRONON_ZONE_C("write_exr", trace_category::kOutput);
    try {
        ensure_parent_dir(path);

        const int width = framebuffer.width();
        const int height = framebuffer.height();

        if (width <= 0 || height <= 0) {
            return false;
        }

        // NaN/Inf guard helper: returns 0.0f for corrupted channels
        const auto safe_channel = [](float v) -> float {
            if (std::isnan(v) || std::isinf(v)) return 0.0f;
            return v;
        };

        const size_t pixel_count = static_cast<size_t>(width) * height;
        const Imf::PixelType pixel_type = options.exr_half_float ? Imf::HALF : Imf::FLOAT;

        if (options.exr_half_float) {
            std::vector<half> r(pixel_count);
            std::vector<half> g(pixel_count);
            std::vector<half> b(pixel_count);
            std::vector<half> a(pixel_count);

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const Color c = framebuffer.get_pixel(x, y);
                    const size_t index = static_cast<size_t>(y) * width + x;
                    r[index] = static_cast<half>(safe_channel(c.r));
                    g[index] = static_cast<half>(safe_channel(c.g));
                    b[index] = static_cast<half>(safe_channel(c.b));
                    a[index] = static_cast<half>(safe_channel(c.a));
                }
            }

            Imf::Header header(width, height);
            header.channels().insert("R", Imf::Channel(pixel_type));
            header.channels().insert("G", Imf::Channel(pixel_type));
            header.channels().insert("B", Imf::Channel(pixel_type));
            header.channels().insert("A", Imf::Channel(pixel_type));

            if (options.exr_dwaa) {
                header.compression() = Imf::DWAA_COMPRESSION;
            }

            Imf::FrameBuffer frame_buffer;
            const size_t x_stride = sizeof(half);
            const size_t y_stride = static_cast<size_t>(width) * sizeof(half);

            frame_buffer.insert("R", Imf::Slice(pixel_type, reinterpret_cast<char*>(r.data()), x_stride, y_stride));
            frame_buffer.insert("G", Imf::Slice(pixel_type, reinterpret_cast<char*>(g.data()), x_stride, y_stride));
            frame_buffer.insert("B", Imf::Slice(pixel_type, reinterpret_cast<char*>(b.data()), x_stride, y_stride));
            frame_buffer.insert("A", Imf::Slice(pixel_type, reinterpret_cast<char*>(a.data()), x_stride, y_stride));

            if (options.exr_tiled) {
                header.setTileDescription(Imf::TileDescription(256, 256, Imf::ONE_LEVEL));
                Imf::TiledOutputFile file(path.c_str(), header);
                file.setFrameBuffer(frame_buffer);
                file.writeTiles(0, file.numXTiles() - 1, 0, file.numYTiles() - 1);
            } else {
                Imf::OutputFile file(path.c_str(), header);
                file.setFrameBuffer(frame_buffer);
                file.writePixels(height);
            }
        } else {
            std::vector<float> r(pixel_count);
            std::vector<float> g(pixel_count);
            std::vector<float> b(pixel_count);
            std::vector<float> a(pixel_count);

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const Color c = framebuffer.get_pixel(x, y);
                    const size_t index = static_cast<size_t>(y) * width + x;
                    r[index] = safe_channel(c.r);
                    g[index] = safe_channel(c.g);
                    b[index] = safe_channel(c.b);
                    a[index] = safe_channel(c.a);
                }
            }

            Imf::Header header(width, height);
            header.channels().insert("R", Imf::Channel(pixel_type));
            header.channels().insert("G", Imf::Channel(pixel_type));
            header.channels().insert("B", Imf::Channel(pixel_type));
            header.channels().insert("A", Imf::Channel(pixel_type));

            if (options.exr_dwaa) {
                header.compression() = Imf::DWAA_COMPRESSION;
            }

            Imf::FrameBuffer frame_buffer;
            const size_t x_stride = sizeof(float);
            const size_t y_stride = static_cast<size_t>(width) * sizeof(float);

            frame_buffer.insert("R", Imf::Slice(pixel_type, reinterpret_cast<char*>(r.data()), x_stride, y_stride));
            frame_buffer.insert("G", Imf::Slice(pixel_type, reinterpret_cast<char*>(g.data()), x_stride, y_stride));
            frame_buffer.insert("B", Imf::Slice(pixel_type, reinterpret_cast<char*>(b.data()), x_stride, y_stride));
            frame_buffer.insert("A", Imf::Slice(pixel_type, reinterpret_cast<char*>(a.data()), x_stride, y_stride));

            if (options.exr_tiled) {
                header.setTileDescription(Imf::TileDescription(256, 256, Imf::ONE_LEVEL));
                Imf::TiledOutputFile file(path.c_str(), header);
                file.setFrameBuffer(frame_buffer);
                file.writeTiles(0, file.numXTiles() - 1, 0, file.numYTiles() - 1);
            } else {
                Imf::OutputFile file(path.c_str(), header);
                file.setFrameBuffer(frame_buffer);
                file.writePixels(height);
            }
        }

        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to write EXR image '{}': {}", path, e.what());
        return false;
    } catch (...) {
        spdlog::error("Failed to write EXR image '{}': unknown exception", path);
        return false;
    }
}
#else
// save_exr not compiled — inline stub in header returns false
#endif

bool save_image(const Framebuffer& framebuffer,
                const std::string& path,
                const ImageWriteOptions& options) {
    CHRONON_ZONE_C("save_image", trace_category::kOutput);
    ImageFormat format = options.format;

    if (format == ImageFormat::Unknown) {
        format = image_format_from_path(path);
    }

    switch (format) {
        case ImageFormat::Png:
            return save_png(framebuffer, path);

        case ImageFormat::Exr:
            return save_exr(framebuffer, path, options);

        default:
            return false;
    }
}

} // namespace chronon3d
