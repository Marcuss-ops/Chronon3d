#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <cstdint>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <functional>
#include <atomic>
#include <string>
#include <memory>
#include <vector>

// =============================================================================
// chronon3d/timeline/composition.hpp
//
// Composition header — P3-F post-migration state.
//
// Composition is now IMMUTABLE on the camera side after P3-F.  There is
// NO mutable cache, NO lazy compile, NO depent inverse-projection method.
// The Composition shape that survives is:
//
//   * CompositionSpec (the static name / width / height / frame_rate / duration).
//   * Composition class public API (`evaluate` + Scene fn).
//   * Composition's canonical authoring-time camera surface:
//     `default_camera_descriptor(CameraDescriptor)` setter,
//     `default_camera_descriptor()` const getter,
//     `has_default_camera_descriptor()` const probe.
//
// REMOVED in P3-F:
//   * `Composition::default_camera_program()`          — lazy compile cache.
//   * `Composition::invalidate_default_camera_program()` — cache reset.
//   * `Composition::redecompose_camera_from_descriptor(SampleTime)` — inverse
//     projection onto the removed mutable camera state.
//
// New V2 staging path (the canonical path going forward):
//   `CompositionDefinition` → `chronon3d::compile_composition(...)` →
//   `CompiledComposition` → `chronon3d::evaluate(...)` →
//   `EvaluatedCompositionFrame` (with `Camera2_5D` populated from the
//   compiled program).  See
//   `<chronon3d/timeline/compile_evaluate.hpp>` for the entry points.
//
// The header includes `camera_v1::CameraDescriptor` because the
// descriptor value is stored in `m_default_camera_desc` (POCO field, no
// cache).  The compiled `CameraProgram` lives only in
// `CompiledComposition::camera_program`.
// =============================================================================

namespace chronon3d {

template <typename T, typename E> class Result;
struct CompiledComposition;
struct CompositionCompileContext;
struct CompositionCompileError;

struct CompositionSpec {
    std::string name{"Untitled"};
    i32 width{1920};
    i32 height{1080};
    FrameRate frame_rate{30, 1};
    Frame duration{0}; // Asset roots belong to the runtime resolver, not authoring metadata.
};

class Composition {
public:
    using SceneFunction = std::function<Scene(const FrameContext&)>;

    Composition(CompositionSpec spec,
                SceneFunction render,
                std::uint64_t scene_content_fingerprint = 0)
        : m_spec(std::move(spec)),
          m_render(std::move(render)),
          m_scene_content_fingerprint(scene_content_fingerprint),
          m_identity(s_next_identity.fetch_add(1, std::memory_order_relaxed)) {}

    [[nodiscard]] i32 width() const { return m_spec.width; }
    [[nodiscard]] i32 height() const { return m_spec.height; }
    [[nodiscard]] FrameRate frame_rate() const { return m_spec.frame_rate; }
    [[nodiscard]] Frame duration() const { return m_spec.duration; }
    [[nodiscard]] const std::string& name() const { return m_spec.name; }

    /// Deterministic identity of values captured by the scene callback.
    /// Zero preserves the legacy authoring contract for callbacks whose
    /// recipe has no externally supplied identity.
    [[nodiscard]] std::uint64_t scene_content_fingerprint() const noexcept {
        return m_scene_content_fingerprint;
    }

    [[nodiscard]] std::uint64_t identity() const noexcept { return m_identity; }

    /// Direct evaluation from a pre-built FrameContext.
    /// This is the natural V2 entry point: callers that already have a
    /// FrameContext (e.g. tests, content compositions) can pass it
    /// directly without extracting individual fields.
    [[nodiscard]] Scene evaluate(const FrameContext& ctx) const {
        return evaluate_scene_function(m_render, ctx);
    }

    // ══════════════════════════════════════════════════════════════════════
    // P3-F — Default camera authoring surface (READ-ONLY after compile).
    //
    // The Composition now only carries the camera as a value-typed
    // `camera_v1::CameraDescriptor`.  No cache, no lazy compile, no
    // inverse-projection helper.  The OPP renderer that wants a
    // CameraProgram should:
    //   1. Read `Composition::default_camera_descriptor()`.
    //   2. Build a `CompositionDefinition` carrying that descriptor.
    //   3. Call `chronon3d::compile_composition(...)` to get a
    //      `CompiledComposition` whose `camera_program` is the
    //      SINGLE immutable compilation.
    //   4. Per frame, call `chronon3d::evaluate(compiled, ctx, f)`.
    //
    // The removed mutable Camera field is intentionally not part of this
    // authoring surface. Camera state is represented by the descriptor or
    // the pre-compiled canonical CameraProgram above.
    // ══════════════════════════════════════════════════════════════════════

    /// Set the canonical default-camera CameraDescriptor.
    /// P3-F: this is now a pure value-set; there is no cache to invalidate
    /// because the OPP compiles via `compile_composition` and owns the
    /// resulting `CompiledComposition`.  Passing an empty descriptor
    /// (`id.empty()`) is treated as "no descriptor set" by
    /// `has_default_camera_descriptor()`.
    Composition& default_camera_descriptor(
        chronon3d::camera_v1::CameraDescriptor descriptor) {
        m_default_camera_desc = std::move(descriptor);
        return *this;
    }

    /// Read-only accessor for the CameraDescriptor in composition settings.
    [[nodiscard]] const chronon3d::camera_v1::CameraDescriptor&
    default_camera_descriptor() const noexcept {
        return m_default_camera_desc;
    }

    /// True when `default_camera_descriptor(...)` has set a non-empty
    /// descriptor.  Read-only; the descriptor's presence is the OPP's
    /// signal that the V2 compile path should be used.
    [[nodiscard]] bool has_default_camera_descriptor() const noexcept {
        return !m_default_camera_desc.id.empty();
    }

private:
    friend Result<CompiledComposition, CompositionCompileError>
    compile_composition(const Composition& composition,
                        const CompositionCompileContext& context);

    [[nodiscard]] bool has_scene_function() const noexcept {
        return static_cast<bool>(m_render);
    }

    [[nodiscard]] SceneFunction scene_function_snapshot() const {
        return m_render;
    }

    [[nodiscard]] static Scene evaluate_scene_function(
        const SceneFunction& render,
        const FrameContext& ctx) {
        Scene result = render(ctx);
        if (!ctx.assets_root.empty()) {
            result.set_assets_root(ctx.assets_root);
        }
        return result;
    }

    // ── P3-F — the Composition is immutable on the camera side.
    //    Only the value-typed descriptor field remains; no cache or lazy
    //    inverse projection is retained.
    chronon3d::camera_v1::CameraDescriptor m_default_camera_desc{};
    std::uint64_t m_scene_content_fingerprint{0};
    inline static std::atomic_uint64_t s_next_identity{1};
    std::uint64_t m_identity{0};

    CompositionSpec m_spec;
    SceneFunction m_render;
};

inline Composition composition(CompositionSpec spec, Composition::SceneFunction render) {
    return Composition(std::move(spec), std::move(render));
}

} // namespace chronon3d
