struct FileIdentity {
    std::uint64_t device{0};
    std::uint64_t inode{0};

    friend bool operator==(const FileIdentity&, const FileIdentity&) = default;
};

[[nodiscard]] FileIdentity file_identity(const std::filesystem::path& path) {
#if !defined(_WIN32)
    struct stat info {};
    if (::stat(path.c_str(), &info) == 0) {
        return {static_cast<std::uint64_t>(info.st_dev),
                static_cast<std::uint64_t>(info.st_ino)};
    }
#else
    (void)path;
#endif
    return {};
}

struct DigestCacheEntry {
    std::uint32_t algorithm_version{1};
    std::uint64_t byte_size{0};
    std::int64_t timestamp{0};
    FileIdentity identity{};
    ContentDigest digest{};
};

struct CachedDigest {
    ContentDigest digest{};
    bool hit{false};
    bool invalidated{false};
    std::uint64_t bytes_hashed{0};
    double cache_lookup_ms{0.0};
    double cache_write_ms{0.0};
    double full_hash_ms{0.0};
};

// Process-persistent and shared by Direct-YUV, FullGraph, and future worker
// jobs. Holding this mutex through a miss is intentional: it is the single-
// flight guarantee. Two jobs cannot read and hash the same immutable asset at
// the same time, while different assets remain safely serialized at this
// small preflight boundary.
class PreparedAssetDigestCache {
public:
    static constexpr std::uint32_t kAlgorithmVersion = 1;

    [[nodiscard]] Result<CachedDigest, AssetPreflightError> resolve(
        const std::filesystem::path& canonical,
        std::string logical_path,
        std::uint64_t byte_size,
        std::int64_t timestamp,
        FileIdentity identity,
        std::uint64_t max_size) {
        const auto lookup_t0 = chronon3d::profiling::now();
        std::lock_guard<std::mutex> lock(m_mutex);
        const double lookup_ms = chronon3d::profiling::duration_ms(
            lookup_t0, chronon3d::profiling::now());
        m_stats.cache_lookup_ms += lookup_ms;
        const auto key = canonical.generic_string();
        const auto found = m_entries.find(key);
        const bool invalidated = found != m_entries.end();
        if (found != m_entries.end() &&
            found->second.algorithm_version == kAlgorithmVersion &&
            found->second.byte_size == byte_size &&
            found->second.timestamp == timestamp &&
            found->second.identity == identity) {
            ++m_stats.hits;
            return CachedDigest{found->second.digest, true, false, 0U,
                                lookup_ms, 0.0, 0.0};
        }

        ++m_stats.misses;
        if (invalidated) ++m_stats.invalidations;
        const auto hash_t0 = chronon3d::profiling::now();
        auto digest = hash_file(canonical, logical_path, byte_size, max_size);
        if (!digest) return std::move(digest).error();
        const double hash_ms = chronon3d::profiling::duration_ms(
            hash_t0, chronon3d::profiling::now());
        m_stats.bytes_hashed += byte_size;
        m_stats.full_hash_ms += hash_ms;
        const auto write_t0 = chronon3d::profiling::now();
        m_entries[key] = DigestCacheEntry{kAlgorithmVersion, byte_size,
                                           timestamp, identity, digest.value()};
        const double write_ms = chronon3d::profiling::duration_ms(
            write_t0, chronon3d::profiling::now());
        m_stats.cache_write_ms += write_ms;
        return CachedDigest{std::move(digest).value(), false, invalidated,
                            byte_size, lookup_ms, write_ms, hash_ms};
    }

    void discard(const std::filesystem::path& canonical) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.erase(canonical.generic_string());
    }

    [[nodiscard]] AssetDigestCacheStats stats() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_stats;
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, DigestCacheEntry> m_entries;
    AssetDigestCacheStats m_stats{};
};

[[nodiscard]] PreparedAssetDigestCache& prepared_asset_digest_cache() {
    static PreparedAssetDigestCache cache;
    return cache;
}

void add_u64(Sha256& sha, std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(value >> (i * 8U));
    sha.update(bytes.data(), bytes.size());
}
