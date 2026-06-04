#pragma once

#include <chronon3d/extension/extension_module.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {

class ExtensionRegistry;

/// Describes a plugin that was loaded (or failed to load) from a shared library.
struct LoadedPlugin {
    std::filesystem::path path;      ///< Full path to the .so / .dll
    std::string           id;        ///< Module id from the descriptor (empty on failure)
    bool                  success{false};
    std::string           error;     ///< Error message on failure
};

/// Loads ExtensionModule shared libraries at runtime.
///
/// Plugin contract:
///   Each shared library must export a `ChrononModuleDescriptor` struct
///   via the `CHRONON_MODULE_EXPORT` macro:
///
///   ```cpp
///   struct ChrononModuleDescriptor {
///       std::uint32_t api_version;
///       const char*   id;
///       chronon3d::ExtensionModule* (*create)();
///   };
///
///   CHRONON_MODULE_EXPORT ChrononModuleDescriptor chronon3d_module = {
///       .api_version = CHRONON_MODULE_API_VERSION,
///       .id          = "my_plugin",
///       .create      = []() -> chronon3d::ExtensionModule* {
///           return new MyPluginModule();
///       },
///   };
///   ```
///
/// Lifetime: The loader takes ownership of the returned `ExtensionModule*`
/// and wraps it in a `std::unique_ptr`.  The shared library handle is kept
/// alive until the loader is destroyed or `unload_all()` is called.
class ExtensionLoader {
public:
    ExtensionLoader();
    ~ExtensionLoader();

    ExtensionLoader(const ExtensionLoader&) = delete;
    ExtensionLoader& operator=(const ExtensionLoader&) = delete;

    /// Load a single shared library and register its module.
    /// Returns true on success.
    bool load(const std::filesystem::path& path, ExtensionRegistry& registry);

    /// Load all shared libraries in a directory.
    /// Files with extensions .so, .dylib, or .dll are considered.
    /// Returns the number of successfully loaded plugins.
    std::size_t load_all(const std::filesystem::path& directory,
                         ExtensionRegistry& registry);

    /// Unload all previously loaded plugins and close their library handles.
    /// Modules remain registered in the ExtensionRegistry but may become
    /// dangling — use with care.
    void unload_all();

    /// Diagnostics for all load attempts (successes and failures).
    [[nodiscard]] const std::vector<LoadedPlugin>& diagnostics() const;

    /// Number of successfully loaded plugins.
    [[nodiscard]] std::size_t loaded_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace chronon3d
