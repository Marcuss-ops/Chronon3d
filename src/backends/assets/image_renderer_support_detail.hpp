#include <chronon3d/backends/assets/rounded_rect_coverage.hpp>

namespace chronon3d {

namespace {

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

void apply_rounded_coverage(Framebuffer& fb, float radius) {
    const int w = fb.width();
    const int h = fb.height();
    if (w <= 0 || h <= 0 || radius <= 0.0f) return;

    // Evaluate the same rounded-rectangle distance field used by
    // rounded_framebuffer(). The old implementation faded every pixel in the
    // left/right radius band, including middle rows that are part of the
    // straight edge; that produced a wide horizontal alpha gradient along the
    // full image height instead of clipping only the four corners.
    tbb::parallel_for(tbb::blocked_range<int>(0, h, 16), [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                const float coverage = ::chronon3d::detail::rounded_rect_coverage(
                    static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    static_cast<float>(w),
                    static_cast<float>(h),
                    radius);
                if (coverage >= 1.0f) continue;
                if (coverage <= 0.0f) {
                    row[x] = Color::transparent();
                } else {
                    row[x].r *= coverage;
                    row[x].g *= coverage;
                    row[x].b *= coverage;
                    row[x].a *= coverage;
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
                const float coverage = ::chronon3d::detail::rounded_rect_coverage(
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
