// =============================================================================
// persistent_framebuffer_store.cpp — CFB4 binary codec + persistent I/O.
//
// Reader is gated behind CHRONON3D_USE_MMAP=1 on POSIX:
//   * default  (unset / != "1") — std::ifstream cross-platform path
//   * set to "1" on Linux       — ::mmap zero-copy path
//
// On non-Linux builds the env var is ignored (mmap helper is #ifdef'd
// out); std::ifstream is always used.  Writer is unchanged — std::ofstream
// with atomic tmp→fsync→rename.
//
// Public ABI: unchanged.  No new methods, types, or symbols in
// include/chronon3d/.
// =============================================================================

#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <xxhash.h>

#include <atomic>
#include <cstdlib>     // std::getenv — CHRONON3D_USE_MMAP gate
#include <mutex>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>    // mmap, getpid, etc.
#else
#include <chrono>
#include <thread>
#endif

namespace chronon3d::cache {

// ── Little-endian I/O helpers ─────────────────────────────────────────────

namespace {

/// Write a value in little-endian to an ostream.
/// Only used for multi-byte integer types — NOT for byte sequences (magic).
template <typename T>
void write_le(std::ostream& os, T value) {
    uint8_t buf[sizeof(T)];
    std::memcpy(buf, &value, sizeof(T));
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    std::reverse(buf, buf + sizeof(T));
#endif
    os.write(reinterpret_cast<const char*>(buf), sizeof(T));
}

/// Read a little-endian value from a buffer at the given offset.
/// Advances `pos` by sizeof(T).  Returns false if out of bounds.
/// Only used for multi-byte integer types — NOT for byte sequences.
template <typename T>
bool read_le(const uint8_t* data, size_t size, size_t& pos, T& out) {
    if (pos + sizeof(T) > size) return false;
    uint8_t buf[sizeof(T)];
    std::memcpy(buf, data + pos, sizeof(T));
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    std::reverse(buf, buf + sizeof(T));
#endif
    std::memcpy(&out, buf, sizeof(T));
    pos += sizeof(T);
    return true;
}

/// `CHRONON3D_USE_MMAP=1` gates the zero-copy mmap reader on POSIX.
/// Default false (keeps the cross-platform fopen path).  Consulted on
/// every `load()` call — cheap getenv(3) syscall, dominated by file I/O.
///
/// Consulted per-call so the micro-benchmark in
/// `tests/bench/micro_benchmarks.cpp` can flip the gate via
/// `setenv(3)` between iterations without a process restart.
inline bool use_mmap_reader() {
#ifdef __linux__
    if (const char* e = std::getenv("CHRONON3D_USE_MMAP")) {
        return std::string_view{e} == "1";
    }
    return false;
#else
    return false;
#endif
}

} // namespace

// ── Path construction ─────────────────────────────────────────────────────

std::filesystem::path PersistentFramebufferStore::file_path(
    const NodeCacheKey& key) const
{
    // Path sharding: first 4 hex chars → 2-level subdirectory.
    // e.g. digest = 0xABCD1234...  →  ab/cd/ABCD1234....cfb4
    std::ostringstream hex;
    hex << std::hex << std::setfill('0') << std::setw(16) << key.digest();

    const std::string hex_str = hex.str();   // e.g. "abcd1234deadbeef"
    const std::string sub1    = hex_str.substr(0, 2);  // "ab"
    const std::string sub2    = hex_str.substr(2, 2);  // "cd"
    const std::string filename = hex_str + ".cfb4";

    return m_cache_dir / sub1 / sub2 / filename;
}

// ── Static config state (injected by SoftwareRenderer at startup) ────────

// FASE 3 (TICKET-079) — Static config (disabled + cache_dir) set once at startup
// by SoftwareRenderer.  First-call-wins via atomic CAS; eliminates std::once_flag
// + std::call_once (per AGENTS.md pattern `is serialised + idempotent without an
// external std::once_flag`).
namespace {
    bool              s_disabled  = false;
    std::string       s_cache_dir;
    std::atomic<bool> s_config_set{false};
} // namespace

// ── Singleton / config ────────────────────────────────────────────────────

PersistentFramebufferStore& PersistentFramebufferStore::instance() {
    static PersistentFramebufferStore s_instance;
    return s_instance;
}

void PersistentFramebufferStore::set_store_config(bool disabled, std::string cache_dir) {
    bool expected = false;
    if (s_config_set.compare_exchange_strong(
            expected, true,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
        s_disabled  = disabled;
        s_cache_dir = std::move(cache_dir);
    }
}

bool PersistentFramebufferStore::enabled_for_current_run() {
    return !s_disabled;
}

PersistentFramebufferStore::PersistentFramebufferStore() {
    if (!s_cache_dir.empty()) {
        m_cache_dir = s_cache_dir;
    } else {
        m_cache_dir = std::filesystem::path("output") / "cache" / "framebuffers";
    }
}

void PersistentFramebufferStore::set_cache_dir(const std::filesystem::path& path) {
    m_cache_dir = path;
}

std::filesystem::path PersistentFramebufferStore::cache_dir() const {
    return m_cache_dir;
}

// ── Read helpers (extracted from the legacy monolithic load()) ────────────
//
// `load_via_ifstream` is the cross-platform default (used unconditionally
// on non-Linux builds; used on Linux when CHRONON3D_USE_MMAP != "1").
// `load_via_mmap` is the Linux-only opt-in zero-copy path (used when
// CHRONON3D_USE_MMAP=1).  Both share `reconstruct_framebuffer()` for the
// pixel-copy / stride-aware unpacking so the output is bit-identical.

namespace {

/// Reconstruct a Framebuffer from a validated header + payload pointer.
/// Bit-identical to the pre-PR inline reconstruction logic that lived
/// inside `load()` (now extracted so both reader helpers share it).
StoreLoadResult reconstruct_framebuffer(
    const NodeCacheKey& key,
    const PersistentFramebufferHeader& hdr,
    const uint8_t* payload)
{
    auto fb = std::make_shared<Framebuffer>(
        static_cast<i32>(hdr.width), static_cast<i32>(hdr.height));
    fb->set_origin(static_cast<i32>(hdr.origin_x), static_cast<i32>(hdr.origin_y));
    if (hdr.flags & 1u) fb->set_opaque(true);

    if (fb->allocated_width() == hdr.width) {
        std::memcpy(fb->data(), payload, hdr.payload_bytes);
    } else {
        for (u32 y = 0; y < hdr.height; ++y) {
            const auto* src_row = payload +
                static_cast<size_t>(y) * hdr.width * sizeof(Color);
            std::memcpy(fb->pixels_row(static_cast<i32>(y)), src_row,
                       static_cast<size_t>(hdr.width) * sizeof(Color));
        }
    }

    StoreLoadResult result{};
    result.status      = StoreLoadStatus::Ok;
    result.framebuffer = std::move(fb);
    return result;
}

/// Validate parsed header fields + payload against expected constants.
/// Checks magic, version, payload size, checksum, and key digest.
/// Returns the error status on failure (logs warnings).  Does NOT
/// delete the file — the caller handles I/O cleanup (munmap/close +
/// conditional std::filesystem::remove).
/// On success returns StoreLoadStatus::Ok.
StoreLoadStatus validate_loaded_header(
    const NodeCacheKey& key,
    const PersistentFramebufferHeader& hdr,
    const uint8_t* payload,
    const std::filesystem::path& path)
{
    if (hdr.magic != PersistentFramebufferHeader::kMagic) {
        spdlog::warn("[PersistentFB] bad magic in {} — deleting", path.string());
        return StoreLoadStatus::BadMagic;
    }
    if (hdr.version != PersistentFramebufferHeader::kCurrentVersion) {
        spdlog::info("[PersistentFB] version {} != {} in {} — deleting",
                     hdr.version, PersistentFramebufferHeader::kCurrentVersion,
                     path.string());
        return StoreLoadStatus::BadVersion;
    }
    const size_t expected_payload =
        static_cast<size_t>(hdr.width) * hdr.height * sizeof(Color);
    if (hdr.payload_bytes != expected_payload) {
        spdlog::warn("[PersistentFB] bad payload size in {} — deleting", path.string());
        return StoreLoadStatus::BadSize;
    }
    const u64 computed_checksum = XXH64(payload, hdr.payload_bytes, 0);
    if (computed_checksum != hdr.checksum) {
        spdlog::warn("[PersistentFB] checksum mismatch in {} — deleting", path.string());
        return StoreLoadStatus::ChecksumMismatch;
    }
    if (hdr.key_digest != key.digest()) {
        return StoreLoadStatus::KeyMismatch;
    }
    return StoreLoadStatus::Ok;
}

#ifdef __linux__
/// Linux-only zero-copy reader used when `use_mmap_reader()` returned
/// true.  Validates header + checksum, then dispatches to
/// `reconstruct_framebuffer(hdr, mapped_payload)`.  Any corruption
/// (bad magic, version, size, checksum, key mismatch, truncation)
/// deletes the file and returns the appropriate status — bit-identical
/// to the inline body that lived inside the pre-PR `load()`.
StoreLoadResult load_via_mmap(const NodeCacheKey& key,
                              const std::filesystem::path& path) {
    int fd = ::open(path.string().c_str(), O_RDONLY);
    if (fd == -1) {
        StoreLoadResult r{};
        r.status = StoreLoadStatus::OpenFailed;
        return r;
    }

    struct stat st{};
    if (::fstat(fd, &st) == -1) {
        ::close(fd);
        StoreLoadResult r{};
        r.status = StoreLoadStatus::OpenFailed;
        return r;
    }

    const auto file_size = static_cast<size_t>(st.st_size);
    if (file_size < PersistentFramebufferHeader::kHeaderSize) {
        ::close(fd);
        spdlog::warn("[PersistentFB] truncated file ({} bytes) — deleting {}",
                     file_size, path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        StoreLoadResult r{};
        r.status = StoreLoadStatus::TooSmall;
        return r;
    }

    void* mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) {
        StoreLoadResult r{};
        r.status = StoreLoadStatus::OpenFailed;
        return r;
    }

    const auto* data = static_cast<const uint8_t*>(mapped);
    size_t pos = 0;

    PersistentFramebufferHeader hdr{};
    if (pos + hdr.magic.size() > file_size) {
        ::munmap(mapped, file_size);
        StoreLoadResult r{};
        r.status = StoreLoadStatus::TooSmall;
        return r;
    }
    std::memcpy(hdr.magic.data(), data + pos, hdr.magic.size());
    pos += hdr.magic.size();

    if (!read_le(data, file_size, pos, hdr.version)         ||
        !read_le(data, file_size, pos, hdr.header_size)    ||
        !read_le(data, file_size, pos, hdr.width)          ||
        !read_le(data, file_size, pos, hdr.height)         ||
        !read_le(data, file_size, pos, hdr.allocated_width)||
        !read_le(data, file_size, pos, hdr.origin_x)       ||
        !read_le(data, file_size, pos, hdr.origin_y)       ||
        !read_le(data, file_size, pos, hdr.pixel_format)   ||
        !read_le(data, file_size, pos, hdr.flags)          ||
        !read_le(data, file_size, pos, hdr.reserved)       ||
        !read_le(data, file_size, pos, hdr.key_digest)     ||
        !read_le(data, file_size, pos, hdr.payload_bytes)  ||
        !read_le(data, file_size, pos, hdr.checksum)) {
        ::munmap(mapped, file_size);
        StoreLoadResult r{};
        r.status = StoreLoadStatus::TooSmall;
        return r;
    }

    // Truncation check (mmap-specific — verifies the mapped region
    // contains the full payload before we touch it).
    if (file_size < pos + hdr.payload_bytes) {
        ::munmap(mapped, file_size);
        spdlog::warn("[PersistentFB] truncated payload in {} — deleting", path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        StoreLoadResult r{};
        r.status = StoreLoadStatus::TooSmall;
        return r;
    }

    const auto* payload = data + pos;

    // Common header validation (magic, version, payload size, checksum, key).
    auto status = validate_loaded_header(key, hdr, payload, path);
    if (status != StoreLoadStatus::Ok) {
        ::munmap(mapped, file_size);
        if (status != StoreLoadStatus::KeyMismatch) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
        StoreLoadResult r{};
        r.status = status;
        return r;
    }

    auto result = reconstruct_framebuffer(key, hdr, payload);
    ::munmap(mapped, file_size);
    return result;
}
#endif // __linux__

/// Cross-platform fopen fallback.  Reads the entire payload into a
/// std::vector<uint8_t>, then dispatches to `reconstruct_framebuffer`.
/// Used unconditionally on non-Linux builds and on Linux when
/// CHRONON3D_USE_MMAP != "1" (the default).
StoreLoadResult load_via_ifstream(const NodeCacheKey& key,
                                  const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        StoreLoadResult r{};
        r.status = StoreLoadStatus::OpenFailed;
        return r;
    }

    PersistentFramebufferHeader hdr{};
    auto read_field = [&](auto& field) {
        uint8_t buf[sizeof(field)];
        if (!file.read(reinterpret_cast<char*>(buf), sizeof(buf))) return false;
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        std::reverse(buf, buf + sizeof(buf));
#endif
        std::memcpy(&field, buf, sizeof(field));
        return true;
    };

    if (!read_field(hdr.magic)        || !read_field(hdr.version) ||
        !read_field(hdr.header_size)  || !read_field(hdr.width)    ||
        !read_field(hdr.height)       || !read_field(hdr.allocated_width) ||
        !read_field(hdr.origin_x)     || !read_field(hdr.origin_y) ||
        !read_field(hdr.pixel_format) || !read_field(hdr.flags)   ||
        !read_field(hdr.reserved)     || !read_field(hdr.key_digest) ||
        !read_field(hdr.payload_bytes)|| !read_field(hdr.checksum)) {
        file.close();
        std::error_code ec;
        std::filesystem::remove(path, ec);
        StoreLoadResult r{};
        r.status = StoreLoadStatus::TooSmall;
        return r;
    }

    // Read payload before validation (ifstream path — reads into a
    // heap buffer; the mmap path validates bounds instead).
    std::vector<uint8_t> payload_buf(hdr.payload_bytes);
    if (!file.read(reinterpret_cast<char*>(payload_buf.data()),
                   static_cast<std::streamsize>(hdr.payload_bytes))) {
        file.close();
        std::error_code ec;
        std::filesystem::remove(path, ec);
        StoreLoadResult r{};
        r.status = StoreLoadStatus::TooSmall;
        return r;
    }

    // Common header validation (magic, version, payload size, checksum, key).
    auto status = validate_loaded_header(key, hdr, payload_buf.data(), path);
    if (status != StoreLoadStatus::Ok) {
        file.close();
        if (status != StoreLoadStatus::KeyMismatch) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
        StoreLoadResult r{};
        r.status = status;
        return r;
    }

    return reconstruct_framebuffer(key, hdr, payload_buf.data());
}

} // namespace

// ── Read (dispatch) ──────────────────────────────────────────────────────

std::shared_ptr<Framebuffer> PersistentFramebufferStore::get(
    const NodeCacheKey& key)
{
    return load(key).framebuffer;
}

StoreLoadResult PersistentFramebufferStore::load(const NodeCacheKey& key) {
    CHRONON_ZONE_C("persistent_fb_load", trace_category::kPipeline);

    if (!enabled_for_current_run()) {
        StoreLoadResult result{};
        result.status = StoreLoadStatus::NotFound;
        return result;
    }

    const auto path = file_path(key);
    if (!std::filesystem::exists(path)) {
        StoreLoadResult result{};
        result.status = StoreLoadStatus::NotFound;
        return result;
    }

    // Gate: mmap only when CHRONON3D_USE_MMAP=1 on POSIX (see
    // `use_mmap_reader()` above).  Default keeps the cross-platform
    // fopen path — existing builds preserve identical behaviour.
#ifdef __linux__
    if (use_mmap_reader()) {
        return load_via_mmap(key, path);
    }
#endif
    return load_via_ifstream(key, path);
}

// ── Write ─────────────────────────────────────────────────────────────────

StoreWriteResult PersistentFramebufferStore::store(
    const NodeCacheKey& key,
    const Framebuffer&  fb)
{
    CHRONON_ZONE_C("persistent_fb_store", trace_category::kPipeline);

    StoreWriteResult result{};
    if (!enabled_for_current_run()) {
        return result;
    }

    const auto path = file_path(key);

    // Create parent directories (including sharding subdirs).
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        spdlog::warn("[PersistentFB] cannot create dirs {}: {}",
                     path.parent_path().string(), ec.message());
        return result;
    }

    // Temp file: <digest>.tmp.<unique-suffix>
    // Portable: PID on Linux, thread::id + clock on other platforms.
    std::string suffix;
#ifdef __linux__
    suffix = std::to_string(::getpid());
#else
    suffix = std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
             "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
    const auto temp_path = path.string() + ".tmp." + suffix;

    std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
    if (!file) return result;

    // ── Compute checksum from Framebuffer in memory ──────────────────
    const u64 payload_bytes =
        static_cast<u64>(fb.width()) * fb.height() * sizeof(Color);

    u64 checksum;
    if (fb.allocated_width() == fb.width()) {
        checksum = XXH64(fb.data(), payload_bytes, 0);
    } else {
        // Stride-aware: accumulate row-by-row into a contiguous buffer.
        std::vector<uint8_t> buf(payload_bytes);
        for (i32 y = 0; y < fb.height(); ++y) {
            std::memcpy(buf.data() + static_cast<size_t>(y) * fb.width() * sizeof(Color),
                       fb.pixels_row(y),
                       static_cast<size_t>(fb.width()) * sizeof(Color));
        }
        checksum = XXH64(buf.data(), payload_bytes, 0);
    }

    // ── Write header field-by-field (LE) ──────────────────────────────
    // Magic is a byte sequence — write raw, no byte-swapping.
    file.write(PersistentFramebufferHeader::kMagic.data(),
               static_cast<std::streamsize>(PersistentFramebufferHeader::kMagic.size()));
    write_le(file, PersistentFramebufferHeader::kCurrentVersion);
    write_le(file, PersistentFramebufferHeader::kHeaderSize);
    write_le(file, static_cast<u32>(fb.width()));
    write_le(file, static_cast<u32>(fb.height()));
    write_le(file, static_cast<u32>(fb.allocated_width()));
    write_le(file, static_cast<i32>(fb.origin_x()));
    write_le(file, static_cast<i32>(fb.origin_y()));
    write_le(file, u32{0});                      // pixel_format = Color
    write_le(file, fb.is_opaque() ? u32{1} : u32{0});  // flags
    write_le(file, u32{0});                      // reserved
    write_le(file, key.digest());                // key_digest
    write_le(file, payload_bytes);
    write_le(file, checksum);

    // ── Write payload ─────────────────────────────────────────────────
    if (fb.allocated_width() == fb.width()) {
        file.write(reinterpret_cast<const char*>(fb.data()),
                   static_cast<std::streamsize>(payload_bytes));
    } else {
        for (i32 y = 0; y < fb.height(); ++y) {
            file.write(reinterpret_cast<const char*>(fb.pixels_row(y)),
                       static_cast<std::streamsize>(fb.width()) * sizeof(Color));
        }
    }

    file.flush();
    file.close();

    // ── Atomic rename ─────────────────────────────────────────────────
    std::filesystem::rename(temp_path, path, ec);
    if (ec) {
        spdlog::warn("[PersistentFB] rename failed {} → {}: {}",
                     temp_path, path.string(), ec.message());
        std::filesystem::remove(temp_path, ec);
        return result;
    }

    result.ok            = true;
    result.bytes_written = static_cast<size_t>(std::filesystem::file_size(path, ec));
    return result;
}

// ── Bulk / maintenance ────────────────────────────────────────────────────

void PersistentFramebufferStore::put_batch(
    const std::vector<std::pair<NodeCacheKey, std::shared_ptr<Framebuffer>>>& entries)
{
    for (const auto& [key, fb] : entries) {
        if (fb) store(key, *fb);
    }
}

bool PersistentFramebufferStore::contains(const NodeCacheKey& key) const {
    if (!enabled_for_current_run()) return false;
    return std::filesystem::exists(file_path(key));
}

bool PersistentFramebufferStore::erase(const NodeCacheKey& key) {
    std::error_code ec;
    return std::filesystem::remove(file_path(key), ec);
}

void PersistentFramebufferStore::clear() {
    std::error_code ec;
    std::filesystem::remove_all(m_cache_dir, ec);
    std::filesystem::create_directories(m_cache_dir, ec);
}

PersistentStoreStats PersistentFramebufferStore::stats() const {
    PersistentStoreStats s{};
    if (!std::filesystem::exists(m_cache_dir)) return s;

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(m_cache_dir, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->path().extension() == ".cfb4") {
            ++s.entry_count;
            s.total_bytes += static_cast<size_t>(it->file_size(ec));
        }
    }
    return s;
}

} // namespace chronon3d::cache
