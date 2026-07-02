#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <string>
#include <unordered_map>

namespace chronon3d {

class ImageRenderer {
public:
    explicit ImageRenderer(ImageCache* cache = nullptr) : m_cache(cache) {}

    void set_backend(std::shared_ptr<image::ImageBackend> backend) {
        if (m_cache) {
            m_cache->set_backend(std::move(backend));
        } else {
            spdlog::error("ImageRenderer::set_backend: no ImageCache wired — backend dropped");
        }
    }
    bool draw_image(const ImageShape& image, const RenderState& state, Framebuffer& fb);
    bool draw_image_tiled(const ImageShape& image, const RenderState& state, Framebuffer& fb);

    void clear_cache() {
        if (m_cache) {
            m_cache->clear();
        }
        std::lock_guard<std::mutex> lock(*m_rounded_mutex);
        m_rounded_framebuffers.clear();
    }

    void set_cache(ImageCache* cache) { m_cache = cache; }

private:
    [[nodiscard]] std::shared_ptr<const Framebuffer> rounded_framebuffer(
        const std::string& path,
        const CachedImage& cached,
        float radius
    );

    ImageCache* m_cache{nullptr};
    std::unique_ptr<std::mutex> m_rounded_mutex{std::make_unique<std::mutex>()};
    std::unordered_map<std::string, std::shared_ptr<const Framebuffer>> m_rounded_framebuffers;
};

} // namespace chronon3d
