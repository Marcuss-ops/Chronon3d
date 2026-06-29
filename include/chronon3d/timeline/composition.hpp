#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <functional>
#include <string>
#include <memory>
#include <vector>

// =============================================================================
// chronon3d/timeline/composition.hpp
//
// Composition header — TICKET-034.
//
// The default camera is a `camera_v1::CameraDescriptor` held in composition
// settings, compiled via the canonical `compile_camera()` pipeline at first
// read, evaluated by the existing `CameraProgram` runtime, fingerprint-
// serializable via the FNV-1a helper in
// `<chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>`.
//
// The header MUST include the full `camera_v1` types — the private members
// `m_default_camera_desc` (value) and `m_default_program_cache` (unique_ptr
// to a value type) REQUIRE complete types at this translation-unit level.
// The heavy `compile_camera() / evaluate()` BODIES remain out-of-line in
// `src/timeline/composition.cpp` so spdlog + camera_v1 program-compiler
// translation-unit weight is paid exactly once per binary, not once per
// scheduling TU that includes this header.
//
// Surface (kept stable across TICKET-033 / TICKET-034 / TICKET-035):
//   * The CompositionSpec struct.
//   * The Composition class public API (evaluate / camera enum /
//     default_camera_descriptor / has_default_camera_descriptor /
//     invalidate_default_camera_program / default_camera_program /
//     redecompose_camera_from_descriptor).
//   * The legacy `Camera camera;` public field (kept for backward-compat
//     with golden harnesses).
// =============================================================================

namespace chronon3d {

struct CompositionSpec {
    std::string name{"Untitled"};
    i32 width{1920};
    i32 height{1080};
    FrameRate frame_rate{30, 1};
    Frame duration{0};
    std::string assets_root{""};
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

    [[nodiscard]] Scene evaluate(Frame frame,
                                 std::pmr::memory_resource* res = std::pmr::get_default_resource()) const {
        return evaluate(frame, 0.0f, res);
    }

    [[nodiscard]] Scene evaluate(SampleTime time,
                                 std::pmr::memory_resource* res = std::pmr::get_default_resource()) const {
        return evaluate_double(time.frame, time.frame_rate, res);
    }

    [[nodiscard]] Scene evaluate(Frame frame, f32 frame_time,
                                 std::pmr::memory_resource* res = std::pmr::get_default_resource()) const {
        return evaluate_double(static_cast<double>(frame) + static_cast<double>(frame_time),
                               m_spec.frame_rate, res);
    }

    // codex/agent2-font-bind-fixes — engine-aware evaluate overload.
    // Same semantics as the 3-arg version above, but additionally threads
    // the per-frame FontEngine from the render pipeline
    // (SoftwareRenderer::font_engine()) into FrameContext::font_engine,
    // which is then auto-forwarded by SceneBuilder(ctx) onto every
    // LayerBuilder.  Without this overload the WP-8 PR 8.0 strict
    // binding in `materialize_text_run_shape` rejects the resolve_engine
    // lookup (engine=nullptr if no other binding path) and text layers
    // render blank.  Existing 1/2/3-arg overloads continue to default
    // engine=nullptr for backwards compatibility.
    //
    // Parameter ordering: engine BEFORE memres (engine is the more
    // semantically important binding; memres is defaulted so callers
    // don't need to write `get_default_resource()` everywhere).
    [[nodiscard]] Scene evaluate(Frame frame, f32 frame_time,
                                 FontEngine* engine,
                                 std::pmr::memory_resource* res = std::pmr::get_default_resource()) const {
        return evaluate_double(static_cast<double>(frame) + static_cast<double>(frame_time),
                               m_spec.frame_rate, res, engine);
    }

    // ══════════════════════════════════════════════════════════════════════
    // TICKET-034 — Default camera as a normal CameraDescriptor in composition
    // settings.  Implementations live in `src/timeline/composition.cpp`.
    // ══════════════════════════════════════════════════════════════════════

    /// Set the canonical default-camera CameraDescriptor.  Passing an
    /// empty descriptor (`id.empty()`) is treated as "no descriptor set"
    /// by `has_default_camera_descriptor()`.
    Composition& default_camera_descriptor(
        chronon3d::camera_v1::CameraDescriptor descriptor);

    /// Read-only accessor for the CameraDescriptor in composition settings.
    [[nodiscard]] const chronon3d::camera_v1::CameraDescriptor&
    default_camera_descriptor() const noexcept {
        return m_default_camera_desc;
    }

    /// True when `default_camera_descriptor(...)` has set a non-empty
    /// descriptor.  Used as a quick filter so the render pipeline can
    /// skip the canonical compile path when no descriptor was supplied.
    [[nodiscard]] bool has_default_camera_descriptor() const noexcept {
        return !m_default_camera_desc.id.empty();
    }

