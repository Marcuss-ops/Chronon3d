#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling.hpp>
#include <spdlog/spdlog.h>
#include <any>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <variant>

namespace chronon3d::renderer {
    chronon3d::raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread = 0.0f);
}
