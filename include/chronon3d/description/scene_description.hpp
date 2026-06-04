#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/description/layer_desc.hpp>
#include <chronon3d/description/camera_desc.hpp>
#include <optional>
#include <string>
#include <vector>
#include <memory>

namespace chronon3d {

// Forward declarations for precomp support
class CompositionRegistry;
struct SceneDescription;

// ── SubComposition (AE-6) ──────────────────────────────────────────────────
// An inline composition that can be referenced by precomp layers.
// Stored alongside the parent SceneDescription and converted to a
// CompositionRegistry entry at evaluation time.
// Uses unique_ptr to break the circular dependency: SceneDescription
// contains vector<SubComposition> and SubComposition contains SceneDescription.
// Constructors are declared here and defined after SceneDescription.
struct SubComposition {
    std::string                           name;   // unique identifier, referenced by precomp layers
    std::unique_ptr<SceneDescription>     scene;  // the nested scene definition

    SubComposition();
    SubComposition(const SubComposition& other);
    SubComposition& operator=(const SubComposition& other);
    SubComposition(SubComposition&&) noexcept = default;
    SubComposition& operator=(SubComposition&&) noexcept = default;
};

// Top-level scene description: declares what exists in the scene.
// Evaluation against a specific frame is done by TimelineEvaluator.
// The legacy SceneFunction (FrameContext -> Scene) coexists with this.
struct SceneDescription {
    std::string name;
    i32         width{1280};
    i32         height{720};
    FrameRate   frame_rate{30, 1};
    Frame       duration{0};

    std::vector<LayerDesc>              layers;
    std::optional<Camera2_5DAuthoring>   camera;

    // ── Precomp (AE-6): inline sub-compositions ──
    std::vector<SubComposition> sub_compositions;
};

// ── SubComposition constructor definitions (after SceneDescription is complete) ──
inline SubComposition::SubComposition()
    : scene(std::make_unique<SceneDescription>()) {}

inline SubComposition::SubComposition(const SubComposition& other)
    : name(other.name), scene(std::make_unique<SceneDescription>(*other.scene)) {}

inline SubComposition& SubComposition::operator=(const SubComposition& other) {
    if (this != &other) {
        name = other.name;
        scene = std::make_unique<SceneDescription>(*other.scene);
    }
    return *this;
}

// Build a CompositionRegistry from the inline sub-compositions in a SceneDescription.
// External compositions registered elsewhere are merged in via the optional parameter.
// Implementation in src/description/sub_composition_registry.cpp
[[nodiscard]] CompositionRegistry build_sub_composition_registry(
    const SceneDescription& desc,
    const CompositionRegistry* external = nullptr);

} // namespace chronon3d
