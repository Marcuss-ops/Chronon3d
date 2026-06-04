#include <chronon3d/specscene/validation/specscene_validator.hpp>
#include <fmt/format.h>
#include <unordered_set>

namespace chronon3d::specscene {

bool validate_document(
    const SpecSceneDocument& doc,
    std::vector<std::string>& diagnostics)
{
    bool valid = true;
    auto& scene = doc.scene;

    // Scene dimensions must be positive
    if (scene.width <= 0) {
        diagnostics.push_back(fmt::format("Invalid scene width: {}", scene.width));
        valid = false;
    }
    if (scene.height <= 0) {
        diagnostics.push_back(fmt::format("Invalid scene height: {}", scene.height));
        valid = false;
    }

    // Frame rate must be > 0 (FrameRate is a rational: numerator/denominator)
    if (scene.frame_rate.numerator <= 0 || scene.frame_rate.denominator <= 0) {
        diagnostics.push_back(
            fmt::format("Invalid frame rate: {}/{}",
                        scene.frame_rate.numerator,
                        scene.frame_rate.denominator));
        valid = false;
    }

    // Duration must be >= 0
    if (scene.duration < 0) {
        diagnostics.push_back(fmt::format("Invalid duration: {}", scene.duration));
        valid = false;
    }

    // Check for duplicate layer names
    for (size_t i = 0; i < scene.layers.size(); ++i) {
        for (size_t j = i + 1; j < scene.layers.size(); ++j) {
            if (scene.layers[i].name == scene.layers[j].name) {
                diagnostics.push_back(
                    fmt::format("Duplicate layer name '{}' at layers {} and {}",
                                scene.layers[i].name, i, j));
                valid = false;
            }
        }
    }

    // Collect all layer names for reference validation
    std::unordered_set<std::string> layer_names;
    for (const auto& layer : scene.layers) {
        layer_names.insert(layer.name);
    }

    // Validate parent references (LayerDesc::parent is an optional string)
    for (const auto& layer : scene.layers) {
        if (layer.parent.has_value() &&
            layer_names.find(layer.parent.value()) == layer_names.end()) {
            diagnostics.push_back(
                fmt::format("Layer '{}' references missing parent '{}'",
                            layer.name, layer.parent.value()));
            valid = false;
        }
    }

    return valid;
}

} // namespace chronon3d::specscene
