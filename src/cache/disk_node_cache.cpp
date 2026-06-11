#include <chronon3d/cache/disk_node_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>

#ifdef __linux__
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

namespace chronon3d::cache {

DiskNodeCache& DiskNodeCache::instance() {
    static DiskNodeCache s_instance;
    return s_instance;
}

DiskNodeCache::DiskNodeCache() {
    auto& dir = Config::get().disk_cache_dir;
    if (!dir.empty()) {
        m_cache_dir = dir;
    } else {
        m_cache_dir = std::filesystem::path("output") / "cache" / "nodes";
    }
}

void DiskNodeCache::set_cache_dir(const std::filesystem::path& path) {
    m_cache_dir = path;
}

std::filesystem::path DiskNodeCache::cache_dir() const {
    return m_cache_dir;
}

static std::filesystem::path get_file_path(const std::filesystem::path& dir, const NodeCacheKey& key) {
    std::ostringstream oss;
    oss << std::hex << key.digest() << ".cfb"; // chronon framebuffer
    return dir / oss.str();
}

struct DiskHeader {
    char magic[4] = {'C', 'F', 'B', '1'};
    int32_t width = 0;
    int32_t height = 0;
    int32_t origin_x = 0;
    int32_t origin_y = 0;
    uint32_t is_opaque = 0;
    uint64_t payload_size = 0;
};

std::shared_ptr<Framebuffer> DiskNodeCache::get(const NodeCacheKey& key) {
    CHRONON_ZONE_C("disk_cache_get", trace_category::kPipeline);
    const auto path = get_file_path(m_cache_dir, key);

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

    if (st.st_size < static_cast<off_t>(sizeof(DiskHeader))) {
        close(fd);
        return nullptr;
    }

    void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (mapped == MAP_FAILED) {
        return nullptr;
    }

    const DiskHeader* header = static_cast<const DiskHeader*>(mapped);
    if (std::strncmp(header->magic, "CFB1", 4) != 0 || 
        header->payload_size != static_cast<uint64_t>(header->width) * header->height * sizeof(Color)) {
        munmap(mapped, st.st_size);
        return nullptr;
    }

    auto fb = std::make_shared<Framebuffer>(header->width, header->height);
    fb->set_origin(header->origin_x, header->origin_y);
    if (header->is_opaque) {
        fb->set_opaque(true);
    }
    
    const uint8_t* payload = static_cast<const uint8_t*>(mapped) + sizeof(DiskHeader);
    std::memcpy(fb->data(), payload, header->payload_size);

    munmap(mapped, st.st_size);
    return fb;
#else
    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;

    DiskHeader header;
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) return nullptr;

    if (std::strncmp(header.magic, "CFB1", 4) != 0) return nullptr;

    auto fb = std::make_shared<Framebuffer>(header.width, header.height);
    fb->set_origin(header.origin_x, header.origin_y);
    if (header.is_opaque) fb->set_opaque(true);

    if (!file.read(reinterpret_cast<char*>(fb->data()), header.payload_size)) return nullptr;

    return fb;
#endif
}

void DiskNodeCache::put(const NodeCacheKey& key, const Framebuffer& fb) {
    CHRONON_ZONE_C("disk_cache_put", trace_category::kPipeline);
    
    std::error_code ec;
    std::filesystem::create_directories(m_cache_dir, ec);

    const auto path = get_file_path(m_cache_dir, key);
    const auto temp_path = path.string() + ".tmp";

    DiskHeader header;
    header.width = fb.width();
    header.height = fb.height();
    header.origin_x = fb.origin_x();
    header.origin_y = fb.origin_y();
    header.is_opaque = fb.is_opaque() ? 1 : 0;
    header.payload_size = static_cast<uint64_t>(fb.width()) * fb.height() * sizeof(Color);

    std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
    if (!file) return;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(fb.data()), header.payload_size);
    file.close();

    std::filesystem::rename(temp_path, path, ec);
}

bool DiskNodeCache::exists(const NodeCacheKey& key) const {
    return std::filesystem::exists(get_file_path(m_cache_dir, key));
}

void DiskNodeCache::clear() {
    std::error_code ec;
    std::filesystem::remove_all(m_cache_dir, ec);
    std::filesystem::create_directories(m_cache_dir, ec);
}

} // namespace chronon3d::cache
