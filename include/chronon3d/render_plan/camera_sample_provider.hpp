#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <functional>
#include <string>

namespace chronon3d::render_plan {

/// Runtime-only source of fully resolved camera values. `sample` receives an
/// absolute fractional presentation frame and returns the complete camera
/// state for that instant. The stable identity participates in render and
/// scene cache keys; the callback itself is never serialized.
struct RuntimeCameraSampleProvider {
    std::string fingerprint;
    std::function<CameraPlan(double)> sample;
};

} // namespace chronon3d::render_plan
