#pragma once
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/effect_presets/core.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include "../../src/render_plan/render_plan_compiler_detail.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <chronon3d/render_plan/subtitle_style.hpp>
#include <chronon3d/scene/model/core/transition_catalog_ids.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/core/mask_utils.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/text/text_color_packing.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/math/expression.hpp>
#if defined(CHRONON_TEST_ENABLE_SVG)
#include <chronon3d/render_plan/render_plan.hpp>
#endif
#include <chronon3d/timeline/compile_evaluate.hpp>
#include "test_render_plan_shape_helpers.hpp"
#include "../../src/render_plan/audio_react_baker.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

class FingerprintTempDir {
public:
    FingerprintTempDir() {
        path = std::filesystem::temp_directory_path() /
               ("chronon3d_fingerprint_" +
                std::to_string(std::chrono::steady_clock::now()
                                   .time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~FingerprintTempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

chronon3d::render_plan::RenderPlan image_plan() {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "fingerprint-plan";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{3}};
    chronon3d::render_plan::LayerPlan image;
    image.id = "image";
    image.type = chronon3d::render_plan::LayerType::Image;
    image.asset = "images/source.png";
    plan.layers.push_back(std::move(image));
    return plan;
}

void write_asset(const std::filesystem::path& root, std::string contents) {
    std::filesystem::create_directories(root / "images");
    std::ofstream output(root / "images/source.png", std::ios::binary);
    output << contents;
}

std::filesystem::path repo_image_fixture() {
    auto root = std::filesystem::current_path();
    for (int depth = 0; depth < 8 && !root.empty(); ++depth) {
        const auto candidate = root / "tests/install_consumer/assets/test_image.png";
        if (std::filesystem::is_regular_file(candidate)) return candidate;
        const auto parent = root.parent_path();
        if (parent == root) break;
        root = parent;
    }
    throw std::runtime_error("cannot locate tests/install_consumer/assets/test_image.png");
}

} // namespace

