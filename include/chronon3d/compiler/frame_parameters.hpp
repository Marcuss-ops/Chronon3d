#pragma once

#include <chronon3d/compiler/compiled_node.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::compiler {

/// Frame-local values produced by the compiler IR sampler.  This is deliberately
/// CPU-only data: backend resources are resolved later by graph materialization.
struct SampledLayerParameters {
    NodeId id{0};
    NodeKind kind{NodeKind::Unknown};
    Mat4 matrix{1.0f};
    float opacity{1.0f};
    bool active{false};
    std::uint32_t text_index{0};
};

/// Stable, typed output of sampling one immutable CompiledProgram.
struct FrameParameterBlock {
    Frame frame{0};
    std::vector<SampledLayerParameters> layers;
};

}  // namespace chronon3d::compiler
