#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <functional>
#include <string>
#include <cstdint>
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
// The header STILL includes `camera_v1::CameraDescriptor` because the
// descriptor value is stored in `m_default_camera_desc` (POCO field, no
// cache).  No `camera_v1::CameraProgram` member is retained — heavy
// program type lives in `CompiledComposition::camera_program` only.
// =============================================================================

namespace chronon3d {

struct CompositionSpec {
    std::string name{"Untitled"};
    i32 width{1920};
    i32 height{1080};
    FrameRate frame_rate{30, 1};
    Frame duration{0};
    std::string assets_root{};

    /// RGBA clear color for the composition's clear pass (LE AABBGGRR;
    /// 0 = transparent black, preserves existing default-clear behaviour).
    /// TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-1 Batch 1.5 FORWARD-POINT —
    /// the field is ADDITIVE and DEFAULT-BIT-IDENTICAL (no existing caller
    /// change), but the OPP consumer path (`SoftwareRenderer::render` →
    /// `render_scene_via_graph` → OPP compiler) does NOT YET consume it
    /// (CRITICAL A confirmed via rg this session).  See TICKET-OPP-BG-CONSUMER.
    /// Field RETAINED so future OPP wiring can read it without re-introduction.
    std::uint32_t background_color_rgba{0x00000000u};
};

class Composition {
public:
    using SceneFunction = std::function<Scene(const FrameContext&)>;

    Composition(CompositionSpec spec, SceneFunction render)
        : m_spec(std::move(spec)), m_render(std::move(render)) {}

    [[nodiscard]] i32 width() const { return m_spec.width; }
    [[nodiscard]] i32 height() const { return m_spec.height; }
    [[nodiscard]] FrameRate frame_rate() const { return m_spec.frame_rate; }
    [[nodiscard]] Frame duration() const { return m_spec.duration; }
    [[nodiscard]] const std::string& name() const { return m_spec.name; }
    [[nodiscard]] const std::string& assets_root() const { return m_spec.assets_root; }

    /// Direct evaluation from a pre-built FrameContext.
    /// This is the natural V2 entry point: callers that already have a
    /// FrameContext (e.g. tests, content compositions) can pass it
    /// directly without extracting individual fields.
    [[nodiscard]] Scene evaluate(const FrameContext& ctx) const {
        Scene result = m_render(ctx);
        if (!ctx.assets_root.empty()) {
            result.set_assets_root(ctx.assets_root);
        }
        return result;
    }

    /// Evaluate one integral frame using the composition's immutable spec.
    /// This is the supported convenience entry point; sub-frame evaluation
    /// and service injection belong to an explicit FrameContext.
    [[nodiscard]] Scene evaluate(Frame frame) const {
        const auto sample = SampleTime::from_frame_int(frame, m_spec.frame_rate);
        return evaluate(make_frame_context({
            .global_time = sample,
            .duration = m_spec.duration,
            .width = m_spec.width,
            .height = m_spec.height,
            .assets_root = m_spec.assets_root,
        }));
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

    // ══════════════════════════════════════════════════════════════════════
    // P3-H + feat(api) public camera facade — Composition::camera_program(p)
    //
    // Mirror of the spec example:
    //   auto program  = compile_camera(descriptor).value();
    //   composition.camera_program(program);
    //   renderer.render(composition, Frame{30});
    //
    // Stores a pre-compiled `CameraProgram` on the Composition.  The OPP
    // renderer consumes the program on the per-frame evaluate path,
    // skipping the `compile_camera(...)` step on the hot loop.
    //
    // The explicit name keeps the pre-compiled program distinct from the
    // descriptor-based authoring surface.
    // ══════════════════════════════════════════════════════════════════════
    Composition& camera_program(chronon3d::camera_v1::CameraProgram program) {
        m_camera_program = std::move(program);
        return *this;
    }

    /// Read-only accessor for the pre-compiled camera program.
    [[nodiscard]] const chronon3d::camera_v1::CameraProgram&
    camera_program() const noexcept {
        return m_camera_program;
    }

    /// True iff `camera(program)` has been called with a non-empty
    /// (compiled) program.  The OPP renderer uses this probe to implement
    /// the documented precedence policy: when BOTH `camera(p)` and
    /// `default_camera_descriptor(d)` are set, the program wins at render
    /// time.  See `camera(p)` doc-comment for the full precedence rule.
    [[nodiscard]] bool has_camera_program() const noexcept {
        return m_camera_program.is_compiled();
    }

    /// True when `default_camera_descriptor(...)` has set a non-empty
    /// descriptor.  Read-only; the descriptor's presence is the OPP's
    /// signal that the V2 compile path should be used.
    [[nodiscard]] bool has_default_camera_descriptor() const noexcept {
        return !m_default_camera_desc.id.empty();
    }

private:
    // ── P3-F — the Composition is immutable on the camera side.
    //    Only the value-typed descriptor field remains; no cache or lazy
    //    inverse projection is retained.
    chronon3d::camera_v1::CameraDescriptor m_default_camera_desc{};

    // ── P3-H + feat(api) public camera facade — pre-compiled program.
    //    Authoring-time entry point: the consumer calls
    //    `composition.camera(compile_camera(descriptor).value())` to
    //    set the program once.  The OPP renderer reads
    //    `m_camera_program` on each evaluate() call.
    chronon3d::camera_v1::CameraProgram m_camera_program{};

    CompositionSpec m_spec;
    SceneFunction m_render;
};

inline Composition composition(CompositionSpec spec, Composition::SceneFunction render) {
    return Composition(std::move(spec), std::move(render));
}

} // namespace chronon3d
