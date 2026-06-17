// ==============================================================================
// chronon3d/scene/registry/camera_motion_registry.cpp
// ==============================================================================
#include <chronon3d/scene/registry/camera_motion_registry.hpp>
#include <chronon3d/scene/camera/camera_rig_builder.hpp>  // camera_rig::hero_push_in
#include <chronon3d/animations/camera_motion_applier.hpp>  // motion-rig applier (legacy bridge)

#include <mutex>
#include <stdexcept>

namespace chronon3d::camera_v1 {

CameraMotionRegistry& CameraMotionRegistry::instance() {
    static CameraMotionRegistry r;
    return r;
}

void CameraMotionRegistry::register_motion(std::shared_ptr<CameraMotion> motion) {
    if (!motion) throw std::invalid_argument("CameraMotionRegistry: null motion");
    auto desc = motion->descriptor();
    if (desc.id.empty()) throw std::invalid_argument("CameraMotionRegistry: empty id");
    std::lock_guard<std::mutex> lk(mu_);
    if (motions_.count(desc.id)) {
        throw std::invalid_argument("CameraMotionRegistry: duplicate id '" + desc.id + "'");
    }
    motions_.emplace(desc.id, std::move(motion));
}

const CameraMotion* CameraMotionRegistry::find(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = motions_.find(id);
    return it == motions_.end() ? nullptr : it->second.get();
}

bool CameraMotionRegistry::has(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    return motions_.count(id) > 0;
}

std::vector<std::string> CameraMotionRegistry::ids() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    out.reserve(motions_.size());
    for (auto& kv : motions_) out.push_back(kv.first);
    return out;
}

std::vector<CameraMotionDescriptor> CameraMotionRegistry::catalog() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<CameraMotionDescriptor> out;
    out.reserve(motions_.size());
    for (auto& kv : motions_) out.push_back(kv.second->descriptor());
    return out;
}

Camera2_5D CameraMotionRegistry::build(const std::string& id,
                                       const CameraMotionContext& ctx,
                                       const Camera2_5D& base) const {
    const CameraMotion* m = find(id);
    if (!m) return base;  // graceful fallback for missing preset ids
    return m->evaluate(ctx);
}

} // namespace chronon3d::camera_v1
