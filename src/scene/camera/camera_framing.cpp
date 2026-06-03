#include <chronon3d/scene/camera/camera_framing.hpp>
#include <glm/glm.hpp>
#include <algorithm>

namespace chronon3d {

Camera2_5D fit_camera_to_layers(
    Camera2_5D camera,
    const std::vector<std::string>& layer_names,
    const TransformResolverResult& transforms,
    Viewport viewport,
    CameraFramingOptions options
) {
    Camera2_5D current_cam = camera;

    for (int iter = 0; iter < options.max_iterations; ++iter) {
        bool needs_dolly_out = false;
        bool can_dolly_in = true;

        for (const auto& name : layer_names) {
            auto mat_opt = transforms.world_matrix(name);
            if (!mat_opt) continue;

            Vec2 size{200.0f, 200.0f};
            auto size_it = options.layer_sizes.find(name);
            if (size_it != options.layer_sizes.end()) {
                size = size_it->second;
            }
            const Mat4& world_m = *mat_opt;
            std::array<Vec3, 4> corners = {
                Vec3(world_m * Vec4(0.0f, 0.0f, 0.0f, 1.0f)),
                Vec3(world_m * Vec4(size.x, 0.0f, 0.0f, 1.0f)),
                Vec3(world_m * Vec4(size.x, size.y, 0.0f, 1.0f)),
                Vec3(world_m * Vec4(0.0f, size.y, 0.0f, 1.0f))
            };

            ProjectedBounds pb = project_quad_to_screen(corners, current_cam, viewport);

            float sx1 = viewport.width * options.safe_margin_norm;
            float sy1 = viewport.height * options.safe_margin_norm;
            float sx2 = viewport.width * (1.0f - options.safe_margin_norm);
            float sy2 = viewport.height * (1.0f - options.safe_margin_norm);

            bool outside_safe = (pb.min.x < sx1 || pb.max.x > sx2 ||
                                 pb.min.y < sy1 || pb.max.y > sy2);

            if (outside_safe || pb.visible_ratio < options.min_visible_ratio) {
                needs_dolly_out = true;
            }

            float tolerance = 50.0f;
            bool too_far_inside = (pb.min.x > sx1 + tolerance && pb.max.x < sx2 - tolerance &&
                                   pb.min.y > sy1 + tolerance && pb.max.y < sy2 - tolerance);
            if (!too_far_inside) {
                can_dolly_in = false;
            }
        }

        if (needs_dolly_out) {
            if (current_cam.point_of_interest_enabled) {
                Vec3 dir = glm::normalize(current_cam.position - current_cam.point_of_interest);
                current_cam.position += dir * options.dolly_step;
            } else {
                current_cam.position.z -= options.dolly_step;
            }
        } else if (can_dolly_in) {
            if (current_cam.point_of_interest_enabled) {
                Vec3 dir = glm::normalize(current_cam.position - current_cam.point_of_interest);
                current_cam.position -= dir * (options.dolly_step * 0.5f);
            } else {
                current_cam.position.z += (options.dolly_step * 0.5f);
            }
        } else {
            break;
        }
    }

    return current_cam;
}

} // namespace chronon3d
