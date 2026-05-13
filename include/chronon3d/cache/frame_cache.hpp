#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/renderer/software/framebuffer.hpp>
#include <memory>
#include <string>
#include <unordered_map>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace chronon3d::cache {

struct FrameCacheKey {
    std::string composition_id;
    Frame frame{0};
    i32 width{0};
    i32 height{0};
    u64 scene_hash{0};
    u64 render_hash{0};

    [[nodiscard]] u64 digest() const;
    [[nodiscard]] bool operator==(const FrameCacheKey&) const = default;
};

struct FrameCacheKeyHash {
    [[nodiscard]] size_t operator()(const FrameCacheKey& key) const noexcept;
};

class FrameCache {
public:
    using Value = std::shared_ptr<Framebuffer>;

    [[nodiscard]] bool contains(const FrameCacheKey& key) const;
    [[nodiscard]] const Value* find(const FrameCacheKey& key) const;
    void store(FrameCacheKey key, Value value);
    [[nodiscard]] bool erase(const FrameCacheKey& key);
    void clear();
    [[nodiscard]] usize size() const { return m_entries.size(); }

private:
    std::unordered_map<FrameCacheKey, Value, FrameCacheKeyHash> m_entries;
};

} // namespace chronon3d::cache
