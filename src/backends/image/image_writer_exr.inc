#ifdef CHRONON3D_ENABLE_EXR
bool save_exr(const Framebuffer& framebuffer,
              const std::string& path,
              const ImageWriteOptions& options) {
    CHRONON_TRACE_SCOPE("chronon.io", "write_exr");
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
