#pragma once

#include <chronon3d/compiler/compiled_node.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <array>
#include <cstdint>
#include <variant>
#include <string_view>
#include <vector>

namespace chronon3d::compiler {

/// CPU-only payloads sampled for the corresponding render-node kind.  These
/// deliberately contain values, not Scene/RenderNode objects or backend
/// handles: graph execution can consume them without rebuilding authoring
/// state on the frame path.
///
/// Every alternative below has a producer in TypedProgramSampler. A shape or
/// effect payload is added here only together with the lowering that samples
/// it — an accepted-but-never-constructed alternative inflates the hottest
/// per-layer variant (and a heap member inside it would be a per-layer-per-
/// frame allocation trap).
struct SampledColorParameters {
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct SampledImageParameters {
    // View into the immutable CompiledProgram. Frame payloads never own a
    // copy of static asset identifiers.
    std::string_view asset;
    std::array<float, 2> size{0.0f, 0.0f};
    float radius{0.0f};
};

struct SampledVideoParameters {
    std::string_view source;
    std::array<float, 2> size{0.0f, 0.0f};
    bool loop{false};
};

struct SampledTextParameters {
    std::uint32_t text_index{0};
};

struct SampledShapeParameters {
    std::array<float, 2> size{0.0f, 0.0f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    float opacity{1.0f};
};

using SampledLayerPayload = std::variant<
    std::monostate,
    SampledColorParameters,
    SampledImageParameters,
    SampledVideoParameters,
    SampledTextParameters,
    SampledShapeParameters>;

/// Frame-local values produced by the compiler IR sampler. This is
/// deliberately CPU-only data: backend resources are resolved later by graph
/// materialization.
struct SampledElementParameters {
    std::uint64_t id{0};
    Vec3 position{0.0f};
    Vec3 rotation{0.0f};
    Vec3 scale{1.0f};
    float opacity{1.0f};
    float blur{0.0f};
    bool active{true};
};

struct SampledLayerParameters {
    NodeId id{0};
    NodeKind kind{NodeKind::Unknown};
    Mat4 matrix{1.0f};
    float opacity{1.0f};
    bool active{false};
    double local_time_frame{0.0};
    std::uint32_t text_index{0};
    SampledLayerPayload payload{};
    std::vector<SampledElementParameters> elements;
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
