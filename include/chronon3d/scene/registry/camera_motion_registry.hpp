#pragma once
// ==============================================================================
// chronon3d/scene/registry/camera_motion_registry.hpp
//
// V1 Camera Motion Registry — single source of truth for all camera motion
// presets. Backed by std::map for deterministic enumeration order.
//
// Migration model (P2):
//   - Existing camera_rig::hero_push_in() / focus_pull() / etc. are KEPT as
//     thin compat wrappers that call CameraMotionRegistry::instance().build().
//   - New presets are added via register_motion(...) at module init time.
//   - CLI exposes them via /api/registry/camera_motions / `camera list-motions`.
//
// Concurrency: register_motion() is intended to be called at startup. After
// that point the registry is read-only and safe to query from any thread.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_motion_descriptor.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace chronon3d::camera_v1 {

/// Singleton registry: all camera-motion presets are registered here.
class CameraMotionRegistry {
public:
    static CameraMotionRegistry& instance();

    /// Register a motion plugin. Throws std::invalid_argument if id is empty
    /// or already registered.
    void register_motion(std::shared_ptr<CameraMotion> motion);

    /// Build a Camera2_5D by evaluating the preset for ctx. Returns the base
    /// cam (unchanged) if `id` is not found. This is the *non-throwing* path.
    Camera2_5D build(const std::string& id, const CameraMotionContext& ctx,
                    const Camera2_5D& base = {}) const;

    /// Look up by id. Returns nullptr if absent. After freeze(), the shared_ptr
    /// guarantees the returned CameraMotion is alive even without the mutex held.
    std::shared_ptr<const CameraMotion> find(const std::string& id) const;

    /// Enumerate all registered motion ids (deterministic alphabetical order).
    std::vector<std::string> ids() const;

    /// Enumerate all descriptors (parallel to ids()).
    std::vector<CameraMotionDescriptor> catalog() const;

    /// True if id is registered.
    bool has(const std::string& id) const;

    /// Freeze the registry: after this, reads are concurrent-safe (no mutex)
    /// and any register_motion() call throws std::logic_error.
    /// Idempotent: calling freeze() multiple times is a no-op.
    void freeze();

    /// True if freeze() has been called.
    bool is_frozen() const noexcept { return frozen_; }

    // Default constructor is public to allow test-only isolated instances.
    // Production code MUST use instance(), not direct construction.
    CameraMotionRegistry() = default;

    mutable std::mutex mu_;
    std::map<std::string, std::shared_ptr<CameraMotion>> motions_;
    bool frozen_{false};
};

} // namespace chronon3d::camera_v1
