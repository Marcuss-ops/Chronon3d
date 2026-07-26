#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace chronon3d {

class ImageRenderer {
public:
    explicit ImageRenderer(ImageCache& cache) : m_cache(cache) {}

    void set_backend(std::shared_ptr<image::ImageBackend> backend) {
        m_cache.get().set_backend(std::move(backend));
    }
    bool draw_image(const ImageShape& image, const RenderState& state, Framebuffer& fb);
    bool draw_image_tiled(const ImageShape& image, const RenderState& state, Framebuffer& fb);

    void clear_cache() {
        m_cache.get().clear();
        std::lock_guard<std::mutex> lock(*m_rounded_mutex);
        m_rounded_framebuffers.clear();
    }

private:
    [[nodiscard]] std::shared_ptr<const Framebuffer> rounded_framebuffer(
        const std::string& path,
        const CachedImage& cached,
        float radius,
        ImageDecodeOptions options
    );

    std::reference_wrapper<ImageCache> m_cache;
    std::unique_ptr<std::mutex> m_rounded_mutex{std::make_unique<std::mutex>()};
    std::unordered_map<std::string, std::shared_ptr<const Framebuffer>> m_rounded_framebuffers;
};

} // namespace chronon3d
