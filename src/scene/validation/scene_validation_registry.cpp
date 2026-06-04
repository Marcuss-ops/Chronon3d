#include <chronon3d/scene/validation/scene_validation_registry.hpp>
#include <chronon3d/scene/scene.hpp>

#include <stdexcept>

namespace chronon3d {

SceneValidationRegistry& SceneValidationRegistry::instance() {
    static SceneValidationRegistry s_instance;
    return s_instance;
}

SceneValidationRegistry::SceneValidationRegistry() = default;
SceneValidationRegistry::~SceneValidationRegistry() = default;

void SceneValidationRegistry::register_rule(std::unique_ptr<SceneValidationRule> rule) {
    if (!rule) {
        throw std::invalid_argument("SceneValidationRegistry::register_rule: null rule");
    }
    auto id = std::string(rule->id());
    auto [it, inserted] = m_rules.try_emplace(id, std::move(rule));
    if (!inserted) {
        throw std::invalid_argument(
            "SceneValidationRegistry::register_rule: duplicate id '" + id + "'");
    }
}

bool SceneValidationRegistry::contains(std::string_view id) const {
    return m_rules.find(id) != m_rules.end();
}

std::vector<std::string> SceneValidationRegistry::available() const {
    std::vector<std::string> ids;
    ids.reserve(m_rules.size());
    for (const auto& [id, _] : m_rules) {
        ids.push_back(id);
    }
    return ids;
}

void SceneValidationRegistry::run_all(const Scene& scene,
                                       std::vector<ValidationDiagnostic>& diagnostics) const {
    for (const auto& [_, rule] : m_rules) {
        rule->check(scene, diagnostics);
    }
}

void SceneValidationRegistry::register_builtins() {
    // Built-in rules are handled directly by SceneValidator::validate().
    // This method exists for future extension-built rules.
}

} // namespace chronon3d
