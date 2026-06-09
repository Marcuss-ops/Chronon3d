#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/core/mask_utils.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <variant>

#include "detail/bbox_projection.hpp"
#include "clear_node.hpp"
#include "source_node.hpp"
#include "multi_source_node.hpp"
#include "mask_node.hpp"
#include "effect_stack_node.hpp"
#include "adjustment_node.hpp"
#include "composite_node.hpp"

namespace chronon3d::renderer {
    chronon3d::raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread = 0.0f);
}
