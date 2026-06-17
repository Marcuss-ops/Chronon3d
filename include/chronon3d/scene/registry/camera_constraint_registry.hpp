#pragma once
// ==============================================================================
// chronon3d/scene/registry/camera_constraint_registry.hpp
//
// Singleton registry of CameraConstraint factories, keyed by dotted id
// (e.g. "camera.look_at", "camera.keep_horizon", "camera.damped_follow").
// Used by CameraProgram::add_constraint_named() to attach a constraint by id
// without writing a builder chain at the call site.
//
// Threading: register_factory() is intended to run at startup; after that
// the registry is read-only and safe to query from any thread.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_constraint.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace chronon3d::camera_v1 {

class CameraConstraintRegistry {
public:
    static CameraConstraintRegistry& instance();

    /// Factory function pointer: zero-arg, returns a fresh constraint.
    using Factory = std::shared_ptr<CameraConstraint> (*)();

    /// Register a factory. Throws on null or duplicate id.
    void register_factory(std::string id, Factory f);

    /// Create a constraint by id. Returns nullptr on miss (no throw).
    std::shared_ptr<CameraConstraint> create(const std::string& id) const;

    /// Enumerate all registered ids (alphabetical).
    std::vector<std::string> ids() const;

    bool has(const std::string& id) const;

private:
    CameraConstraintRegistry() = default;
    mutable std::mutex mu_;
    std::map<std::string, Factory> factories_;
};

} // namespace chronon3d::camera_v1
