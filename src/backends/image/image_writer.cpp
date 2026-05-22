#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/profiling.hpp>

#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <Imath/half.h>

#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>

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

const char* image_format_name(ImageFormat format) {
    switch (format) {
        case ImageFormat::Png: return "png";
        case ImageFormat::Exr: return "exr";
        default: return "unknown";
    }
}

bool save_png(const Framebuffer& framebuffer, const std::string& path) {
    CHRONON_ZONE_C("write_png", trace_category::kOutput);
    // Use lower compression (2 vs default 8) for ~3x faster encoding
    stbi_write_png_compression_level = 2;
    ensure_parent_dir(path);

    i32 width = framebuffer.width();
    i32 height = framebuffer.height();
    
    // Convert Framebuffer (float RGBA) to byte RGBA
    std::vector<uint8_t> data(width * height * 4);
    
    for (i32 y = 0; y < height; ++y) {
        for (i32 x = 0; x < width; ++x) {
            Color linear_c = framebuffer.get_pixel(x, y);
            Color c = linear_c.to_srgb(); // Convert from linear to sRGB for saving
            usize index = (y * width + x) * 4;
            
            data[index + 0] = static_cast<uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f));
            data[index + 1] = static_cast<uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f));
            data[index + 2] = static_cast<uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f));
            data[index + 3] = static_cast<uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f));
        }
    }
    
    return stbi_write_png(path.c_str(), width, height, 4, data.data(), width * 4) != 0;
}

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
                    // EXR stores scene-linear values. Do NOT convert to sRGB here.
                    r[index] = static_cast<half>(c.r);
                    g[index] = static_cast<half>(c.g);
                    b[index] = static_cast<half>(c.b);
                    a[index] = static_cast<half>(c.a);
                }
            }

            Imf::Header header(width, height);
            header.channels().insert("R", Imf::Channel(pixel_type));
            header.channels().insert("G", Imf::Channel(pixel_type));
            header.channels().insert("B", Imf::Channel(pixel_type));
            header.channels().insert("A", Imf::Channel(pixel_type));

            Imf::FrameBuffer frame_buffer;
            const size_t x_stride = sizeof(half);
            const size_t y_stride = static_cast<size_t>(width) * sizeof(half);

            frame_buffer.insert("R", Imf::Slice(pixel_type, reinterpret_cast<char*>(r.data()), x_stride, y_stride));
            frame_buffer.insert("G", Imf::Slice(pixel_type, reinterpret_cast<char*>(g.data()), x_stride, y_stride));
            frame_buffer.insert("B", Imf::Slice(pixel_type, reinterpret_cast<char*>(b.data()), x_stride, y_stride));
            frame_buffer.insert("A", Imf::Slice(pixel_type, reinterpret_cast<char*>(a.data()), x_stride, y_stride));

            Imf::OutputFile file(path.c_str(), header);
            file.setFrameBuffer(frame_buffer);
            file.writePixels(height);
        } else {
            std::vector<float> r(pixel_count);
            std::vector<float> g(pixel_count);
            std::vector<float> b(pixel_count);
            std::vector<float> a(pixel_count);

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const Color c = framebuffer.get_pixel(x, y);
                    const size_t index = static_cast<size_t>(y) * width + x;
                    // EXR stores scene-linear values. Do NOT convert to sRGB here.
                    r[index] = c.r;
                    g[index] = c.g;
                    b[index] = c.b;
                    a[index] = c.a;
                }
            }

            Imf::Header header(width, height);
            header.channels().insert("R", Imf::Channel(pixel_type));
            header.channels().insert("G", Imf::Channel(pixel_type));
            header.channels().insert("B", Imf::Channel(pixel_type));
            header.channels().insert("A", Imf::Channel(pixel_type));

            Imf::FrameBuffer frame_buffer;
            const size_t x_stride = sizeof(float);
            const size_t y_stride = static_cast<size_t>(width) * sizeof(float);

            frame_buffer.insert("R", Imf::Slice(pixel_type, reinterpret_cast<char*>(r.data()), x_stride, y_stride));
            frame_buffer.insert("G", Imf::Slice(pixel_type, reinterpret_cast<char*>(g.data()), x_stride, y_stride));
            frame_buffer.insert("B", Imf::Slice(pixel_type, reinterpret_cast<char*>(b.data()), x_stride, y_stride));
            frame_buffer.insert("A", Imf::Slice(pixel_type, reinterpret_cast<char*>(a.data()), x_stride, y_stride));

            Imf::OutputFile file(path.c_str(), header);
            file.setFrameBuffer(frame_buffer);
            file.writePixels(height);
        }

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

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
