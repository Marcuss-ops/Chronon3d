#pragma once
// ==============================================================================
// chronon3d/scene/registry/camera_constraint_registry.hpp
//
// Singleton registry of CameraConstraint factories, keyed by dotted id.
// Factories now accept CameraConstraintParams for typed configuration.
//
// Threading: register_factory() intended at startup; after freeze() the
// registry is read-only and safe to query from any thread.
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

    using Factory = ConstraintFactory;

    /// Register a factory. Throws on null, empty id, or duplicate.
    /// Throws std::logic_error if frozen.
    void register_factory(std::string id, Factory f);

    /// Create a constraint by id with default params. Returns nullptr on miss.
    std::shared_ptr<CameraConstraint> create(const std::string& id) const;

    /// Create with explicit params (for non-default configuration).
    std::shared_ptr<CameraConstraint> create(const std::string& id,
                                              const CameraConstraintParams& params) const;

    std::vector<std::string> ids() const;
    bool has(const std::string& id) const;
    void freeze();
    bool is_frozen() const noexcept { return frozen_; }

private:
    CameraConstraintRegistry() = default;
    mutable std::mutex mu_;
    std::map<std::string, Factory> factories_;
    bool frozen_{false};
};

} // namespace chronon3d::camera_v1
