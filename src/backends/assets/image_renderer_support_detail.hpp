namespace chronon3d {

namespace {

float rounded_rect_coverage(float x, float y, float w, float h, float radius) {
    if (radius <= 0.0f) {
        return 1.0f;
    }

    const float r = std::min({radius, w * 0.5f, h * 0.5f});
    const float inner_x0 = r;
    const float inner_y0 = r;
    const float inner_x1 = w - r;
    const float inner_y1 = h - r;
    const float cx = std::clamp(x, inner_x0, inner_x1);
    const float cy = std::clamp(y, inner_y0, inner_y1);
    const float dx = x - cx;
    const float dy = y - cy;
    const float dist = std::sqrt(dx * dx + dy * dy);

    return std::clamp(r + 0.5f - dist, 0.0f, 1.0f);
}

std::string rounded_cache_key(const std::string& path, int width, int height,
                              float radius, ImageDecodeOptions options) {
    const int quantized_radius = static_cast<int>(std::round(radius * 64.0f));
    std::ostringstream key;
    key << path << '#' << width << 'x' << height << "#r" << quantized_radius
        << "#cs" << static_cast<int>(options.color_space)
        << "#pm" << static_cast<int>(options.premultiply)
        << "#ori" << static_cast<int>(options.orientation);
    return key.str();
}

void record_image_telemetry(
    const RenderState& state,
    const std::string& image_path,
    int image_width,
    int image_height,
    const char* cache_status,
    double decode_ms,
    double sample_ms,
    uint64_t sampled_pixels
) {
    // The event stores exist ONLY for the SQLite telemetry consumer
    // (TICKET-TELEMETRY-STORE-CONSUMER-AUDIT); in default builds the records
    // would be drained and discarded, so the per-draw mutex + string cost is
    // gated on the real consumer.
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    telemetry::ImageTelemetryRecord rec;
    rec.frame_number = state.frame_number;
    rec.layer_id = state.layer_id;
    rec.image_path = image_path;
    rec.image_width = image_width;
    rec.image_height = image_height;
    rec.cache_status = cache_status ? cache_status : "";
    rec.decode_ms = decode_ms;
    rec.sample_ms = sample_ms;
    rec.sampled_pixels = sampled_pixels;
    telemetry::record_image_telemetry(std::move(rec));
#else
    (void)state;
    (void)image_path;
    (void)image_width;
    (void)image_height;
    (void)cache_status;
    (void)decode_ms;
    (void)sample_ms;
    (void)sampled_pixels;
#endif
}

void apply_rounded_coverage(Framebuffer& fb, float radius) {
    const int w = fb.width();
    const int h = fb.height();
    if (w <= 0 || h <= 0 || radius <= 0.0f) return;

    const float r = std::min({radius, static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f});
    const int ix0 = static_cast<int>(r);
    const int iy0 = static_cast<int>(r);
    const int ix1 = w - ix0;
    const int iy1 = h - iy0;
    const float r_sq = (r + 0.5f) * (r + 0.5f);

    tbb::parallel_for(tbb::blocked_range<int>(0, h, 16), [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            Color* row = fb.pixels_row(y);
            const bool edge_row = (y < iy0 || y >= iy1);

            if (edge_row) {
                // Top or bottom edge: all pixels potentially need coverage
                for (int x = 0; x < w; ++x) {
                    const float fx = static_cast<float>(x) + 0.5f;
                    const float fy = static_cast<float>(y) + 0.5f;
                    const float dx = (fx < r) ? fx - r : ((fx > w - r) ? fx - (w - r) : 0.0f);
                    const float dy = (fy < r) ? fy - r : ((fy > h - r) ? fy - (h - r) : 0.0f);
                    if (dx == 0.0f && dy == 0.0f) continue; // interior, coverage = 1
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    const float coverage = std::clamp(r + 0.5f - dist, 0.0f, 1.0f);
                    if (coverage <= 0.0f) {
                        row[x] = Color::transparent();
                    } else if (coverage < 1.0f) {
                        row[x].r *= coverage;
                        row[x].g *= coverage;
                        row[x].b *= coverage;
                        row[x].a *= coverage;
                    }
                }
            } else {
                // Middle rows: only left and right edge pixels need coverage
                // Left edge
                for (int x = 0; x < ix0 && x < w; ++x) {
                    const float fx = static_cast<float>(x) + 0.5f;
                    const float dx = fx - r;
                    const float dy = 0.0f; // interior y
                    const float dist_sq = dx * dx;
                    if (dist_sq >= r_sq) {
                        row[x] = Color::transparent();
                    } else {
                        const float coverage = 1.0f - std::sqrt(dist_sq) / (r + 0.5f);
                        row[x].r *= coverage;
                        row[x].g *= coverage;
                        row[x].b *= coverage;
                        row[x].a *= coverage;
                    }
                }
                // Right edge
                for (int x = ix1; x < w; ++x) {
                    const float fx = static_cast<float>(x) + 0.5f;
                    const float dx = fx - (w - r);
                    const float dist_sq = dx * dx;
                    if (dist_sq >= r_sq) {
                        row[x] = Color::transparent();
                    } else {
                        const float coverage = 1.0f - std::sqrt(dist_sq) / (r + 0.5f);
                        row[x].r *= coverage;
                        row[x].g *= coverage;
                        row[x].b *= coverage;
                        row[x].a *= coverage;
                    }
                }
            }
        }
    });
}

} // namespace

std::shared_ptr<const Framebuffer> ImageRenderer::rounded_framebuffer(
    const std::string& path,
    const CachedImage& cached,
    float radius,
    ImageDecodeOptions options
) {
    if (!cached.fb_img || radius <= 0.0f) {
        return nullptr;
    }

    const auto canonical = m_cache.get().canonical_key_for(path, options);
    if (!canonical) return nullptr;
    const std::string key = rounded_cache_key(
        canonical->canonical_path.string(), cached.width, cached.height,
        radius, canonical->options);
    {
        std::lock_guard<std::mutex> lock(*m_rounded_mutex);
        auto it = m_rounded_framebuffers.find(key);
        if (it != m_rounded_framebuffers.end()) {
            return it->second;
        }
    }

    auto rounded = std::make_shared<Framebuffer>(cached.width, cached.height);
    rounded->set_opaque(false);

    const float w = static_cast<float>(cached.width);
    const float h = static_cast<float>(cached.height);
    tbb::parallel_for(tbb::blocked_range<int>(0, cached.height, 16), [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            const Color* src_row = cached.fb_img->pixels_row(y);
            Color* dst_row = rounded->pixels_row(y);
            for (int x = 0; x < cached.width; ++x) {
                const float coverage = rounded_rect_coverage(
                    static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    w,
                    h,
                    radius
                );
                Color c = src_row[x];
                c.r *= coverage;
                c.g *= coverage;
                c.b *= coverage;
                c.a *= coverage;
                dst_row[x] = c;
            }
        }
    });

    {
        std::lock_guard<std::mutex> lock(*m_rounded_mutex);
        m_rounded_framebuffers[key] = rounded;
    }
    return rounded;
}
