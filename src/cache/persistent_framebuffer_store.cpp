#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <cstring>
#include <sstream>

#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace chronon3d::cache {

PersistentFramebufferStore& PersistentFramebufferStore::instance() {
    static PersistentFramebufferStore s_instance;
    return s_instance;
}

bool PersistentFramebufferStore::enabled_for_current_run() {
#ifdef CHRONON_BUILD_TESTS
    return false;
#else
    return !Config::get().disable_persistent_framebuffer_cache;
#endif
}

PersistentFramebufferStore::PersistentFramebufferStore() {
    auto& dir = Config::get().persistent_framebuffer_cache_dir;
    if (!dir.empty()) {
        m_cache_dir = dir;
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

std::filesystem::path PersistentFramebufferStore::file_path(const NodeCacheKey& key) const {
    std::ostringstream oss;
    oss << std::hex << key.digest() << ".cfb3";
    return m_cache_dir / oss.str();
}

std::shared_ptr<Framebuffer> PersistentFramebufferStore::get(const NodeCacheKey& key) {
    CHRONON_ZONE_C("persistent_fb_get", trace_category::kPipeline);

    if (!enabled_for_current_run()) {
        return nullptr;
    }

    const auto path = file_path(key);
    if (!std::filesystem::exists(path)) {
        return nullptr;
    }

#ifdef __linux__
    int fd = ::open(path.string().c_str(), O_RDONLY);
    if (fd == -1) return nullptr;

    struct stat st{};
    if (::fstat(fd, &st) == -1) {
        ::close(fd);
        return nullptr;
    }
    if (st.st_size < static_cast<off_t>(sizeof(Framebuffer3Header))) {
        ::close(fd);
        return nullptr;
    }

    void* mapped = ::mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) return nullptr;

    const auto* header = static_cast<const Framebuffer3Header*>(mapped);
    if (std::strncmp(header->magic, "CFB3", 4) != 0 ||
        header->payload_size !=
            static_cast<uint64_t>(header->width) * header->height * sizeof(Color)) {
        ::munmap(mapped, st.st_size);
        return nullptr;
    }

    auto fb = std::make_shared<Framebuffer>(header->width, header->height);
    fb->set_origin(header->origin_x, header->origin_y);
    if (header->is_opaque) fb->set_opaque(true);

    const auto* payload = static_cast<const uint8_t*>(mapped) + sizeof(Framebuffer3Header);
    if (fb->allocated_width() == fb->width()) {
        std::memcpy(fb->data(), payload, header->payload_size);
    } else {
        for (i32 y = 0; y < header->height; ++y) {
            const auto* src_row =
                payload + static_cast<std::size_t>(y) * header->width * sizeof(Color);
            std::memcpy(fb->pixels_row(y), src_row,
                       static_cast<std::size_t>(header->width) * sizeof(Color));
        }
    }

    ::munmap(mapped, st.st_size);
    return fb;
#else
    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;

    Framebuffer3Header header{};
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) return nullptr;
    if (std::strncmp(header.magic, "CFB3", 4) != 0) return nullptr;

    auto fb = std::make_shared<Framebuffer>(header.width, header.height);
    fb->set_origin(header.origin_x, header.origin_y);
    if (header.is_opaque) fb->set_opaque(true);

    if (fb->allocated_width() == fb->width()) {
        if (!file.read(reinterpret_cast<char*>(fb->data()), header.payload_size)) return nullptr;
    } else {
        for (i32 y = 0; y < header.height; ++y) {
            if (!file.read(reinterpret_cast<char*>(fb->pixels_row(y)),
                           static_cast<std::size_t>(header.width) * sizeof(Color))) {
                return nullptr;
            }
        }
    }
    return fb;
#endif
}

void PersistentFramebufferStore::put(const NodeCacheKey& key, const Framebuffer& fb) {
    CHRONON_ZONE_C("persistent_fb_put", trace_category::kPipeline);

    if (!enabled_for_current_run()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(m_cache_dir, ec);

    const auto path = file_path(key);
    const auto temp_path = path.string() + ".tmp";

    Framebuffer3Header header{};
    header.width        = fb.width();
    header.height       = fb.height();
    header.origin_x     = fb.origin_x();
    header.origin_y     = fb.origin_y();
    header.is_opaque    = fb.is_opaque() ? 1u : 0u;
    header.payload_size =
        static_cast<uint64_t>(fb.width()) * fb.height() * sizeof(Color);

    std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
    if (!file) return;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (fb.allocated_width() == fb.width()) {
        file.write(reinterpret_cast<const char*>(fb.data()), header.payload_size);
    } else {
        for (i32 y = 0; y < fb.height(); ++y) {
            file.write(reinterpret_cast<const char*>(fb.pixels_row(y)),
                       static_cast<std::size_t>(fb.width()) * sizeof(Color));
        }
    }
    file.close();

    std::filesystem::rename(temp_path, path, ec);
}

void PersistentFramebufferStore::put_batch(
    const std::vector<std::pair<NodeCacheKey, std::shared_ptr<Framebuffer>>>& entries) {
    for (const auto& [key, fb] : entries) {
        if (fb) put(key, *fb);
    }
}

bool PersistentFramebufferStore::exists(const NodeCacheKey& key) const {
    if (!enabled_for_current_run()) {
        return false;
    }
    return std::filesystem::exists(file_path(key));
}

void PersistentFramebufferStore::clear() {
    std::error_code ec;
    std::filesystem::remove_all(m_cache_dir, ec);
    std::filesystem::create_directories(m_cache_dir, ec);
}

std::size_t PersistentFramebufferStore::entry_count() const {
    std::size_t count = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_cache_dir, ec)) {
        if (entry.path().extension() == ".cfb3") {
            ++count;
        }
    }
    return count;
}

std::size_t PersistentFramebufferStore::total_bytes() const {
    std::size_t total = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_cache_dir, ec)) {
        if (entry.path().extension() == ".cfb3") {
            total += static_cast<std::size_t>(entry.file_size(ec));
        }
    }
    return total;
}

} // namespace chronon3d::cache
