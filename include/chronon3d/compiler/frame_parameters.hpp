#pragma once

#include <chronon3d/compiler/compiled_node.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <array>
#include <cstdint>
#include <variant>
#include <string>
#include <vector>

namespace chronon3d::compiler {

/// CPU-only payloads sampled for the corresponding render-node kind.  These
/// deliberately contain values, not Scene/RenderNode objects or backend
/// handles: graph execution can consume them without rebuilding authoring
/// state on the frame path.
struct SampledColorParameters {
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct SampledImageParameters {
    std::string asset;
    std::array<float, 2> size{0.0f, 0.0f};
    float radius{0.0f};
};

struct SampledVideoParameters {
    std::string source;
    std::array<float, 2> size{0.0f, 0.0f};
    bool loop{false};
};

struct SampledTextParameters {
    std::uint32_t text_index{0};
    std::uint32_t font_index{0};
};

struct SampledShapeParameters {
    std::array<float, 4> fill{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> stroke{0.0f, 0.0f, 0.0f, 1.0f};
    float stroke_width{0.0f};
    float radius{0.0f};
    bool stroke_enabled{false};
};

struct SampledEffectParameters {
    // The effect table is intentionally typed even when a compiler program
    // has no effect entries yet.  Future effect lowering extends this value
    // variant instead of reintroducing Scene reads in the executor.
    std::vector<float> values;
};

using SampledLayerPayload = std::variant<
    std::monostate,
    SampledColorParameters,
    SampledImageParameters,
    SampledVideoParameters,
    SampledTextParameters,
    SampledShapeParameters,
    SampledEffectParameters>;

/// Frame-local values produced by the compiler IR sampler. This is
/// deliberately CPU-only data: backend resources are resolved later by graph
/// materialization.
struct SampledLayerParameters {
    NodeId id{0};
    NodeKind kind{NodeKind::Unknown};
    Mat4 matrix{1.0f};
    float opacity{1.0f};
    bool active{false};
    std::uint32_t text_index{0};
    SampledLayerPayload payload{};
};

/// Stable, typed output of sampling one immutable CompiledProgram.
struct FrameParameterBlock {
    Frame frame{0};
    std::vector<SampledLayerParameters> layers;
};

[[nodiscard]] inline const SampledLayerParameters* find_layer(
    const FrameParameterBlock& block, NodeId id) noexcept {
    for (const auto& layer : block.layers) {
        if (layer.id == id) return &layer;
    }
    return nullptr;
}

}  // namespace chronon3d::compiler
