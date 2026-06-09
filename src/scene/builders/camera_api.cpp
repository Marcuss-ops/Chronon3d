#include <chronon3d/scene/builders/camera_api.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d {

CameraApi& CameraApi::set(Camera2_5D camera) {
    owner_->set_camera(std::move(camera));
    return *this;
}

CameraApi& CameraApi::set_animated(const AnimatedCamera2_5D& cam) {
    return set(cam.evaluate(owner_->frame()));
}

CameraApi& CameraApi::enable(bool enabled) {
    owner_->edit_camera([&](Camera2_5D& cam) { cam.enabled = enabled; });
    return *this;
}

CameraApi& CameraApi::position(Vec3 p) {
    owner_->edit_camera([&](Camera2_5D& cam) { cam.position = p; });
    return *this;
}

CameraApi& CameraApi::zoom(f32 value) {
    owner_->edit_camera([&](Camera2_5D& cam) { cam.zoom = value; });
    return *this;
}

CameraApi& CameraApi::fov(f32 fov_deg) {
    owner_->edit_camera([&](Camera2_5D& cam) {
        cam.fov_deg = fov_deg;
        cam.projection_mode = Camera2_5DProjectionMode::Fov;
    });
    return *this;
}

CameraApi& CameraApi::dof(DepthOfFieldSettings value) {
    owner_->edit_camera([&](Camera2_5D& cam) { cam.dof = value; });
    return *this;
}

CameraApi& CameraApi::parent(std::string name) {
    owner_->edit_camera([&, n = std::move(name)](Camera2_5D& cam) {
        cam.parent_name = std::pmr::string{n, owner_->resource()};
    });
    return *this;
}

CameraApi& CameraApi::rotation(Vec3 euler_deg) {
    owner_->edit_camera([&](Camera2_5D& cam) { cam.rotation = euler_deg; });
    return *this;
}

CameraApi& CameraApi::tilt(f32 degrees) {
    owner_->edit_camera([&](Camera2_5D& cam) { cam.set_tilt(degrees); });
    return *this;
}

CameraApi& CameraApi::pan(f32 degrees) {
    owner_->edit_camera([&](Camera2_5D& cam) { cam.set_pan(degrees); });
    return *this;
}

CameraApi& CameraApi::roll(f32 degrees) {
    owner_->edit_camera([&](Camera2_5D& cam) { cam.set_roll(degrees); });
    return *this;
}

CameraApi& CameraApi::point_of_interest(Vec3 poi) {
    owner_->edit_camera([&](Camera2_5D& cam) {
        cam.point_of_interest = poi;
        cam.point_of_interest_enabled = true;
    });
    return *this;
}

CameraApi& CameraApi::look_at(Vec3 poi) {
    return point_of_interest(poi);
}

CameraApi& CameraApi::target(std::string name) {
    owner_->edit_camera([&, n = std::move(name)](Camera2_5D& cam) {
        cam.target_name = std::pmr::string{n, owner_->resource()};
        cam.point_of_interest_enabled = true;
    });
    return *this;
}

} // namespace chronon3d
