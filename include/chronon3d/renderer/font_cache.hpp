#pragma once

#include <chronon3d/core/types.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d {

struct CachedFont {
    std::vector<unsigned char> data; // raw TTF bytes kept alive for stbtt
    [[nodiscard]] bool valid() const { return !data.empty(); }
};

// Keyed by file path. Single-threaded use only.
class FontCache {
public:
    // Returns nullptr if the font file cannot be read.
    const CachedFont* get_or_load(const std::string& path);
    void clear();
    [[nodiscard]] usize size() const { return m_cache.size(); }

private:
    std::unordered_map<std::string, CachedFont> m_cache;
};

} // namespace chronon3d
