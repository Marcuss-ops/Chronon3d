#pragma once

#include <chronon3d/animation/effects/animated_transform.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <vector>
#include <numeric>
#include <algorithm>
#include <random>
#include <functional>
#include <string>

namespace chronon3d {

enum class StaggerOrder {
    LeftToRight,
    RightToLeft,
    TopToBottom,
    BottomToTop,
    CenterOut,
    CenterIn,
    Random,
    DepthNearToFar,
    DepthFarToNear,
};

struct StaggerConfig {
    Frame delay_per_unit{4};
    EasingCurve easing{Easing::OutCubic};
    f32 randomize{0.0f};         // 0..1, variance relative to delay_per_unit
    unsigned random_seed{0};    // 0 = use random_device seed
};

inline Frame compute_stagger_delay(size_t rank, size_t total, const StaggerConfig& config) {
    if (total <= 1) return Frame{0};
    f32 t = static_cast<f32>(rank) / static_cast<f32>(total - 1);
    f32 eased = config.easing.apply(t);
    f32 max_delay = static_cast<f32>(static_cast<int>(total - 1) * static_cast<int>(config.delay_per_unit));
    Frame base = Frame{static_cast<int>(eased * max_delay)};

    if (config.randomize > 0.0f) {
        static thread_local std::mt19937 rng(config.random_seed ? config.random_seed : std::random_device{}());
        std::uniform_real_distribution<f32> dist(-1.0f, 1.0f);
        f32 variance = dist(rng) * static_cast<f32>(config.delay_per_unit) * config.randomize;
        base += Frame{static_cast<int>(std::lround(variance))};
    }
    return base;
}

/// Concept: GetPosition must be invocable with const T& and return
/// something convertible to Vec3 (or comparable for spatial sorting).
template<typename F, typename T>
concept PositionGetter = std::invocable<F, const T&>;

namespace detail {

template<typename T, PositionGetter<T> GetPosition>
std::vector<size_t> compute_stagger_ranks_impl(
    const std::vector<T*>& items,
    StaggerOrder order,
    GetPosition get_position,
    const StaggerConfig& config
) {
    std::vector<size_t> indices(items.size());
    std::iota(indices.begin(), indices.end(), 0);

    if (order == StaggerOrder::Random) {
        std::mt19937 rng(config.random_seed ? config.random_seed : std::random_device{}());
        std::shuffle(indices.begin(), indices.end(), rng);
    } else if (order == StaggerOrder::CenterOut || order == StaggerOrder::CenterIn) {
        Vec3 centroid{0.0f};
        for (size_t i = 0; i < items.size(); ++i) {
            centroid += get_position(*items[i]);
        }
        centroid /= static_cast<f32>(items.size());

        auto dist_cmp = [&](size_t a, size_t b) {
            f32 da = glm::length(get_position(*items[a]) - centroid);
            f32 db = glm::length(get_position(*items[b]) - centroid);
            return order == StaggerOrder::CenterOut ? da < db : da > db;
        };
        std::sort(indices.begin(), indices.end(), dist_cmp);
    } else {
        auto comparator = [&](size_t a, size_t b) {
            const Vec3 pa = get_position(*items[a]);
            const Vec3 pb = get_position(*items[b]);
            switch (order) {
                case StaggerOrder::LeftToRight:    return pa.x < pb.x;
                case StaggerOrder::RightToLeft:    return pa.x > pb.x;
                case StaggerOrder::TopToBottom:    return pa.y > pb.y;
                case StaggerOrder::BottomToTop:    return pa.y < pb.y;
                case StaggerOrder::DepthNearToFar: return pa.z < pb.z;
                case StaggerOrder::DepthFarToNear: return pa.z > pb.z;
                default: return a < b;
            }
        };
        std::sort(indices.begin(), indices.end(), comparator);
    }

    // Convert to ranks: ranks[original_index] = rank
    std::vector<size_t> ranks(items.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        ranks[indices[i]] = i;
    }
    return ranks;
}

} // namespace detail

// ── Stagger for Layers ─────────────────────────────────────────────────────

inline void stagger_layers(
    std::pmr::vector<Layer>& layers,
    const StaggerConfig& config,
    StaggerOrder order = StaggerOrder::LeftToRight
) {
    std::vector<Layer*> ptrs;
    ptrs.reserve(layers.size());
    for (auto& l : layers) ptrs.push_back(&l);

    auto ranks = detail::compute_stagger_ranks_impl(
        ptrs, order,
        [](const Layer& l) { return l.transform.position; },
        config
    );

    for (size_t i = 0; i < layers.size(); ++i) {
        Frame delay = compute_stagger_delay(ranks[i], layers.size(), config);
        layers[i].anim_transform.shift(delay);
    }
}

// ── Stagger for MotionObjects ──────────────────────────────────────────────

inline void stagger_motion_objects(
    std::vector<presets::motion::MotionObject*>& objects,
    const StaggerConfig& config,
    StaggerOrder order = StaggerOrder::LeftToRight
) {
    auto ranks = detail::compute_stagger_ranks_impl(
        objects, order,
        [](const presets::motion::MotionObject& obj) { return obj.position_value; },
        config
    );

    for (size_t i = 0; i < objects.size(); ++i) {
        Frame delay = compute_stagger_delay(ranks[i], objects.size(), config);
        objects[i]->shift(delay);
    }
}

// ── Convenience: stagger a subset of layers by name ─────────────────────────

inline void stagger_named_layers(
    std::pmr::vector<Layer>& layers,
    const std::vector<std::string>& names,
    const StaggerConfig& config,
    StaggerOrder order = StaggerOrder::LeftToRight
) {
    std::vector<Layer*> ptrs;
    for (auto& l : layers) {
        for (const auto& n : names) {
            if (l.name == n.c_str()) { ptrs.push_back(&l); break; }
        }
    }
    auto ranks = detail::compute_stagger_ranks_impl(
        ptrs, order,
        [](const Layer& l) { return l.transform.position; },
        config
    );
    for (size_t i = 0; i < ptrs.size(); ++i) {
        Frame delay = compute_stagger_delay(ranks[i], ptrs.size(), config);
        ptrs[i]->anim_transform.shift(delay);
    }
}

} // namespace chronon3d
