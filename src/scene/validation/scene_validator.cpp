#include <chronon3d/scene/validation/scene_validator.hpp>
#include <chronon3d/scene/validation/scene_validation_registry.hpp>
#include <chronon3d/scene/model/scene.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_set>

namespace chronon3d {

// ── ValidationResult ──────────────────────────────────────────────────

bool ValidationResult::has_errors() const {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const auto& d) { return d.severity == ValidationSeverity::Error; });
}

bool ValidationResult::has_warnings() const {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const auto& d) { return d.severity == ValidationSeverity::Warning; });
}

std::vector<ValidationDiagnostic> ValidationResult::errors() const {
    std::vector<ValidationDiagnostic> out;
    for (const auto& d : diagnostics) {
        if (d.severity == ValidationSeverity::Error) out.push_back(d);
    }
    return out;
}

std::vector<ValidationDiagnostic> ValidationResult::warnings() const {
    std::vector<ValidationDiagnostic> out;
    for (const auto& d : diagnostics) {
        if (d.severity == ValidationSeverity::Warning) out.push_back(d);
    }
    return out;
}

std::string ValidationResult::to_text() const {
    std::ostringstream ss;
    for (const auto& d : diagnostics) {
        switch (d.severity) {
            case ValidationSeverity::Error:   ss << "[ERROR]   "; break;
            case ValidationSeverity::Warning: ss << "[WARNING] "; break;
            case ValidationSeverity::Info:    ss << "[INFO]    "; break;
        }
        ss << d.rule_id;
        if (!d.context.empty()) ss << " (" << d.context << ")";
        ss << ": " << d.message << "\n";
    }
    return ss.str();
}

// ── Built-in validation rules ─────────────────────────────────────────

namespace {

void check_duplicate_layer_names(const Scene& scene,
                                 std::vector<ValidationDiagnostic>& diagnostics) {
    std::unordered_set<std::string> seen;
    for (const auto& layer : scene.layers()) {
        std::string name(layer.name);
        if (!seen.insert(name).second) {
            diagnostics.push_back({
                ValidationSeverity::Error,
                "layer.duplicate_name",
                "Duplicate layer name: every layer must have a unique name",
                name,
            });
        }
    }
}

void check_missing_parent_references(const Scene& scene,
                                     std::vector<ValidationDiagnostic>& diagnostics) {
    std::unordered_set<std::string> layer_names;
    for (const auto& layer : scene.layers()) {
        layer_names.insert(std::string(layer.name));
    }
    for (const auto& layer : scene.layers()) {
        std::string parent(layer.parent_name);
        if (!parent.empty() && layer_names.find(parent) == layer_names.end()) {
            diagnostics.push_back({
                ValidationSeverity::Warning,
                "layer.missing_parent",
                "Layer references non-existent parent",
                std::string(layer.name),
            });
        }
    }
}

void check_zero_duration_layers(const Scene& scene,
                                std::vector<ValidationDiagnostic>& diagnostics) {
    for (const auto& layer : scene.layers()) {
        if (layer.duration == 0 && layer.kind != LayerKind::Null) {
            diagnostics.push_back({
                ValidationSeverity::Warning,
                "layer.zero_duration",
                "Layer has zero duration and will never be visible",
                std::string(layer.name),
            });
        }
    }
}

void check_track_matte_sources(const Scene& scene,
                               std::vector<ValidationDiagnostic>& diagnostics) {
    std::unordered_set<std::string> layer_names;
    for (const auto& layer : scene.layers()) {
        layer_names.insert(std::string(layer.name));
    }
    for (const auto& layer : scene.layers()) {
        if (layer.track_matte.active()) {
            std::string src(layer.track_matte.source_layer);
            if (layer_names.find(src) == layer_names.end()) {
                diagnostics.push_back({
                    ValidationSeverity::Error,
                    "layer.track_matte_missing_source",
                    "Track matte references non-existent source layer",
                    std::string(layer.name),
                });
            }
        }
    }
}

void check_camera_target(const Scene& scene,
                         std::vector<ValidationDiagnostic>& diagnostics) {
    const auto& cam = scene.camera_2_5d();
    if (!cam.enabled) return;

    if (!cam.target_name.empty()) {
        std::unordered_set<std::string> layer_names;
        for (const auto& layer : scene.layers()) {
            layer_names.insert(std::string(layer.name));
        }
        if (layer_names.find(std::string(cam.target_name)) == layer_names.end()) {
            diagnostics.push_back({
                ValidationSeverity::Warning,
                "camera.missing_target",
                "Camera target references non-existent layer",
                std::string(cam.target_name),
            });
        }
    }
}

void check_circular_parent_hierarchy(const Scene& scene,
                                     std::vector<ValidationDiagnostic>& diagnostics) {
    for (const auto& layer : scene.layers()) {
        std::string current(layer.parent_name);
        std::unordered_set<std::string> visited;
        visited.insert(std::string(layer.name));
        bool cycle = false;
        while (!current.empty()) {
            if (visited.count(current)) {
                cycle = true;
                break;
            }
            visited.insert(current);
            // Find the parent layer
            bool found = false;
            for (const auto& l : scene.layers()) {
                if (std::string(l.name) == current) {
                    current = std::string(l.parent_name);
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
        if (cycle) {
            diagnostics.push_back({
                ValidationSeverity::Error,
                "layer.circular_parent",
                "Circular parent hierarchy detected",
                std::string(layer.name),
            });
        }
    }
}

} // anonymous namespace

// ── SceneValidator ────────────────────────────────────────────────────

ValidationResult SceneValidator::validate(const Scene& scene) const {
    std::vector<ValidationDiagnostic> diagnostics;

    // Run built-in rules.
    check_duplicate_layer_names(scene, diagnostics);
    check_missing_parent_references(scene, diagnostics);
    check_zero_duration_layers(scene, diagnostics);
    check_track_matte_sources(scene, diagnostics);
    check_camera_target(scene, diagnostics);
    check_circular_parent_hierarchy(scene, diagnostics);

    // Run registered extension rules.
    SceneValidationRegistry::instance().run_all(scene, diagnostics);

    return ValidationResult{std::move(diagnostics)};
}

} // namespace chronon3d
