#pragma once

// ============================================================================
// graph_node_create_request.hpp — Parameterized node creation payloads.
//
// Each node type that requires construction parameters defines a spec struct.
// GraphNodeCreateRequest holds a variant of all known specs.
//
// New spec types are added here when a new parameterized node is registered.
// ============================================================================

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <string>
#include <variant>

namespace chronon3d::graph {

/// Specification for creating a PrecompNode.
struct PrecompNodeCreateSpec {
    std::string composition_name;
    Frame start_frame{0};
    Frame duration{-1};
    Frame cache_frame{-1};

    size_t cache_capacity{8};
    cache::TuneMode tune_mode{cache::TuneMode::Fixed};
    size_t tune_interval{30};
    size_t tune_min_capacity{2};
    size_t tune_max_capacity{128};
};

/// Variant of all supported node creation payloads.
/// Use std::get_if<T>(&payload) to extract a specific spec.
using GraphNodeCreatePayload = std::variant<
    std::monostate,         // fallback for no-parameter factories
    PrecompNodeCreateSpec
>;

/// Request for creating a graph node with optional typed parameters.
struct GraphNodeCreateRequest {
    GraphNodeCreatePayload payload;
};

} // namespace chronon3d::graph
