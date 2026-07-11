// =============================================================================
// include/chronon3d/scene/camera/camera_descriptor_builder.hpp
//
// Public fluent builder for `chronon3d::camera_v1::CameraDescriptor`.
//
// P3-H + feat(api) public camera facade — the canonical "how an external
// consumer writes a perspective camera" entry point.  Stateless, value-typed,
// zero allocation.  Returns a `CameraDescriptor` by value from `.build()`,
// which is then compiled via `chronon3d::compile_camera(descriptor).value()`.
//
// The fields exposed by `.position() / .look_at() / .lens() / .id() /
// .enabled()` map 1:1 onto `CameraBaseSpec` (and the `PhysicalLensProjection`
// variant of `ProjectionSpec`) so the consumer can compose a complete
// perspective descriptor without touching any internal types.
//
// Usage (mirrors the public spec example in the user task):
//
//   auto descriptor = chronon3d::camera()
//       .position({0.0f, 0.0f, -1200.0f})
//       .look_at({0.0f, 0.0f,    0.0f})
//       .lens(chronon3d::PhysicalLens{
//           .focal_length_mm = 50.0f,
//           .sensor_width_mm = 36.0f,
//       })
//       .build();
//   auto program = chronon3d::compile_camera(descriptor).value();
//
//   composition.camera(program);
//   renderer.render(composition, Frame{30});
//
// Cat-3 compliance: this header introduces NO new singleton / registry /
// resolver / sampler.  `CameraDescriptorBuilder` is a value-typed struct
// with no static state.
// =============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <string>

namespace chronon3d {

// ── PhysicalLens — public convenience struct for the spec example ────────
//
// Mirrors the public LensModel field-naming convention used by the V1
// external-consumer test.  Default-constructed to full-frame 50mm (the
// same defaults as `LensPresets::full_frame_50mm()`).  Implicitly
// convertible to `LensModel` so callers can pass it directly to
// `CameraDescriptorBuilder::lens()`.
struct PhysicalLens {
    f32 focal_length_mm{50.0f};
    f32 f_stop{2.8f};
    f32 close_focus_mm{450.0f};
    f32 sensor_width_mm{36.0f};
    f32 sensor_height_mm{24.0f};
    GateFit gate_fit{GateFit::Fill};
    f32 pixel_aspect{1.0f};
    f32 anamorphic_squeeze{1.0f};

    [[nodiscard]] LensModel to_lens_model() const noexcept {
        LensModel m;
        m.focal_length       = focal_length_mm;
        m.f_stop             = f_stop;
        m.close_focus        = close_focus_mm;
        m.sensor_width       = sensor_width_mm;
        m.sensor_height      = sensor_height_mm;
        m.gate_fit           = gate_fit;
        m.pixel_aspect       = pixel_aspect;
        m.anamorphic_squeeze = anamorphic_squeeze;
        return m;
    }
};

// ── CameraDescriptorBuilder — fluent value-typed builder ────────────────
class CameraDescriptorBuilder {
public:
    CameraDescriptorBuilder() = default;

    CameraDescriptorBuilder& id(std::string s) {
        desc_.id = std::move(s);
        return *this;
    }

    CameraDescriptorBuilder& enabled(bool e) noexcept {
        desc_.base.enabled = e;
        return *this;
    }

    /// Camera world-space position (scene units).
    CameraDescriptorBuilder& position(Vec3 v) noexcept {
        desc_.base.position = v;
        return *this;
    }

    /// Look-at target.  Maps to `base.point_of_interest` + a `LookAtPoint`
    /// `OrientationSpec` so the compiled camera orients to this target.
    CameraDescriptorBuilder& look_at(Vec3 v) noexcept {
        desc_.base.point_of_interest_enabled = true;
        desc_.base.point_of_interest = v;
        desc_.orientation = camera_v1::LookAtPoint{v};
        return *this;
    }

    /// Physical lens.  Implicitly switches the projection to
    /// `PhysicalLensProjection{lens}` so the compiled program uses the
    /// physical-lens evaluation path.
    CameraDescriptorBuilder& lens(LensModel l) noexcept {
        desc_.base.lens = l;
        desc_.base.projection = camera_v1::PhysicalLensProjection{l};
        return *this;
    }

    /// Physical lens (convenience: accepts `PhysicalLens` named-field
    /// struct, which matches the user-spec example exactly).
    CameraDescriptorBuilder& lens(PhysicalLens pl) noexcept {
        return lens(pl.to_lens_model());
    }

    /// Finalise the builder.  Returns the value-typed `CameraDescriptor`
    /// by copy; the builder remains in a valid (reusable) state.
    [[nodiscard]] camera_v1::CameraDescriptor build() const noexcept {
        return desc_;
    }

private:
    camera_v1::CameraDescriptor desc_{};
};

// ── chronon3d::camera() — free function returning a fresh builder ───────
//
// Spec example: `auto descriptor = camera()...`.  Returns by value so
// each call gets a fresh, isolated builder.
inline CameraDescriptorBuilder camera() {
    return CameraDescriptorBuilder{};
}

} // namespace chronon3d
