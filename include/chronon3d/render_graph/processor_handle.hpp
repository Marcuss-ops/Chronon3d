#pragma once

// Public processor-handle value types.  These are the stable, public-facing
// identifiers carried by RenderState / compiled frame graphs to reference a
// shape or effect processor captured in a renderer::ProcessorRegistrySnapshot.
//
// The snapshot itself stays internal (internal/render_graph/...); only these
// small PODs are exposed so public headers (e.g. math/transform.hpp) can hold
// a handle BY VALUE without depending on the internal/ tree — which is not
// installed with the SDK.

#include <cstdint>

// Forward-declare graph types needed by CompileNodeContext / CompileRecorderFn.
// The full definitions live in compiled_frame_graph.hpp.
namespace chronon3d::graph {
struct CompiledOperation;
struct CompiledNodeInfo;
} // namespace chronon3d::graph

namespace chronon3d::renderer {

struct ShapeProcessorHandle {
    std::uint32_t index{invalid_index()};

    [[nodiscard]] static constexpr std::uint32_t invalid_index() noexcept {
        return static_cast<std::uint32_t>(-1);
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != invalid_index();
    }

    friend constexpr bool operator==(ShapeProcessorHandle,
                                     ShapeProcessorHandle) = default;
};

struct EffectProcessorHandle {
    std::uint32_t index{invalid_index()};

    [[nodiscard]] static constexpr std::uint32_t invalid_index() noexcept {
        return static_cast<std::uint32_t>(-1);
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != invalid_index();
    }

    friend constexpr bool operator==(EffectProcessorHandle,
                                     EffectProcessorHandle) = default;
};

struct ProcessorCapabilities {
    bool gpu : 1 {false};
    bool in_place : 1 {false};
    bool fusible : 1 {false};
    bool pixel_local : 1 {false};
    bool native_surface_input : 1 {false};
    bool native_surface_output : 1 {false};

    [[nodiscard]] constexpr bool is_gpu_fusible() const noexcept {
        return gpu && fusible && pixel_local;
    }

    friend constexpr bool operator==(ProcessorCapabilities,
                                     ProcessorCapabilities) = default;
};

// ── Compiled recording infrastructure ────────────────────────────────────
//
// A CompileRecorderFn is a function that, given compile-time node context,
// produces an executable CompiledOperation.  Processors that provide a
// recorder enable the fully-compiled execution path; processors without
// one fall back to node.execute().

/// Context passed by the compiler to a processor's compile_recorder.
/// Contains everything the recorder needs to produce a CompiledOperation.
struct CompileNodeContext {
    const graph::CompiledNodeInfo* node_info{nullptr};
    const std::uint32_t* input_ids{nullptr};
    std::uint32_t input_count{0};
    std::uint32_t output_physical_slot{0};
};

/// A function that compiles a node into an executable CompiledOperation.
/// Returns a default-constructed CompiledOperation (with node == k_invalid_node)
/// when the node cannot be compiled.
using CompileRecorderFn = graph::CompiledOperation (*)(const CompileNodeContext& ctx);

/// Canonical descriptor for a processor entry in the registry.  Bundles
/// capabilities with the optional compile-recorder function.  When
/// compile_recorder is non-null the processor participates in the
/// fully-compiled execution path.
struct ProcessorDescriptor {
    ProcessorCapabilities capabilities;
    CompileRecorderFn compile_recorder{nullptr};

    [[nodiscard]] constexpr bool has_compile_recorder() const noexcept {
        return compile_recorder != nullptr;
    }
};

} // namespace chronon3d::renderer