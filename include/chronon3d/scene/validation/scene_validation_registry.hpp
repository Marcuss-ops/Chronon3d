#pragma once

#include <chronon3d/scene/validation/scene_validator.hpp>

#include <functional>
#include <map>
#include <memory>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

class Scene;

/// A pluggable validation rule that can be registered into the
/// `SceneValidationRegistry`.  Each rule inspects a `Scene` and
/// appends any findings to the diagnostics vector.
class SceneValidationRule {
public:
    virtual ~SceneValidationRule() = default;

    /// Unique identifier for this rule (e.g. "layer.duplicate_name").
    [[nodiscard]] virtual std::string_view id() const = 0;

    /// Human-readable description of what this rule checks.
    [[nodiscard]] virtual std::string_view description() const = 0;

    /// Run this rule against the given scene, appending findings.
    virtual void check(const Scene& scene,
                       std::vector<ValidationDiagnostic>& diagnostics) const = 0;
};

/// Registry for pluggable scene validation rules.
///
/// Features and modules can register additional validation rules without
/// modifying `SceneValidator` directly.
///
/// Singleton — use `SceneValidationRegistry::instance()`.
class SceneValidationRegistry {
public:
    static SceneValidationRegistry& instance();

    SceneValidationRegistry();
    ~SceneValidationRegistry();

    /// Register a validation rule.  The `id` must be unique.
    void register_rule(std::unique_ptr<SceneValidationRule> rule);

    /// Check if a rule with the given id is registered.
    [[nodiscard]] bool contains(std::string_view id) const;

    /// Get all registered rule ids.
    [[nodiscard]] std::vector<std::string> available() const;

    /// Run all registered rules on the given scene, appending to diagnostics.
    void run_all(const Scene& scene,
                 std::vector<ValidationDiagnostic>& diagnostics) const;

    /// Register the built-in validation rules.
    void register_builtins();

private:
    std::map<std::string, std::unique_ptr<SceneValidationRule>, std::less<>> m_rules;
};

} // namespace chronon3d
