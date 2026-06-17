#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_motion_descriptor.hpp
//
// Descriptor + interface contract for any camera motion that can be plugged
// into CameraMotionRegistry. This is the V1 "one entry-point" surface.
//
// All motion plugins must satisfy:
//   - descriptor() returns stable id + category + description
//   - evaluate(ctx) produces a deterministic Camera2_5D for the given ctx
//   - register() / unregister() managed by CameraMotionRegistry, not here
//
// The interface is non-intrusive: the existing camera_rig::preset functions
// stay compilable; this header only adds the new path.
// ==============================================================================
#include <chronon3d/math/camera_2_5d_projection.hpp>  // Camera2_5D

#include <string>
#include <vector>

namespace chronon3d::camera_v1 {

/// Forward declarations
struct CameraMotionContext;

/// Catalog metadata for `camera list-motions` CLI introspection.
struct CameraMotionDescriptor {
    std::string id;            ///< Stable dotted id, e.g. "camera.hero_push".
    std::string category;      ///< "push-in", "orbit", "parallax", "dolly-zoom", ...
    std::string description;   ///< Human-readable, single line.
    bool loopable{false};      ///< If true, repeating ctx.frame doesn't drift.
};

/// Pure interface: evaluate(ctx) -> Camera2_5D. Stateless beyond descriptor.
class CameraMotion {
public:
    virtual ~CameraMotion() = default;
    virtual CameraMotionDescriptor descriptor() const = 0;
    virtual Camera2_5D evaluate(const CameraMotionContext& ctx) const = 0;
};

/// Helper: identical-content descriptor for presets that share a category.
inline CameraMotionDescriptor make_descriptor(std::string id,
                                              std::string category,
                                              std::string description,
                                              bool loopable = false) {
    return CameraMotionDescriptor{
        std::move(id),
        std::move(category),
        std::move(description),
        loopable,
    };
}

} // namespace chronon3d::camera_v1
