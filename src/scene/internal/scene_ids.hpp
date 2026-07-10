// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/scene/internal/scene_ids.hpp
//
// P1-#8 — primitive obsession: typed scene IDs.
//
// Mirrors the canonical `StableNodeId` strong-type pattern from
// `<chronon3d/render_graph/core/node_identity.hpp>` (WP 4.0): each ID
// wraps a single `std::uint64_t` exposed via `.value`; implicit
// conversion FROM `std::uint64_t` is allowed so existing aggregate
// literal init sites (`LayerID{42}`) keep compiling while the compiler
// refuses cross-typing at the call site of any typed factory.
//
// Hash-canonical: each new ID has a `std::hash<>` specialization that
// funnels through `std::hash<std::uint64_t>`.  O(1) hash, no allocation,
// drop-in for `std::unordered_map<LayerID, T>` keys.
//
// Internal-to-lib — NEVER included via `<chronon3d/scene/...>`.  Lives in
// `src/scene/internal/` so .cpp files reach it via relative
// `#include "../internal/scene_ids.hpp"`.  AGENTS.md v0.1 Cat-3 freeze:
// this header does NOT expand the public ABI surface (no file under
// `include/chronon3d/scene/`).
//
// Taxonomy:
//   LayerID       — stable positional index of a Layer in a Scene
//                   (string-name lookup → LayerID, used internally).
//   NodeID        — alias of `chronon3d::graph::StableNodeId` (WP-4.0).
//                   Reuse of the existing render-graph ID — single source
//                   of truth for compiled-node identity.
//   InternalAssetRef — pre-existing 4-field POD struct in
//                      `<chronon3d/assets/asset_readiness_v2.hpp>` (M1.7,
//                      Phase A2). NOT redefined here; cross-referenced
//                      for completeness. Phase A2 renamed the old
//                      `v2::AssetRef` POD to `assets::InternalAssetRef`
//                      and flattened the `v2::` sub-namespace.
//   MediaRef      — typed wrapper for media clip references (video / audio).
//                   Pre-P1-#8 sites held raw `std::string` paths; future
//                   P1-FU commits migrate `video_path / audio_path` to
//                   this typed slot.
//   CameraClipID  — typed wrapper for camera clip identifiers.
//                   Pre-P1-#8 sites held `std::string` rule names; future
//                   P1-FU commits migrate `camera_clip.m_rules_centered[].name`
//                   strings to this typed slot.
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <compare>
#include <cstdint>
#include <cstddef>
#include <functional>

// NodeID reuses the existing render-graph identity — single source of truth.
#include <chronon3d/render_graph/core/node_identity.hpp>

// InternalAssetRef lives in M1.7 / Phase-A2 canonical header
// (asset_readiness_v2.hpp) — not redefined here; cross-referenced for
// type-taxonomy completeness.
//
//   chronon3d::assets::InternalAssetRef
//     { AssetKind kind; std::string path; std::string owner; bool required; }
//
// See `<chronon3d/assets/asset_readiness_v2.hpp>` for the canonical
// 4-field definition + AssetManifest aggregation contract. Phase A2
// renamed the old `v2::AssetRef` POD to `assets::InternalAssetRef` and
// flattened the `v2::` sub-namespace.

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// NodeID — alias of StableNodeId (render-graph canonical).
// ═══════════════════════════════════════════════════════════════════════════
//
// Reuses the WP-4.0 strong type.  Scene code that needs "node identity"
// uses `NodeID` (or `chronon3d::graph::StableNodeId` directly) — both
// resolve to the same type, so a scene-resident `unordered_map<NodeID, T>`
// and a render-graph-resident `unordered_map<StableNodeId, T>` are
// interchangeable assignments.  The alias keeps prose consistent across
// scene/render namespaces.

