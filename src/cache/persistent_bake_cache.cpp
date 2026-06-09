#include "chronon3d/cache/persistent_bake_cache.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

#ifdef __linux__
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace chronon3d::cache {

PersistentBakeCache& PersistentBakeCache::instance() {
    static PersistentBakeCache s_instance;
    return s_instance;
}

PersistentBakeCache::PersistentBakeCache() {
    const char* env = std::getenv("CHRONON_BAKE_CACHE_DIR");
    if (env && *env) {
        m_cache_dir = env;
    } else {
        m_cache_dir = std::filesystem::path("output") / "cache" / "baked";
    }
}

void PersistentBakeCache::set_cache_dir(const std::filesystem::path& path) {
    m_cache_dir = path;
}

std::filesystem::path PersistentBakeCache::file_path(const NodeCacheKey& key) const {
    std::ostringstream oss;
    oss << std::hex << key.digest() << ".cfb2";
    return m_cache_dir / oss.str();
}

std::shared_ptr<Framebuffer> PersistentBakeCache::load(const NodeCacheKey& key) {
    CHRONON_ZONE_C("bake_cache_load", trace_category::kPipeline);
    const auto path = file_path(key);

    if (!std::filesystem::exists(path)) {
        return nullptr;
    }

#ifdef __linux__
    int fd = open(path.string().c_str(), O_RDONLY);
    if (fd == -1) return nullptr;

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return nullptr;
    }

    if (st.st_size < static_cast<off_t>(sizeof(BakedFrameHeader))) {
        close(fd);
        return nullptr;
    }

    void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (mapped == MAP_FAILED) {
        return nullptr;
    }

    const auto* header = static_cast<const BakedFrameHeader*>(mapped);
    if (std::strncmp(header->magic, "CFB2", 4) != 0 ||
        header->payload_size != static_cast<uint64_t>(header->width) * header->height * sizeof(Color)) {
        munmap(mapped, st.st_size);
        return nullptr;
    }

    auto fb = std::make_shared<Framebuffer>(header->width, header->height);
    fb->set_origin(header->origin_x, header->origin_y);
    if (header->is_opaque) {
        fb->set_opaque(true);
    }

    // Row-by-row copy to handle stride differences (fast path when aligned)
    const auto* payload = static_cast<const uint8_t*>(mapped) + sizeof(BakedFrameHeader);
    if (fb->allocated_width() == fb->width()) {
        std::memcpy(fb->data(), payload, header->payload_size);
    } else {
        for (i32 y = 0; y < header->height; ++y) {
            const auto* src_row = payload + static_cast<size_t>(y) * header->width * sizeof(Color);
            std::memcpy(fb->pixels_row(y), src_row, static_cast<size_t>(header->width) * sizeof(Color));
        }
    }

    munmap(mapped, st.st_size);
    return fb;
#else
    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;

    BakedFrameHeader header;
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) return nullptr;
    if (std::strncmp(header.magic, "CFB2", 4) != 0) return nullptr;

    auto fb = std::make_shared<Framebuffer>(header.width, header.height);
    fb->set_origin(header.origin_x, header.origin_y);
    if (header.is_opaque) fb->set_opaque(true);

    // Row-by-row read to handle stride differences (fast path when aligned)
    if (fb->allocated_width() == fb->width()) {
        if (!file.read(reinterpret_cast<char*>(fb->data()), header.payload_size)) return nullptr;
    } else {
        for (i32 y = 0; y < header.height; ++y) {
            if (!file.read(reinterpret_cast<char*>(fb->pixels_row(y)),
                           static_cast<size_t>(header.width) * sizeof(Color))) return nullptr;
        }
    }
    return fb;
#endif
}

void PersistentBakeCache::store(const NodeCacheKey& key, const Framebuffer& fb) {
    CHRONON_ZONE_C("bake_cache_store", trace_category::kPipeline);

    std::error_code ec;
    std::filesystem::create_directories(m_cache_dir, ec);

    const auto path = file_path(key);
    const auto temp_path = path.string() + ".tmp";

    BakedFrameHeader header;
    header.width        = fb.width();
    header.height       = fb.height();
    header.origin_x     = fb.origin_x();
    header.origin_y     = fb.origin_y();
    header.is_opaque    = fb.is_opaque() ? 1 : 0;
    header.payload_size = static_cast<uint64_t>(fb.width()) * fb.height() * sizeof(Color);

    std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
    if (!file) return;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    // Write payload: fast path when stride == width, else row-by-row
    if (fb.allocated_width() == fb.width()) {
        file.write(reinterpret_cast<const char*>(fb.data()), header.payload_size);
    } else {
        for (i32 y = 0; y < fb.height(); ++y) {
            file.write(reinterpret_cast<const char*>(fb.pixels_row(y)),
                       static_cast<size_t>(fb.width()) * sizeof(Color));
        }
    }
    file.close();

    std::filesystem::rename(temp_path, path, ec);
}

void PersistentBakeCache::store_batch(
    const std::vector<std::pair<NodeCacheKey, std::shared_ptr<Framebuffer>>>& entries)
{
    for (const auto& [key, fb] : entries) {
        if (fb) {
            store(key, *fb);
        }
    }
}

bool PersistentBakeCache::exists(const NodeCacheKey& key) const {
    return std::filesystem::exists(file_path(key));
}

void PersistentBakeCache::clear() {
    std::error_code ec;
    std::filesystem::remove_all(m_cache_dir, ec);
    std::filesystem::create_directories(m_cache_dir, ec);
}

size_t PersistentBakeCache::entry_count() const {
    size_t count = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_cache_dir, ec)) {
        if (entry.path().extension() == ".cfb2") {
            ++count;
        }
    }
    return count;
}

size_t PersistentBakeCache::total_bytes() const {
    size_t total = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_cache_dir, ec)) {
        if (entry.path().extension() == ".cfb2") {
            total += static_cast<size_t>(entry.file_size(ec));
        }
    }
    return total;
}

} // namespace chronon3d::cache
