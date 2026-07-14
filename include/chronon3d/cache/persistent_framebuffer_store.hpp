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
//
// P1-13 — Pure instance ownership (no singleton, no process-wide static).
// The class is owned by:
//   - `runtime::RenderRuntime` per-engine (via `m_framebuffer_store`
//     value member + `framebuffer_store()` accessor AND the non-owning
//     `RenderServices::framebuffer_store` pointer field, both populated
//     in `RenderRuntime::populate()`),
//   - Stack/heap on a per-call-site basis for tests + benchmarks
//     (the test harness instantiates a fresh `PersistentFramebufferStore`
//     with a temp cache_dir per `TEST_CASE`; the micro-benchmark does
//     the same per fixture build).
//
// The legacy `instance()`/`set_store_config()`/`enabled_for_current_run()`
// singleton + static-config helpers were DELETED wholesale:
//   - `instance()`  — `[[deprecated]]` since 2026-07-12 (commit
//     `refactor(cache): mark PersistentFramebufferStore::instance()
//     @deprecated`); per AGENTS.md §GATE-MNT-01 the @deprecated
//     transition is closed by this commit (zero instant callers remain).
//   - `set_store_config()` — process-wide static config block removed;
//     replacement is `set_cache_dir(...)` per instance + an instance
//     `set_disabled(bool)` toggle (see below).
//   - `enabled_for_current_run()` — static-gated flag removed;
//     replacement is an instance `is_enabled()` accessor backed by a
//     private `m_disabled` field, defaulting to `false`.
//
// Cat-3 invariant preserved: zero new singletons/registries/caches; the
// per-instance runtime field IS the single source of truth (one store
// per engine). Public SDK ABI surface UNCHANGED at the cpp-symbol level
// for out-of-tree consumers that already use `framebuffer_store()` or
// the runtime accessor — only the 3 singleton/config symbols are gone.

class PersistentFramebufferStore {
public:
    // ── Construction ────────────────────────────────────────────────────
    // Default-constructed with a JSON-stable default `cache_dir`
    // (`output/cache/framebuffers`).  Previously private (only reachable
    // via `instance()`); P1-13 makes it public so per-TEST_CASE /
    // per-benchmark instances can stack-allocate directly.  No-op
    // side effects; use `set_cache_dir(...)` to override, `set_disabled(...)`
    // to gate I/O.
    PersistentFramebufferStore() = default;

    // ── Per-instance configuration ──────────────────────────────────────
    void set_cache_dir(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path cache_dir() const;

    /// Disable this instance from performing on-disk I/O (load/store are
    /// no-ops).  Per-instance replacement for the deleted process-wide
    /// `set_store_config(bool, ...)` static helper.  Default `false`
    /// (feature enabled).
    void set_disabled(bool disabled);
    [[nodiscard]] bool is_enabled() const noexcept;

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
    std::filesystem::path file_path(const NodeCacheKey& key) const;

    std::filesystem::path m_cache_dir{"output/cache/framebuffers"};
    bool                  m_disabled{false};
};

} // namespace chronon3d::cache
