// Include contract: this fragment is textually included INSIDE
// `namespace chronon3d` by image_writer.cpp — all includes (atomic, cstdint,
// parallel_tracked) live in that TU, never here.
bool save_png(const Framebuffer& framebuffer, const std::string& path) {
    return save_png(framebuffer, path, ImageWriteOptions{});
}

bool save_png(const Framebuffer& framebuffer, const std::string& path,
              const ImageWriteOptions& options) {
    CHRONON_TRACE_SCOPE("chronon.io", "write_png");
    // PNG is a CPU consumer. The render pipeline performs the explicit
    // GPU→CPU synchronization at its output boundary; reject a GPU-only
    // framebuffer here rather than silently exporting stale CPU shadow data.
    if (framebuffer.residency() == SurfaceResidency::GpuOnly) {
        spdlog::error("save_png: framebuffer is GPU-authoritative; explicit readback required before PNG export");
        return false;
    }
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
    
    // Convert Framebuffer (float RGBA) to byte RGBA.
    // Uses the same canonical LUT-based linear→sRGB8 conversion as
    // Framebuffer::save_ppm(), keeping both paths bit-equivalent.
    // (The previous to_srgb() + std::round() path produced identical
    // byte values for all validated inputs but triggered a near-black
    // PNG regression in the install_consumer Phase 4 gate — root cause
    // TBD; this change mirrors the proven save_ppm pixel path.)
    std::vector<uint8_t> data(width * height * 4);

    // TICKET-RENDER-PIPELINE-INTEGRITY: per-pixel NaN/Inf detection.  We
    // log the first corrupt pixel loudly (with channel values + path) and
    // keep zero-filling per-pixel so the byte buffer never contains
    // undefined values — but then throw std::runtime_error ONCE at the
    // end of the loop.  This replaces the previous silent zero-fill guard
    // which propagated corruption as empty all-transparent PNGs.
    // Rows are independent: each iteration reads one source pixel and
    // writes one byte slot.  The conversion runs through the canonical
    // parallel wrapper (P4); the "first corrupt pixel" report stays
    // row-major-deterministic by recording the minimal corrupt index in one
    // atomic instead of logging from inside the parallel region.
    std::atomic<std::int64_t> first_bad_index{-1};
    auto convert_rows = [&](int row_begin, int row_end) {
    for (i32 y = row_begin; y < row_end; ++y) {
        const Color* row = framebuffer.pixels_row(y);
        for (i32 x = 0; x < width; ++x) {
            const Color& linear_c = row[x];

            // NaN/Inf guard: skip corrupted pixels to prevent
            // undefined behavior in linear_to_srgb8() and output
            // file corruption.
            usize index = (y * width + x) * 4;
            if (std::isnan(linear_c.r) || std::isnan(linear_c.g) ||
                std::isnan(linear_c.b) || std::isnan(linear_c.a) ||
                std::isinf(linear_c.r) || std::isinf(linear_c.g) ||
                std::isinf(linear_c.b) || std::isinf(linear_c.a)) {
                const std::int64_t candidate =
                    static_cast<std::int64_t>(y) * width + x;
                std::int64_t observed =
                    first_bad_index.load(std::memory_order_relaxed);
                while (observed < 0 || candidate < observed) {
                    if (first_bad_index.compare_exchange_weak(
                            observed, candidate, std::memory_order_relaxed)) {
                        break;
                    }
                }
                data[index + 0] = 0;
                data[index + 1] = 0;
                data[index + 2] = 0;
                data[index + 3] = 0;
                continue;
            }

            // Framebuffers store premultiplied linear RGB. PNG stores
            // straight-alpha sRGB, so remove coverage before applying the
            // transfer curve; otherwise translucent pixels acquire dark
            // fringes on export.
            const Color straight_linear = linear_c.unpremultiplied();
            const auto encode_rgb = [&](float value) {
                if (options.convert_png_to_srgb)
                    return Color::linear_to_srgb8(value);
                return static_cast<uint8_t>(std::clamp(
                    std::round(value * 255.0f), 0.0f, 255.0f));
            };
            data[index + 0] = encode_rgb(straight_linear.r);
            data[index + 1] = encode_rgb(straight_linear.g);
            data[index + 2] = encode_rgb(straight_linear.b);
            // Alpha stays in linear space (no sRGB curve).
            data[index + 3] = static_cast<uint8_t>(std::clamp(std::round(linear_c.a * 255.0f), 0.0f, 255.0f));
        }
    }
    };
    if (height >= 16) {
        parallel_for_tracked(
            tbb::blocked_range<i32>(0, height),
            [&](const tbb::blocked_range<i32>& range) {
                convert_rows(range.begin(), range.end());
            });
    } else {
        convert_rows(0, height);
    }

    // TICKET-RENDER-PIPELINE-INTEGRITY: refuse to write a PNG with any
    // corrupt channel.  Throw ONCE (not per-pixel) so we don't pay the
    // exception-unwind cost more than once.  Catches the per-pixel
    // NaN/Inf case after the silent-zero fill above; layer (1) of the
    // render-pipeline-integrity ticket stack.  The reported pixel is the
    // row-major-first corrupt one, exactly as in the serial loop.
    const std::int64_t first_bad =
        first_bad_index.load(std::memory_order_relaxed);
    if (first_bad >= 0) {
        const int first_bad_x = static_cast<int>(first_bad % width);
        const int first_bad_y = static_cast<int>(first_bad / width);
        const Color& bad = framebuffer.pixels_row(first_bad_y)[first_bad_x];
        spdlog::error(
            "save_png: corrupt pixel at ({},{}) — channels=("
            "R={:.3f} G={:.3f} B={:.3f} A={:.3f}), path='{}'",
            first_bad_x, first_bad_y, bad.r, bad.g, bad.b, bad.a, path);
        throw std::runtime_error(
            std::string("save_png: refusing to write '") + path +
            "' — first corrupt pixel @ (" +
            std::to_string(first_bad_x) + "," +
            std::to_string(first_bad_y) + ")");
    }
    return stbi_write_png(path.c_str(), width, height, 4, data.data(), width * 4) != 0;
}
