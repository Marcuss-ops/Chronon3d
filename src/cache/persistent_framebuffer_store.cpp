// =============================================================================
// persistent_framebuffer_store.cpp — CFB4 binary codec + persistent I/O.
//
// PR 5 (persistent-framebuffer-codec) — unified binary format with:
//   - Little-endian field-by-field serialization (no raw struct writes)
//   - XXH64 checksum of payload
//   - Path sharding (first 4 hex chars → 2-level subdirectory)
//   - Atomic writes (tmp → fsync → rename)
//   - Corruption handling (delete bad files → return miss)
// =============================================================================

#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <xxhash.h>

#include <mutex>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
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

namespace {
    bool              s_disabled  = false;
    std::string       s_cache_dir;
    std::once_flag    s_config_flag;
} // namespace

// ── Singleton / config ────────────────────────────────────────────────────

PersistentFramebufferStore& PersistentFramebufferStore::instance() {
    static PersistentFramebufferStore s_instance;
    return s_instance;
}

void PersistentFramebufferStore::set_store_config(bool disabled, std::string cache_dir) {
    std::call_once(s_config_flag, [&] {
        s_disabled  = disabled;
        s_cache_dir = std::move(cache_dir);
    });
}

bool PersistentFramebufferStore::enabled_for_current_run() {
#ifdef CHRONON_BUILD_TESTS
    return false;
#else
    return !s_disabled;
#endif
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

// ── Read ──────────────────────────────────────────────────────────────────

std::shared_ptr<Framebuffer> PersistentFramebufferStore::get(
    const NodeCacheKey& key)
{
    return load(key).framebuffer;
}

StoreLoadResult PersistentFramebufferStore::load(const NodeCacheKey& key) {
    CHRONON_ZONE_C("persistent_fb_load", trace_category::kPipeline);

    StoreLoadResult result{};
    if (!enabled_for_current_run()) {
        result.status = StoreLoadStatus::NotFound;
        return result;
    }

    const auto path = file_path(key);
    if (!std::filesystem::exists(path)) {
        result.status = StoreLoadStatus::NotFound;
        return result;
    }

#ifdef __linux__
    // ── Linux: mmap for zero-copy reads ────────────────────────────────
    int fd = ::open(path.string().c_str(), O_RDONLY);
    if (fd == -1) {
        result.status = StoreLoadStatus::OpenFailed;
        return result;
    }

    struct stat st{};
    if (::fstat(fd, &st) == -1) {
        ::close(fd);
        result.status = StoreLoadStatus::OpenFailed;
        return result;
    }

    const auto file_size = static_cast<size_t>(st.st_size);
    if (file_size < PersistentFramebufferHeader::kHeaderSize) {
        ::close(fd);
        // Truncated file — delete and return miss.
        spdlog::warn("[PersistentFB] truncated file ({} bytes) — deleting {}",
                     file_size, path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::TooSmall;
        return result;
    }

    void* mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) {
        result.status = StoreLoadStatus::OpenFailed;
        return result;
    }

    const auto* data = static_cast<const uint8_t*>(mapped);
    size_t pos = 0;

    // ── Parse header field-by-field (LE) ──────────────────────────────
    PersistentFramebufferHeader hdr{};

    // Magic is a byte sequence — read raw, no byte-swapping.
    if (pos + hdr.magic.size() > file_size) {
        ::munmap(mapped, file_size);
        result.status = StoreLoadStatus::TooSmall;
        return result;
    }
    std::memcpy(hdr.magic.data(), data + pos, hdr.magic.size());
    pos += hdr.magic.size();

    if (!read_le(data, file_size, pos, hdr.version)    ||
        !read_le(data, file_size, pos, hdr.header_size) ||
        !read_le(data, file_size, pos, hdr.width)       ||
        !read_le(data, file_size, pos, hdr.height)      ||
        !read_le(data, file_size, pos, hdr.allocated_width) ||
        !read_le(data, file_size, pos, hdr.origin_x)    ||
        !read_le(data, file_size, pos, hdr.origin_y)    ||
        !read_le(data, file_size, pos, hdr.pixel_format) ||
        !read_le(data, file_size, pos, hdr.flags)        ||
        !read_le(data, file_size, pos, hdr.reserved)     ||
        !read_le(data, file_size, pos, hdr.key_digest)   ||
        !read_le(data, file_size, pos, hdr.payload_bytes) ||
        !read_le(data, file_size, pos, hdr.checksum)) {
        ::munmap(mapped, file_size);
        result.status = StoreLoadStatus::TooSmall;
        return result;
    }

    // ── Validate header ───────────────────────────────────────────────
    if (hdr.magic != PersistentFramebufferHeader::kMagic) {
        ::munmap(mapped, file_size);
        spdlog::warn("[PersistentFB] bad magic in {} — deleting", path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::BadMagic;
        return result;
    }

    if (hdr.version != PersistentFramebufferHeader::kCurrentVersion) {
        ::munmap(mapped, file_size);
        // Version mismatch — delete and return miss.
        spdlog::info("[PersistentFB] version {} != {} in {} — deleting",
                     hdr.version, PersistentFramebufferHeader::kCurrentVersion,
                     path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::BadVersion;
        return result;
    }

    const size_t expected_payload =
        static_cast<size_t>(hdr.width) * hdr.height * sizeof(Color);
    if (hdr.payload_bytes != expected_payload) {
        ::munmap(mapped, file_size);
        spdlog::warn("[PersistentFB] bad payload size in {} — deleting", path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::BadSize;
        return result;
    }

    if (file_size < pos + hdr.payload_bytes) {
        ::munmap(mapped, file_size);
        spdlog::warn("[PersistentFB] truncated payload in {} — deleting", path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::TooSmall;
        return result;
    }

    // ── Verify checksum (XXH64 of payload) ────────────────────────────
    const auto* payload = data + pos;
    const u64 computed_checksum = XXH64(payload, hdr.payload_bytes, 0);
    if (computed_checksum != hdr.checksum) {
        ::munmap(mapped, file_size);
        spdlog::warn("[PersistentFB] checksum mismatch in {} — deleting", path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::ChecksumMismatch;
        return result;
    }

    // ── Validate key_digest matches the requested key ──────────────────
    if (hdr.key_digest != key.digest()) {
        ::munmap(mapped, file_size);
        result.status = StoreLoadStatus::KeyMismatch;
        return result;
    }

    // ── Reconstruct Framebuffer ───────────────────────────────────────
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

    ::munmap(mapped, file_size);
    result.status       = StoreLoadStatus::Ok;
    result.framebuffer  = std::move(fb);
    return result;

#else
    // ── Non-Linux: fstream fallback ────────────────────────────────────
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.status = StoreLoadStatus::OpenFailed;
        return result;
    }

    // Read header field-by-field.
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

    if (!read_field(hdr.magic)      || !read_field(hdr.version) ||
        !read_field(hdr.header_size) || !read_field(hdr.width)  ||
        !read_field(hdr.height)      || !read_field(hdr.allocated_width) ||
        !read_field(hdr.origin_x)    || !read_field(hdr.origin_y) ||
        !read_field(hdr.pixel_format)|| !read_field(hdr.flags)   ||
        !read_field(hdr.reserved)    || !read_field(hdr.key_digest) ||
        !read_field(hdr.payload_bytes)|| !read_field(hdr.checksum)) {
        file.close();
        std::error_code ec;
        std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::TooSmall;
        return result;
    }

    // Validate.
    if (hdr.magic != PersistentFramebufferHeader::kMagic) {
        file.close();
        std::error_code ec; std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::BadMagic; return result;
    }
    if (hdr.version != PersistentFramebufferHeader::kCurrentVersion) {
        file.close();
        std::error_code ec; std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::BadVersion; return result;
    }

    const size_t expected_payload =
        static_cast<size_t>(hdr.width) * hdr.height * sizeof(Color);
    if (hdr.payload_bytes != expected_payload) {
        file.close();
        std::error_code ec; std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::BadSize; return result;
    }

    // Read and checksum payload.
    std::vector<uint8_t> payload_buf(hdr.payload_bytes);
    if (!file.read(reinterpret_cast<char*>(payload_buf.data()),
                   static_cast<std::streamsize>(hdr.payload_bytes))) {
        file.close();
        std::error_code ec; std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::TooSmall; return result;
    }

    const u64 computed_checksum = XXH64(payload_buf.data(), hdr.payload_bytes, 0);
    if (computed_checksum != hdr.checksum) {
        file.close();
        std::error_code ec; std::filesystem::remove(path, ec);
        result.status = StoreLoadStatus::ChecksumMismatch; return result;
    }

    // Validate key_digest.
    if (hdr.key_digest != key.digest()) {
        file.close();
        result.status = StoreLoadStatus::KeyMismatch;
        return result;
    }

    // Reconstruct.
    auto fb = std::make_shared<Framebuffer>(
        static_cast<i32>(hdr.width), static_cast<i32>(hdr.height));
    fb->set_origin(static_cast<i32>(hdr.origin_x), static_cast<i32>(hdr.origin_y));
    if (hdr.flags & 1u) fb->set_opaque(true);

    if (fb->allocated_width() == hdr.width) {
        std::memcpy(fb->data(), payload_buf.data(), hdr.payload_bytes);
    } else {
        for (u32 y = 0; y < hdr.height; ++y) {
            const auto* src_row = payload_buf.data() +
                static_cast<size_t>(y) * hdr.width * sizeof(Color);
            std::memcpy(fb->pixels_row(static_cast<i32>(y)), src_row,
                       static_cast<size_t>(hdr.width) * sizeof(Color));
        }
    }

    result.status       = StoreLoadStatus::Ok;
    result.framebuffer  = std::move(fb);
    return result;
#endif
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
