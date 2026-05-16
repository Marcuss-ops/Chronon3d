#include <chronon3d/backends/assets/image_cache.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d {

const CachedImage* ImageCache::get_or_load(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second.valid() ? &it->second : nullptr;
    }

    if (!m_backend) {
        spdlog::error("ImageCache: cannot load '{}' - no backend set", path);
        return nullptr;
    }

    auto buffer = m_backend->load_image(path);
    if (!buffer || !buffer->pixels) {
        // Insert invalid sentinel so we don't retry every frame.
        m_cache.emplace(path, CachedImage{});
        return nullptr;
    }

    CachedImage entry;
    entry.width = buffer->width;
    entry.height = buffer->height;

    // Create BLImage and premultiply alpha for Blend2D (PRGB32)
    entry.bl_img.create(entry.width, entry.height, BL_FORMAT_PRGB32);
    BLImageData bl_data;
    if (entry.bl_img.getData(&bl_data) == BL_SUCCESS) {
        uint32_t* dst = static_cast<uint32_t*>(bl_data.pixelData);
        const uint8_t* src = buffer->pixels.get();
        const int stride = static_cast<int>(bl_data.stride / sizeof(uint32_t));

        for (int y = 0; y < entry.height; ++y) {
            uint32_t* dst_row = dst + y * stride;
            const uint8_t* src_row = src + y * entry.width * 4;
            for (int x = 0; x < entry.width; ++x) {
                uint8_t r = src_row[x * 4 + 0];
                uint8_t g = src_row[x * 4 + 1];
                uint8_t b = src_row[x * 4 + 2];
                uint8_t a = src_row[x * 4 + 3];

                if (a == 0) {
                    dst_row[x] = 0;
                } else if (a == 255) {
                    dst_row[x] = (255u << 24) | (r << 16) | (g << 8) | b;
                } else {
                    uint32_t pr = (r * a + 127) / 255;
                    uint32_t pg = (g * a + 127) / 255;
                    uint32_t pb = (b * a + 127) / 255;
                    dst_row[x] = (static_cast<uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb;
                }
            }
        }
    }

    auto& stored = m_cache.emplace(path, std::move(entry)).first->second;
    return &stored;
}

void ImageCache::clear() {
    m_cache.clear();
}

} // namespace chronon3d
