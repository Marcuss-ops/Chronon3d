#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// test_cache_policy_contract_meta.hpp
//
// Meta-header for the cache-policy contract test (see
// `tests/render_graph/executor/test_cache_policy_contract.cpp`).
//
// The contract test asserts by COMPILE-TIME SFINAE PROBE that
// `RenderGraphNode` does NOT expose:
//   - a policy-mutation setter (the literal `set_cache_policy`)
//   - a cacheable-look virtual accessor (the literal `cacheable()`)
//
// Probing the absence of those symbols requires the probe source itself
// to mention them by name.  This file is the ONLY legal place where
// those identifier literals appear in source code; the closure grep
// exempts it with:
//
//     grep -rnE 'set_cache_policy|\\.cacheable\\(\\)|->cacheable\\(\\)|bool cacheable\\(' \\
//          include src tests --exclude=test_cache_policy_contract_meta.hpp
//
// Any reintroduction of the legacy identifiers anywhere else will be caught
// by the closure grep, which is the audit mechanism that guarantees the
// contract below this header holds.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/core/cache_policy.hpp>

#include <concepts>
#include <string>
#include <type_traits>
#include <utility>

namespace chronon3d::graph::contract_probe {

// SFINAE probe: detects whether `T` exposes a policy-mutation method.
// The literal `set_cache_policy` is intentionally referenced here — the
// concept `false` when absent and `true` when present, so the
// `static_assert(!HasPolicyMutationSetter<T>)` checks the absence.
template <typename T>
concept HasPolicyMutationSetter = requires(T& node, RenderNodeCachePolicy policy) {
    node.set_cache_policy(std::move(policy));
};

// SFINAE probe: detects whether `T` exposes a cacheable-look virtual.
// Same intent — literal `cacheable()` is referenced here only.
template <typename T>
concept HasCacheableVirtualProbe = requires(const T& node) {
    { node.cacheable() } -> std::convertible_to<bool>;
};

} // namespace chronon3d::graph::contract_probe

// Neutral diagnostic strings for the contract test's static_assert
// failures.  Do NOT include legacy-API identifier literals here.
#define CHRONON3D_POLICY_READONLY_HINT  "policy must be fixed at construction"
#define CHRONON3D_CACHEABLE_LOOK_HINT   "no cacheable-look virtual accessor"
