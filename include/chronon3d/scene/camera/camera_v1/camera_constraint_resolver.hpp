#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_constraint_resolver.hpp
//
// ConstraintStack is an ordered list of constraints. The resolver runs the
// list top-to-bottom, threading each Camera2_5D result into the next input.
// Order is explicitly user-controlled (do NOT auto-sort).
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_constraint.hpp>

#include <memory>
#include <string>
#include <vector>

namespace chronon3d::camera_v1 {

class CameraConstraintStack {
public:
    /// Add at the end (top of the stack). Returns *this for chaining.
    CameraConstraintStack& add(std::shared_ptr<CameraConstraint> c);
    /// Insert at position i (used for tests / debugging).
    CameraConstraintStack& insert(std::size_t i, std::shared_ptr<CameraConstraint> c);

    std::size_t size() const { return stack_.size(); }
    bool empty() const { return stack_.empty(); }

    /// Read-only access (for inspection / report).
    const std::vector<std::shared_ptr<CameraConstraint>>& all() const { return stack_; }

    /// Evaluate top-down: starts from `start`, returns final result.
    ConstraintResult resolve(const Camera2_5D& start,
                             const CameraMotionContext& ctx,
                             ConstraintSession& session) const;

private:
    std::vector<std::shared_ptr<CameraConstraint>> stack_;
};

} // namespace chronon3d::camera_v1