    // ── Thread-safety: Composition's default-camera cache (mutable
    //    unique_ptr) assumes ONE job-thread per Composition.  Calling
    //    `default_camera_program()` concurrently from multiple
    //    threads on the same Composition is a USE-BEYOND-CONTRACT that
    //    may double-compile and race the unique_ptr.  Wrap with
    //    `std::call_once` at the call site if multi-threaded access is
    //    needed. ──
    /// Invalidate the lazy-compile cache so the next call to
    /// `default_camera_program()` re-runs `compile_camera(...)`.
    /// Cheap.  Returns the previous "was compiled" state for diagnostics.
    bool invalidate_default_camera_program();

    /// Lazy-compile + cache of the canonical `compile_camera()` result.
    /// Safe to call repeatedly — returns the same CameraProgram from
    /// the cache after the first successful compile.  Implementation
    /// in `src/timeline/composition.cpp` (heavy include of
    /// camera_program_compiler.hpp + spdlog lives there).
    [[nodiscard]] const chronon3d::camera_v1::CameraProgram&
    default_camera_program() const;

    /// Evaluate the cached (or freshly compiled) CameraProgram at
    /// `time` and project the resulting Camera2_5D state onto the
    /// slim legacy `Composition::camera` field.  Returns false ONLY
    /// when no descriptor is set or the compile failed.  For every
    /// successful compile the function returns `true` and copies
    /// `transform.position` + `transform.rotation`; the slim legacy
    /// field's `fov_deg` is only populated when the projection variant
    /// is `FovProjection` AND the evaluated value is > 0 (ZoomProjection
    /// leaves the field at its struct default — Camera::focal_length
    /// is a GETTER, not a setter).
    ///
    /// Projection — strictly limited to fields that ACTUALLY exist
    /// on the legacy `Camera` struct (transform position + rotation,
    /// fov_deg, near_plane, far_plane):
    ///   * transform.position ← result.camera.position
    ///   * transform.rotation ← result.camera.rotation
    ///   * fov_deg            ← result.camera.fov_deg  (only when
    ///                           projection is FovProjection AND the
    ///                           evaluated fov_deg > 0)
    ///
    /// Out of scope (the slim legacy `Camera` struct has no field):
    ///   * parent_name / target_name
    ///   * point_of_interest + point_of_interest_enabled
    ///   * DoF / LensModel / MotionBlurSettings (rich renderer input)
    ///   * ZoomProjection → derived fov_deg (getter-only math)
    ///
    /// Callers needing those fields must read
    /// `default_camera_program().descriptor()` or call
    /// `default_camera_program().evaluate(...)` directly.  A future
    /// TICKET-035 will add a `Camera2_5D`-shaped field to the
    /// composition so the rich payload survives evaluation end-to-end
    /// (including the ZoomProjection → derived fov_deg path).
    bool redecompose_camera_from_descriptor(SampleTime time);

public:
    /// Legacy public Camera field (renderable + mutable for tests/golden
    /// harnesses).  TICKET-034 keeps this mutable for backwards
    /// compatibility; *new* authoring should set
    /// `default_camera_descriptor(...)` and call
    /// `redecompose_camera_from_descriptor(...)` to refresh this field.
    Camera camera;

private:
    [[nodiscard]] Scene evaluate_double(double frame, FrameRate rate,
                                        std::pmr::memory_resource* res,
                                        FontEngine* engine = nullptr) const {
        const Frame integral = static_cast<Frame>(std::floor(frame));
        FrameContext ctx{
            .frame      = integral,
            .local_frame = integral,
            .frame_time = static_cast<f32>(frame - std::floor(frame)),
            .duration   = m_spec.duration,
            .frame_rate = rate,
            .width      = m_spec.width,
            .height     = m_spec.height,
            .assets_root = m_spec.assets_root,
            .resource   = res,
            .font_engine = engine,  // codex/agent2-font-bind-fixes
        };
        // No longer calling AssetRegistry::mount() globally — the assets root
        // is threaded through FrameContext → Scene → RenderGraphContext →
        // thread-local guard, so concurrent render jobs don't interfere.
        Scene result = m_render(ctx);
        if (!ctx.assets_root.empty()) {
            result.set_assets_root(ctx.assets_root);
        }
        return result;
    }

    // ── TICKET-034 — canonical default-camera authoring surface.  The
    //    descriptor lives on the composition (settings-class field) and
    //    is compiled via the canonical compile_camera() pipeline at
    //    first read.  `m_default_program_cache` is mutable so
    //    const-correctness is preserved on `default_camera_program()`.
    //
    //    `m_last_warned_descriptor_hash` triggers on every
    //    `compile_camera()` failure with a new content-stable FNV-1a
    //    descriptor fingerprint so `spdlog::warn` does NOT spam the
    //    log when the same bad descriptor is re-evaluated every frame.
    chronon3d::camera_v1::CameraDescriptor m_default_camera_desc{};
    mutable std::unique_ptr<chronon3d::camera_v1::CameraProgram>
        m_default_program_cache;
    mutable std::uint64_t m_last_warned_descriptor_hash{0};

    CompositionSpec m_spec;
    SceneFunction m_render;
};

inline Composition composition(CompositionSpec spec, Composition::SceneFunction render) {
    return Composition(std::move(spec), std::move(render));
}

} // namespace chronon3d
