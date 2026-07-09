#pragma once

// ── persistent_framebuffer_store.hpp ─────────────────────────────────────
//
// Unified persistent on-disk store for static / deterministic Framebuffers.
//
// Binary format: all multi-byte fields are little-endian, written field-by-
// field (no raw C-struct serialization — endian-safe, padding-free).
//
// File header (CFB4 magic, 72 bytes):
//   Offset  Size  Field
//     0      8    magic       = "CHRONFB4"
//     8      4    version     = 1
//    12      4    header_size = 72
//    16      4    width
//    20      4    height
//    24      4    allocated_width
//    28      4    origin_x
//    32      4    origin_y
//    36      4    pixel_format (0 = Color/float)
//    40      4    flags
//    44      4    reserved
//    48      8    key_digest
//    56      8    payload_bytes
//    64      8    checksum (XXH64 of payload only)
//
// Payload: width * height * sizeof(Color) bytes, written row-by-row when
// allocated_width != width, or contiguous when stride matches.
//
// Path: <root>/<ns>/<hex0-1>/<hex2-3>/<digest>.cfb4
//       e.g. output/cache/framebuffers/a1/b2/abcdef1234567890.cfb4
//
// Atomic writes: write to <digest>.tmp.<pid> → fsync → rename to .cfb4.
// Corruption: magic/version mismatch or checksum fail → delete file → return miss.

#include <array>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/types.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::runtime { class RenderRuntime; }

namespace chronon3d::cache {

// ── PersistentFramebufferHeader (CFB4, 72 bytes) ─────────────────────────

struct PersistentFramebufferHeader {
    static constexpr std::array<char, 8> kMagic = {'C','H','R','O','N','F','B','4'};
    static constexpr u32 kCurrentVersion = 1;
    static constexpr u32 kHeaderSize     = 72;

    // On-disk layout (little-endian, 72 bytes total):
    std::array<char, 8> magic{};
    u32  version{0};
    u32  header_size{0};
    u32  width{0};
    u32  height{0};
    u32  allocated_width{0};
    i32  origin_x{0};
    i32  origin_y{0};
    u32  pixel_format{0};   // 0 = Color (float RGBA)
    u32  flags{0};          // bit 0: is_opaque
    u32  reserved{0};
    u64  key_digest{0};
    u64  payload_bytes{0};
    u64  checksum{0};       // XXH64 of the payload only
};

/// Detailed load result (for diagnostics / corruption reporting).
enum class StoreLoadStatus {
    Ok,
    NotFound,
    OpenFailed,
    TooSmall,
    BadMagic,
    BadVersion,
    BadSize,
    ChecksumMismatch,
    KeyMismatch,
};

struct StoreLoadResult {
    StoreLoadStatus              status{StoreLoadStatus::NotFound};
    std::shared_ptr<Framebuffer> framebuffer;
};

struct StoreWriteResult {
    bool   ok{false};
    size_t bytes_written{0};
};

struct PersistentStoreStats {
    size_t entry_count{0};
    size_t total_bytes{0};
};

// ── PersistentFramebufferStore ────────────────────────────────────────────

class PersistentFramebufferStore {
public:
    [[deprecated("Use runtime().framebuffer_store() where a RenderRuntime& is in scope; instance() is the singleton bootstrap fallback only")]]

    static PersistentFramebufferStore& instance();
    friend class chronon3d::runtime::RenderRuntime;

    /// Feature-flag gate (checked by load/store internally; callers can
    /// short-circuit before key generation for hot-path avoidance).
    [[nodiscard]] static bool enabled_for_current_run();

    // ── Configuration ─────────────────────────────────────────────────
    static void set_store_config(bool disabled, std::string cache_dir);

    void set_cache_dir(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path cache_dir() const;

    // ── Read / Write ──────────────────────────────────────────────────
    /// Load a framebuffer from disk.  Returns nullptr on miss/corruption.
    /// Corrupted files are deleted automatically (version mismatch,
    /// checksum fail, truncated).
    [[nodiscard]] std::shared_ptr<Framebuffer> get(const NodeCacheKey& key);

    /// Detailed load with status (for test diagnostics).
    [[nodiscard]] StoreLoadResult load(const NodeCacheKey& key);

    /// Store a framebuffer to disk.  Atomic (temp → rename).
    /// No-op when disabled or when the directory can't be created.
    StoreWriteResult store(const NodeCacheKey& key, const Framebuffer& fb);

    /// Convenience: store with the same signature as the old `put()`.
    void put(const NodeCacheKey& key, const Framebuffer& fb) {
        store(key, fb);
    }

    // ── Bulk operations ───────────────────────────────────────────────
    void put_batch(
        const std::vector<std::pair<NodeCacheKey, std::shared_ptr<Framebuffer>>>& entries);

    // ── Maintenance / introspection ───────────────────────────────────
    [[nodiscard]] bool contains(const NodeCacheKey& key) const;
    [[nodiscard]] bool exists(const NodeCacheKey& key) const { return contains(key); }

    bool erase(const NodeCacheKey& key);
    void clear();
    [[nodiscard]] PersistentStoreStats stats() const;
    [[nodiscard]] std::size_t entry_count() const { return stats().entry_count; }
    [[nodiscard]] std::size_t total_bytes() const  { return stats().total_bytes; }

private:
    PersistentFramebufferStore();
    std::filesystem::path file_path(const NodeCacheKey& key) const;

    std::filesystem::path m_cache_dir;
};

} // namespace chronon3d::cache
