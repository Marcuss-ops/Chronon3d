#include <chronon3d/scene/builders/layer_command_registry.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/commands/motion_preset_commands.hpp>

#include <stdexcept>

namespace chronon3d {

LayerCommandRegistry& LayerCommandRegistry::instance() {
    static LayerCommandRegistry s_instance;
    return s_instance;
}

LayerCommandRegistry::LayerCommandRegistry() {
    register_builtins();
}
LayerCommandRegistry::~LayerCommandRegistry() = default;

void LayerCommandRegistry::register_command(std::unique_ptr<LayerCommand> cmd) {
    if (!cmd) {
        throw std::invalid_argument("LayerCommandRegistry::register_command: null command");
    }
    auto id = std::string(cmd->id());
    auto [it, inserted] = m_commands.try_emplace(id, std::move(cmd));
    if (!inserted) {
        throw std::invalid_argument(
            "LayerCommandRegistry::register_command: duplicate id '" + id + "'");
    }
}

bool LayerCommandRegistry::contains(std::string_view id) const {
    return m_commands.find(id) != m_commands.end();
}

const LayerCommand& LayerCommandRegistry::get(std::string_view id) const {
    auto it = m_commands.find(id);
    if (it == m_commands.end()) {
        throw std::out_of_range(
            std::string("LayerCommandRegistry::get: unknown id '") +
            std::string(id) + "'");
    }
    return *it->second;
}

std::vector<std::string> LayerCommandRegistry::available() const {
    std::vector<std::string> ids;
    ids.reserve(m_commands.size());
    for (const auto& [id, _] : m_commands) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> LayerCommandRegistry::available_in_category(std::string_view category) const {
    std::vector<std::string> ids;
    for (const auto& [id, cmd] : m_commands) {
        if (cmd->category() == category) {
            ids.push_back(id);
        }
    }
    return ids;
}

void LayerCommandRegistry::apply(std::string_view id, LayerBuilder& builder) const {
    const auto& cmd = get(id);
    cmd.apply(builder);
}

void LayerCommandRegistry::register_builtins() {
    // Idempotent: skip if motion presets already registered.
    if (contains("motion:slide_in")) return;

    register_motion_preset_commands(*this);
}

} // namespace chronon3d
