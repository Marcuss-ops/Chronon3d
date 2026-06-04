#pragma once

#include <string>
#include <vector>

namespace chronon3d {

class Scene;

/// Severity of a validation diagnostic.
enum class ValidationSeverity {
    Info,
    Warning,
    Error,
};

/// A single validation finding.
struct ValidationDiagnostic {
    ValidationSeverity severity;
    std::string        rule_id;    // e.g. "layer.duplicate_name", "camera.missing_target"
    std::string        message;
    std::string        context;    // optional: layer name, node name, etc.
};

/// Result of running the scene validator.
struct ValidationResult {
    std::vector<ValidationDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const;
    [[nodiscard]] bool has_warnings() const;
    [[nodiscard]] bool has_any() const { return !diagnostics.empty(); }

    /// Return only errors.
    [[nodiscard]] std::vector<ValidationDiagnostic> errors() const;

    /// Return only warnings.
    [[nodiscard]] std::vector<ValidationDiagnostic> warnings() const;

    /// Human-readable summary (one line per diagnostic).
    [[nodiscard]] std::string to_text() const;
};

/// Validates a Scene before it enters the graph build pipeline.
///
/// Built-in rules check for:
/// - Duplicate layer names
/// - Missing parent references
/// - Circular parent hierarchies
/// - Layers with zero duration
/// - Camera referencing non-existent targets
/// - Track matte source missing or inactive
/// - Precomp referencing non-existent composition
///
/// Additional rules can be registered via `SceneValidationRegistry`.
class SceneValidator {
public:
    SceneValidator() = default;

    /// Run all built-in validation rules on the given scene.
    [[nodiscard]] ValidationResult validate(const Scene& scene) const;
};

} // namespace chronon3d
