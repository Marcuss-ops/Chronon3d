#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/composition_definition.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/evaluated_composition_frame.hpp>

namespace chronon3d {

struct CompositionCompileContext {};

struct CompositionCompileError {
    enum class Kind : std::uint8_t {
        EmptyCompositionSpec = 0, NoSceneFunction = 1, CameraFailure = 2,
        NotImplementedYet = 3, Internal = 4,
    };
    Kind kind = Kind::Internal;
    std::string message{};
};

struct CompositionEvaluateContext { FrameContext frame_context{}; };

struct CompositionEvaluateError {
    enum class Kind : std::uint8_t {
        NullCompiledComposition = 0, NullDefinition = 1, NullSceneFunction = 2,
        SceneBuildFailed = 3, NotImplementedYet = 4, Internal = 5,
    };
    Kind kind = Kind::Internal;
    std::string message{};
};

Result<camera_v1::CameraProgram, CompositionCompileError>
compile_camera(const camera_v1::CameraDescriptor&, const CompositionCompileContext&);

/// Canonical compiler entry point: Composition is the only core input model.
Result<CompiledComposition, CompositionCompileError>
compile_composition(const Composition&, const CompositionCompileContext&);

/// Boundary compatibility overload. New code must construct Composition and
/// use the canonical overload above.
[[deprecated("use compile_composition(const Composition&, ...) instead")]]
Result<CompiledComposition, CompositionCompileError>
compile_composition(const CompositionDefinition&, const CompositionCompileContext&);

Result<EvaluatedCompositionFrame, CompositionEvaluateError>
evaluate(const CompiledComposition&, const CompositionEvaluateContext&, Frame);

Result<EvaluatedCompositionFrame, CompositionEvaluateError>
evaluate(const CompiledComposition&, const CompositionEvaluateContext&, SampleTime);

} // namespace chronon3d
