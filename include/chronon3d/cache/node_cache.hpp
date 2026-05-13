#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/renderer/framebuffer.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace chronon3d::cache {

struct NodeCacheKey {
    std::string scope;
    Frame frame{0};
    i32 width{0};
    i32 height{0};
    u64 params_hash{0};
    u64 source_hash{0};
    u64 input_hash{0};

    [[nodiscard]] u64 digest() const;
    [[nodiscard]] bool operator==(const NodeCacheKey&) const = default;
};

struct NodeCacheKeyHash {
    [[nodiscard]] size_t operator()(const NodeCacheKey& key) const noexcept;
};

class NodeCache {
public:
    using Value = std::shared_ptr<Framebuffer>;

    [[nodiscard]] bool contains(const NodeCacheKey& key) const;
    [[nodiscard]] const Value* find(const NodeCacheKey& key) const;
    void store(NodeCacheKey key, Value value);
    [[nodiscard]] bool erase(const NodeCacheKey& key);
    void clear();
    [[nodiscard]] usize size() const { return m_entries.size(); }

private:
    std::unordered_map<NodeCacheKey, Value, NodeCacheKeyHash> m_entries;
};

} // namespace chronon3d::cache
