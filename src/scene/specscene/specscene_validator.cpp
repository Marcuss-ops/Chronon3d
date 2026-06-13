#include <chronon3d/specscene/validation/specscene_validator.hpp>
#include <fmt/format.h>
#include <cstdint>
#include <unordered_set>

namespace chronon3d::specscene {

// ── Configurable resource limits ────────────────────────────────────────────
// These prevent unbounded memory allocation from malicious or malformed input.
struct SpecSceneLimits {
    static constexpr i32 max_dimension      = 16384;    // max width or height in pixels
    static constexpr i32 max_fps            = 240;       // max frame rate
    static constexpr Frame max_duration     = Frame{108000}; // ~1 hour at 30fps
    static constexpr size_t max_layers      = 1024;      // max layers per scene
    static constexpr size_t max_sub_comps   = 64;        // max sub-compositions
    static constexpr int64_t max_pixels     = 68'719'476'736LL; // 2^36 — ~8K×2M
};

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

    // Dimension upper bounds
    if (valid && scene.width > SpecSceneLimits::max_dimension) {
        diagnostics.push_back(fmt::format(
            "Scene width {} exceeds max {}", scene.width, SpecSceneLimits::max_dimension));
        valid = false;
    }
    if (valid && scene.height > SpecSceneLimits::max_dimension) {
        diagnostics.push_back(fmt::format(
            "Scene height {} exceeds max {}", scene.height, SpecSceneLimits::max_dimension));
        valid = false;
    }

    // Overflow-safe pixel count check (prevents allocation of unbounded memory)
    if (valid && scene.width > 0 && scene.height > 0) {
        const int64_t pixels = static_cast<int64_t>(scene.width) * static_cast<int64_t>(scene.height);
        if (pixels > SpecSceneLimits::max_pixels) {
            diagnostics.push_back(fmt::format(
                "Total pixels {} ({}x{}) exceeds limit {}",
                pixels, scene.width, scene.height, SpecSceneLimits::max_pixels));
            valid = false;
        }
    }

    // Frame rate must be > 0 (FrameRate is a rational: numerator/denominator)
    if (scene.frame_rate.numerator <= 0 || scene.frame_rate.denominator <= 0) {
        diagnostics.push_back(
            fmt::format("Invalid frame rate: {}/{}",
                        scene.frame_rate.numerator,
                        scene.frame_rate.denominator));
        valid = false;
    }
    if (valid) {
        const float effective_fps = static_cast<float>(scene.frame_rate.numerator)
                                  / static_cast<float>(scene.frame_rate.denominator);
        if (effective_fps > SpecSceneLimits::max_fps) {
            diagnostics.push_back(fmt::format(
                "Frame rate {} exceeds max {}", effective_fps, SpecSceneLimits::max_fps));
            valid = false;
        }
    }

    // Duration must be >= 0 and within limits
    if (scene.duration < 0) {
        diagnostics.push_back(fmt::format("Invalid duration: {}", scene.duration));
        valid = false;
    }
    if (valid && scene.duration > SpecSceneLimits::max_duration) {
        diagnostics.push_back(fmt::format(
            "Duration {} exceeds max {}", static_cast<int>(scene.duration),
            static_cast<int>(SpecSceneLimits::max_duration)));
        valid = false;
    }

    // Layer count limit
    if (scene.layers.size() > SpecSceneLimits::max_layers) {
        diagnostics.push_back(fmt::format(
            "Layer count {} exceeds max {}", scene.layers.size(), SpecSceneLimits::max_layers));
        valid = false;
    }

    // Sub-composition count limit
    if (scene.sub_compositions.size() > SpecSceneLimits::max_sub_comps) {
        diagnostics.push_back(fmt::format(
            "Sub-composition count {} exceeds max {}",
            scene.sub_compositions.size(), SpecSceneLimits::max_sub_comps));
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
    // and check for cycles via iterative depth tracking.
    for (const auto& layer : scene.layers) {
        if (layer.parent.has_value() &&
            layer_names.find(layer.parent.value()) == layer_names.end()) {
            diagnostics.push_back(
                fmt::format("Layer '{}' references missing parent '{}'",
                            layer.name, layer.parent.value()));
            valid = false;
        }
    }

    // Cycle detection: walk parent chains and reject if depth > layer count.
    if (valid) {
        for (const auto& layer : scene.layers) {
            if (!layer.parent.has_value()) continue;
            std::unordered_set<std::string> visited;
            visited.insert(layer.name);
            std::string current = layer.parent.value();
            for (size_t depth = 0; depth < scene.layers.size(); ++depth) {
                if (visited.count(current)) {
                    diagnostics.push_back(fmt::format(
                        "Parent cycle detected involving layer '{}' and '{}'"
                        , layer.name, current));
                    valid = false;
                    break;
                }
                visited.insert(current);
                // Find the parent's parent
                auto it = std::find_if(scene.layers.begin(), scene.layers.end(),
                    [&](const auto& l) { return l.name == current; });
                if (it == scene.layers.end() || !it->parent.has_value()) break;
                current = it->parent.value();
            }
        }
    }

    return valid;
}

} // namespace chronon3d::specscene
