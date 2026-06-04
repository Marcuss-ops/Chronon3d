#pragma once

#include <chronon3d/scene/builders/layer_command.hpp>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

/// Domain-specific registry for `LayerCommand` instances.
///
/// Features and modules can register layer commands (motion presets,
/// content generators, layout helpers, etc.) without modifying
/// `LayerBuilder` directly.
///
/// Singleton — use `LayerCommandRegistry::instance()`.
class LayerCommandRegistry {
public:
    static LayerCommandRegistry& instance();

    LayerCommandRegistry();
    ~LayerCommandRegistry();

    /// Register a command.  The `id` must be unique.
    void register_command(std::unique_ptr<LayerCommand> cmd);

    /// Check if a command with the given id is registered.
    [[nodiscard]] bool contains(std::string_view id) const;

    /// Get a command by id (throws if not found).
    [[nodiscard]] const LayerCommand& get(std::string_view id) const;

    /// Get all registered command ids.
    [[nodiscard]] std::vector<std::string> available() const;

    /// Get all command ids in a given category.
    [[nodiscard]] std::vector<std::string> available_in_category(std::string_view category) const;

    /// Apply a registered command to the given builder.
    /// Throws if the command id is not found.
    void apply(std::string_view id, LayerBuilder& builder) const;

    /// Register the built-in motion preset commands.
    void register_builtins();

private:
    std::map<std::string, std::unique_ptr<LayerCommand>, std::less<>> m_commands;
};

} // namespace chronon3d
