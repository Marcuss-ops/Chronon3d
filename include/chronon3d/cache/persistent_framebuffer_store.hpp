#pragma once

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace chronon3d::cache {

// ── PersistentFramebufferStore ────────────────────────────────────────────
//
// Unified persistent on-disk store for static / deterministic Framebuffers.
// Replaces the previous two-tier split between DiskNodeCache (CFB1,
// output/cache/nodes) and PersistentBakeCache (CFB2, output/cache/baked).
// Both classes used the same NodeCacheKey, the same payload format, and
// were inspected in the same fallback order on a cache miss — collapsing
// them removes ~140 lines of duplicated mmap+rename logic and one
// `instance()` lock acquisition on every read.
//
// File format: CFB3 magic ("Chronon FrameBuffer v3")
//   Header: 32 bytes
//     char[4]  magic        = "CFB3"
//     int32_t  width
//     int32_t  height
//     int32_t  origin_x
//     int32_t  origin_y
//     uint32_t is_opaque
//     uint64_t payload_size (= width*height*sizeof(Color))
//   Payload: raw Color[] data, written row-by-row when allocated_width !=
//            width (stride-aware) and as a single contiguous memcpy when
//            stride == width (the common case).
//
// Linux: zero-copy reads via mmap + MAP_PRIVATE. Atomic writes via
// rename(tmp, final) so a partial flush never replaces a valid entry.
// Non-Linux: fstream fallback with the same row-aware path.
//
// Default cache directory: output/cache/framebuffers (override with
// CHRONON_PERSISTENT_FB_CACHE_DIR or set_cache_dir()).

struct Framebuffer3Header {
    char     magic[4]     = {'C', 'F', 'B', '3'};
    int32_t  width        = 0;
    int32_t  height       = 0;
    int32_t  origin_x     = 0;
    int32_t  origin_y     = 0;
    uint32_t is_opaque    = 0;
    uint64_t payload_size = 0;
};

class PersistentFramebufferStore {
public:
    static PersistentFramebufferStore& instance();

    // ── Static gate used by callers to short-circuit before key gen
    // (rarely needed: get()/put() also check the gate internally; callers
    // should still gate inside hot loops so the store doesn't see the work).
    [[nodiscard]] static bool enabled_for_current_run();

    // ── Configuration ─────────────────────────────────────────────
    void set_cache_dir(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path cache_dir() const;

    // ── Read / Write ──────────────────────────────────────────────
    // Returns nullptr when the entry is missing, the file is corrupted,
    // or the store is currently disabled via CHRONON_DISABLE_PERSISTENT_FB_CACHE.
    [[nodiscard]] std::shared_ptr<Framebuffer> get(const NodeCacheKey& key);

    // No-op when disabled or when the directory cannot be created.
    void put(const NodeCacheKey& key, const Framebuffer& fb);

    // ── Bulk operations ───────────────────────────────────────────
    void put_batch(
        const std::vector<std::pair<NodeCacheKey, std::shared_ptr<Framebuffer>>>& entries
    );

    // ── Maintenance / introspection ───────────────────────────────
    [[nodiscard]] bool exists(const NodeCacheKey& key) const;
    void clear();
    [[nodiscard]] std::size_t entry_count() const;
    [[nodiscard]] std::size_t total_bytes() const;

private:
    PersistentFramebufferStore();
    std::filesystem::path file_path(const NodeCacheKey& key) const;

    std::filesystem::path m_cache_dir;
};

} // namespace chronon3d::cache
