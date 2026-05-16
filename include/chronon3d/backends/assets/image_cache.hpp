#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <string>
#include <unordered_map>
#include <memory>

#include <blend2d.h>

namespace chronon3d {

struct CachedImage {
    int width{0};
    int height{0};
    BLImage bl_img;

    [[nodiscard]] bool valid() const { return !bl_img.empty() && width > 0 && height > 0; }
};

class ImageCache {
public:
    void set_backend(std::shared_ptr<image::ImageBackend> backend) { m_backend = std::move(backend); }
    [[nodiscard]] std::shared_ptr<image::ImageBackend> get_backend() const { return m_backend; }

    const CachedImage* get_or_load(const std::string& path);
    void clear();
    [[nodiscard]] usize size() const { return m_cache.size(); }

private:
    std::shared_ptr<image::ImageBackend> m_backend;
    std::unordered_map<std::string, CachedImage> m_cache;
};

} // namespace chronon3d
