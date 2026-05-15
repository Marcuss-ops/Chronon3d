#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/backends/text/font_backend.hpp>
#include <string>
#include <unordered_map>
#include <memory>

namespace chronon3d {

struct CachedFont {
    // We'll let the backend handle the specific font storage.
    // This class might become a thin wrapper or be removed if the backend
    // manages all caching, but for now we'll keep it as a handle.
    std::string path;
    [[nodiscard]] bool valid() const { return !path.empty(); }
};

class FontCache {
public:
    void set_backend(std::shared_ptr<text::FontBackend> backend) { m_backend = std::move(backend); }
    [[nodiscard]] std::shared_ptr<text::FontBackend> get_backend() const { return m_backend; }

    const CachedFont* get_or_load(const std::string& path);
    void clear();
    [[nodiscard]] usize size() const { return m_cache.size(); }

private:
    std::shared_ptr<text::FontBackend> m_backend;
    std::unordered_map<std::string, CachedFont> m_cache;
};

} // namespace chronon3d
