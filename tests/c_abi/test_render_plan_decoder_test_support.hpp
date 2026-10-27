#pragma once
#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>
#include "../../src/render_plan/light_attenuation.hpp"
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/scene/path_sampling.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>

#include "test_render_plan_shape_helpers.hpp"
#include "../../src/render_plan/render_plan_compiler_detail.hpp"

#include <utility>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <cmath>
#include <string>
#include <nlohmann/json.hpp>

