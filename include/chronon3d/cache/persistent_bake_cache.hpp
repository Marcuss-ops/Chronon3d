#pragma once

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace chronon3d::cache {

// ── PersistentBakeCache ────────────────────────────────────────────────
//
// Stores baked (pre-rendered) framebuffers on disk for static compositions.
// Uses a raw binary format with mmap for zero-copy reads on Linux.
// Faster than PNG/EXR for intermediate cache because:
//  - No compression/decompression overhead
//  - No color space conversion
//  - Direct pixel data in the engine's native Color format
//
// Format: CFB2 magic (Chronon FrameBuffer v2)
//   Header: 32 bytes
//     char[4]  magic      = "CFB2"
//     int32_t  width
//     int32_t  height
//     int32_t  origin_x
//     int32_t  origin_y
//     uint32_t is_opaque
//     uint64_t payload_size  (= width * height * sizeof(Color))
//   Payload: raw Color[] data

struct BakedFrameHeader {
    char     magic[4]     = {'C', 'F', 'B', '2'};
    int32_t  width        = 0;
    int32_t  height       = 0;
    int32_t  origin_x     = 0;
    int32_t  origin_y     = 0;
    uint32_t is_opaque    = 0;
    uint64_t payload_size = 0;
};

class PersistentBakeCache {
public:
    static PersistentBakeCache& instance();

    void set_cache_dir(const std::filesystem::path& path);

    // ── Read ──────────────────────────────────────────────────────────
    // Returns nullptr if not found or invalid.
    [[nodiscard]] std::shared_ptr<Framebuffer> load(
        const NodeCacheKey& key
    );

    [[nodiscard]] bool exists(const NodeCacheKey& key) const;

    // ── Write ─────────────────────────────────────────────────────────
    void store(const NodeCacheKey& key, const Framebuffer& fb);

    // ── Bulk operations ───────────────────────────────────────────────
    void store_batch(
        const std::vector<std::pair<NodeCacheKey, std::shared_ptr<Framebuffer>>>& entries
    );

    // ── Maintenance ───────────────────────────────────────────────────
    void clear();
    [[nodiscard]] size_t entry_count() const;
    [[nodiscard]] size_t total_bytes() const;

private:
    PersistentBakeCache();
    std::filesystem::path m_cache_dir;
    std::filesystem::path file_path(const NodeCacheKey& key) const;
};

} // namespace chronon3d::cache