using NodeID = chronon3d::graph::StableNodeId;
// `using new_scope::X = existing_scope::Y` is a clean type alias; the
// underlying type and its `std::hash<StableNodeId>` specialization in
// `<chronon3d/render_graph/core/node_identity.hpp>` carry over.

} // namespace chronon3d

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// LayerID — typed positional index of a Layer in a Scene.
// ═══════════════════════════════════════════════════════════════════════════
//
// Replace `size_t` value type in scene-resident name → position maps
// (e.g., `build_name_index()` in hierarchy_resolver.cpp) with this
// strongly-typed wrapper.  Implicit conversion from `std::uint64_t`
// allows existing positional-init (`LayerID{layer_position}`) to
// keep building without churn; the strong type prevents accidental
// assignment of a NodeID or AssetRef where a LayerID is expected.
struct LayerID {
    std::uint64_t value{0};
    constexpr LayerID() noexcept = default;
    constexpr explicit(false) LayerID(std::uint64_t v) noexcept : value(v) {}
    constexpr auto operator<=>(const LayerID&) const = default;
    constexpr bool operator==(const LayerID&) const = default;
};

// ═══════════════════════════════════════════════════════════════════════════
// MediaRef — typed wrapper for media clip references (video / audio).
// ═══════════════════════════════════════════════════════════════════════════
//
// Forward-point: pre-P1-#8 sites held `std::string` paths
// (`std::string video_path; std::string audio_path;` in scene-build
// descriptors).  This struct is the canonical "slot identity" for the
// planned P1-FU migration that replaces raw paths with managed
// MediaRefs (see M1.7 Asset Readiness lineage).  Adding the type now
// (purely additive, no caller migration in this commit) means the
// future P1-FU commits only need to swap path → MediaRef at the
// call sites without forwarding declarations.
struct MediaRef {
    std::uint64_t value{0};
    constexpr MediaRef() noexcept = default;
    constexpr explicit(false) MediaRef(std::uint64_t v) noexcept : value(v) {}
    constexpr auto operator<=>(const MediaRef&) const = default;
    constexpr bool operator==(const MediaRef&) const = default;
};

// ═══════════════════════════════════════════════════════════════════════════
// CameraClipID — typed wrapper for camera clip identifiers.
// ═══════════════════════════════════════════════════════════════════════════
//
// Same pattern as `MediaRef` — the canonical "slot identity" for
// camera clips constructor-time registration.  Pre-P1-#8 sites held
// `std::string` rule names (`CameraShotValidator::require_target_centered(
// std::string target_name, …)`); a future P1-FU commit migrates
// these to `CameraClipID`, with a temporary `using CameraClipID =
// std::string` shim if backward-compat must be preserved.
struct CameraClipID {
    std::uint64_t value{0};
    constexpr CameraClipID() noexcept = default;
    constexpr explicit(false) CameraClipID(std::uint64_t v) noexcept : value(v) {}
    constexpr auto operator<=>(const CameraClipID&) const = default;
    constexpr bool operator==(const CameraClipID&) const = default;
};

} // namespace chronon3d

// ═══════════════════════════════════════════════════════════════════════════
// std::hash specializations — drop-in support for std::unordered_map<...> keys.
// ═══════════════════════════════════════════════════════════════════════════
//
// Each specialization funnels through `std::hash<std::uint64_t>` —
// O(1) hash, zero allocation, deterministic across compiler /
// libstdc++ versions (the WP-4.0 lesson that broke the default
// `std::hash` fallback for `StableNodeId` because sfinae gates the
// hash machinery when a struct declares non-trivial ctors).
//
// Copy-constructible, default-constructible, NOT declaring a
// destructor that would break unordered_map's required copyable-hash
// predicate (gcc rejects non-copyable hash specializations).

template <>
struct std::hash<chronon3d::LayerID> {
    std::size_t operator()(const chronon3d::LayerID& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

template <>
struct std::hash<chronon3d::MediaRef> {
    std::size_t operator()(const chronon3d::MediaRef& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

template <>
struct std::hash<chronon3d::CameraClipID> {
    std::size_t operator()(const chronon3d::CameraClipID& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
