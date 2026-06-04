#include <chronon3d/extension/extension_loader.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include <algorithm>
#include <stdexcept>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace chronon3d {

// ── Platform abstractions ────────────────────────────────────────────────────

namespace {

void* platform_load_library(const std::filesystem::path& path) {
#ifdef _WIN32
    return static_cast<void*>(LoadLibraryW(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* platform_get_symbol(void* handle, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(
        GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

std::string platform_error() {
#ifdef _WIN32
    DWORD err = GetLastError();
    if (err == 0) return "unknown error";
    char buf[256]{};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   buf, sizeof(buf), nullptr);
    return buf;
#else
    const char* msg = dlerror();
    return msg ? msg : "unknown error";
#endif
}

void platform_close_library(void* handle) {
    if (!handle) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

bool is_shared_library_ext(const std::filesystem::path& p) {
    const auto ext = p.extension().string();
#ifdef _WIN32
    return ext == ".dll";
#elif defined(__APPLE__)
    return ext == ".dylib" || ext == ".so";
#else
    return ext == ".so";
#endif
}

} // anonymous namespace

// ── Plugin descriptor (C ABI) ────────────────────────────────────────────────

/// C-compatible descriptor struct that each plugin shared library must export.
struct ChrononModuleDescriptor {
    std::uint32_t api_version;
    const char*   id;
    ExtensionModule* (*create)();
};

static constexpr const char* k_descriptor_symbol = "chronon3d_module";

// ── ExtensionLoader::Impl ────────────────────────────────────────────────────

struct ExtensionLoader::Impl {
    struct LoadedEntry {
        void* handle{nullptr};      ///< dlopen / LoadLibrary handle
        std::filesystem::path path;
        std::string id;
    };

    std::vector<LoadedEntry> entries;
    std::vector<LoadedPlugin> diag;

    ~Impl() {
        // Close all handles in reverse order.
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            platform_close_library(it->handle);
        }
    }
};

// ── ExtensionLoader public API ───────────────────────────────────────────────

ExtensionLoader::ExtensionLoader()
    : m_impl(std::make_unique<Impl>())
{}

ExtensionLoader::~ExtensionLoader() = default;

bool ExtensionLoader::load(const std::filesystem::path& path,
                           ExtensionRegistry& registry) {
    LoadedPlugin result;
    result.path = path;

    if (!std::filesystem::exists(path)) {
        result.error = "File does not exist: " + path.string();
        m_impl->diag.push_back(std::move(result));
        return false;
    }

    // 1. Load the shared library.
    void* handle = platform_load_library(path);
    if (!handle) {
        result.error = "Failed to load library: " + platform_error();
        m_impl->diag.push_back(std::move(result));
        return false;
    }

    // 2. Look up the descriptor symbol.
    void* sym = platform_get_symbol(handle, k_descriptor_symbol);
    if (!sym) {
        result.error = "Symbol '" + std::string(k_descriptor_symbol) +
                       "' not found: " + platform_error();
        platform_close_library(handle);
        m_impl->diag.push_back(std::move(result));
        return false;
    }

    auto* desc = reinterpret_cast<ChrononModuleDescriptor*>(sym);

    // 3. Validate API version (major must match).
    if (desc->api_version != CHRONON_MODULE_API_VERSION) {
        result.error = "API version mismatch: plugin=" +
                       std::to_string(desc->api_version) +
                       " expected=" + std::to_string(CHRONON_MODULE_API_VERSION);
        platform_close_library(handle);
        m_impl->diag.push_back(std::move(result));
        return false;
    }

    if (!desc->id || !desc->create) {
        result.error = "Invalid descriptor: null id or create function";
        platform_close_library(handle);
        m_impl->diag.push_back(std::move(result));
        return false;
    }

    result.id = desc->id;

    // 4. Check for duplicate module id.
    if (registry.has_module(result.id)) {
        result.error = "Module '" + result.id + "' is already registered";
        platform_close_library(handle);
        m_impl->diag.push_back(std::move(result));
        return false;
    }

    // 5. Create the module and register it.
    ExtensionModule* raw = desc->create();
    if (!raw) {
        result.error = "create() returned null for module '" + result.id + "'";
        platform_close_library(handle);
        m_impl->diag.push_back(std::move(result));
        return false;
    }

    registry.register_module(std::unique_ptr<ExtensionModule>(raw));

    // 6. Initialize the new module immediately.
    registry.initialize_all();

    // 7. Store the handle for later cleanup.
    m_impl->entries.push_back({handle, path, result.id});
    result.success = true;
    m_impl->diag.push_back(std::move(result));
    return true;
}

std::size_t ExtensionLoader::load_all(const std::filesystem::path& directory,
                                      ExtensionRegistry& registry) {
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec) || ec) {
        return 0;
    }

    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file() && is_shared_library_ext(entry.path())) {
            if (load(entry.path(), registry)) {
                ++count;
            }
        }
    }
    return count;
}

void ExtensionLoader::unload_all() {
    for (auto& e : m_impl->entries) {
        platform_close_library(e.handle);
        e.handle = nullptr;
    }
    m_impl->entries.clear();
}

const std::vector<LoadedPlugin>& ExtensionLoader::diagnostics() const {
    return m_impl->diag;
}

std::size_t ExtensionLoader::loaded_count() const {
    return std::count_if(m_impl->entries.begin(), m_impl->entries.end(),
                         [](const auto& e) { return e.handle != nullptr; });
}

} // namespace chronon3d
