#include <chronon3d/media/media_placement.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {

MediaPlacementResult compute_media_placement(
    Vec2 source_size,
    Vec2 box_size,
    FitMode fit,
    Vec2 focal_point
) {
    MediaPlacementResult result;

    if (source_size.x <= 0.0f || source_size.y <= 0.0f || box_size.x <= 0.0f || box_size.y <= 0.0f) {
        result.src_rect = { {0.0f, 0.0f}, {0.0f, 0.0f} };
        result.dst_rect = { {0.0f, 0.0f}, {0.0f, 0.0f} };
        return result;
    }

    focal_point.x = std::clamp(focal_point.x, 0.0f, 1.0f);
    focal_point.y = std::clamp(focal_point.y, 0.0f, 1.0f);

    switch (fit) {
        case FitMode::Stretch: {
            result.src_rect = { {0.0f, 0.0f}, source_size };
            result.dst_rect = { {0.0f, 0.0f}, box_size };
            break;
        }
        case FitMode::None: {
            result.src_rect = { {0.0f, 0.0f}, source_size };
            Vec2 origin = (box_size - source_size) * 0.5f;
            result.dst_rect = { origin, source_size };
            break;
        }
        case FitMode::Contain: {
            float s = std::min(box_size.x / source_size.x, box_size.y / source_size.y);
            Vec2 dst_sz = source_size * s;
            Vec2 origin = (box_size - dst_sz) * 0.5f;
            result.src_rect = { {0.0f, 0.0f}, source_size };
            result.dst_rect = { origin, dst_sz };
            break;
        }
        case FitMode::Cover: {
            float s = std::max(box_size.x / source_size.x, box_size.y / source_size.y);
            Vec2 src_crop_size = box_size / s;

            // clamp src_crop_size to source_size to avoid precision overflow
            src_crop_size.x = std::min(src_crop_size.x, source_size.x);
            src_crop_size.y = std::min(src_crop_size.y, source_size.y);

            float crop_x = (source_size.x - src_crop_size.x) * focal_point.x;
            float crop_y = (source_size.y - src_crop_size.y) * focal_point.y;

            // clamp origins to safe bounds
            crop_x = std::clamp(crop_x, 0.0f, std::max(0.0f, source_size.x - src_crop_size.x));
            crop_y = std::clamp(crop_y, 0.0f, std::max(0.0f, source_size.y - src_crop_size.y));

            result.src_rect = { {crop_x, crop_y}, src_crop_size };
            result.dst_rect = { {0.0f, 0.0f}, box_size };
            break;
        }
    }

    return result;
}

} // namespace chronon3d
