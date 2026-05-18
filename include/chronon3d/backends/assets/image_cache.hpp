#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

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
    static ImageCache& instance() {
        static ImageCache inst;
        return inst;
    }

    static void preload(const std::string& path) {
        instance().get_or_load(path);
    }

    static const CachedImage* get(const std::string& path) {
        return instance().get_or_load(path);
    }

    void set_backend(std::shared_ptr<image::ImageBackend> backend) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_backend = std::move(backend);
    }
    
    [[nodiscard]] std::shared_ptr<image::ImageBackend> get_backend() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_backend;
    }

    const CachedImage* get_or_load(const std::string& path);
    void clear();
    [[nodiscard]] usize size() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cache.size();
    }

private:
    ImageCache() = default;
    ~ImageCache() = default;
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    std::shared_ptr<image::ImageBackend> m_backend;
    std::unordered_map<std::string, CachedImage> m_cache;
    std::mutex m_mutex;
};

} // namespace chronon3d

