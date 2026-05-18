#pragma once

#include <chronon3d/scene/shape.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>

namespace chronon3d {

class ImageRenderer {
public:
    void set_backend(std::shared_ptr<image::ImageBackend> backend) { ImageCache::instance().set_backend(std::move(backend)); }
    bool draw_image(const ImageShape& image, const RenderState& state, Framebuffer& fb);

    void clear_cache() { ImageCache::instance().clear(); }
};

} // namespace chronon3d
