#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// compiled_template_program.hpp — Fase A: CompiledTemplateProgram ABI surface
//
// A CompiledTemplateProgram is the resolution-independent, cacheable compiled
// form of a RenderGraph.  The RenderGraph is the AST source-language; the
// FrameGraphCompiler produces a CompiledFrameGraph; the template lift
// (`compile_template_program`) packages it into a CompiledTemplateProgram
// with fingerprint, parameter schema, resource manifest, static-region/batch
// lists, materialization boundaries, and accessors for the backing resource
// plan and execution schedule.
//
// Fase A composes the EXISTING compiled types (CompiledFrameGraph,
// PhysicalFramebufferAllocationPlan, CompiledFrameProgram, CompiledLayerBatch)
// instead of duplicating them.  The single source of truth is a
// std::shared_ptr<const CompiledFrameGraph>.
//
// Ticket: TICKET-VIDEO-COMPILER-ARCH-V1 §Fase A
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::graph {

// ── ProgramFingerprint ──────────────────────────────────────────────────────
//
/// Identifies a CompiledTemplateProgram across topology, renderer ABI,
/// and quality profile.  Two programs with identical fingerprint MAY
/// share a template cache slot.
struct ProgramFingerprint {
    std::uint64_t topology_hash{0};     // from compute_structure_hash / structure_hash
    std::uint32_t renderer_abi{0};      // renderer ABI version (e.g. kRenderAbiV1)
    std::uint32_t quality_profile{0};   // quality profile tag (Fase A default: 0)

    bool operator==(const ProgramFingerprint&) const noexcept = default;
    bool operator!=(const ProgramFingerprint&) const noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return topology_hash != 0 && renderer_abi != 0;
    }
};

inline constexpr std::uint32_t kRenderAbiV1 = 1;
inline constexpr std::uint32_t kQualityProfileDefault = 0;

// ── ParameterSchema ─────────────────────────────────────────────────────────
//
/// One parameter entry, derived from CompiledOperation's per-node
/// parameter_offset / parameter_size.  The schema is a sparse index:
/// not every node may carry a parameter block.
struct ParameterSchemaEntry {
    GraphNodeId  node{k_invalid_node};
    StableNodeId stable_node{kInvalidStableNodeId};
    std::uint32_t parameter_offset{0};
    std::uint32_t parameter_size{0};
};

/// Immutable-after-compile parameter footprint, derived from the compiled
/// frame program's operations and prepared parameter table.
struct ParameterSchema {
    std::vector<ParameterSchemaEntry> entries;
    std::size_t total_bytes{0};          // sum of all parameter_sizes
    std::size_t frame_count{0};          // how many frames are pre-sampled
    bool has_prepared_parameters{false}; // derived from prepared_parameters != nullptr
    bool fully_recorded{false};          // mirrored from CompiledFrameProgram

    [[nodiscard]] bool empty() const noexcept { return entries.empty(); }
};

// ── ResourceManifest ────────────────────────────────────────────────────────
//
/// Describes the kinds of external resources referenced by the program.
/// Fase A classifies at a coarse granularity (Image / Text / Video / Font /
/// Atlas / Other).  Phase I (PixelDomain inference) may refine kinds.
enum class ResourceKind : std::uint8_t {
    Image = 0,
    Text,
    Video,
    Font,
    Atlas,
    Other,
};

/// One resource binding entry, keyed by the owning node's layer_id or name.
struct ResourceManifestEntry {
    ResourceKind kind{ResourceKind::Other};
    std::string   binding_id;   // layer_id or authored resource name
    GraphNodeId   node{k_invalid_node};
};

struct ResourceManifest {
    std::vector<ResourceManifestEntry> entries;

    [[nodiscard]] bool empty() const noexcept { return entries.empty(); }
};

// ── StaticBakeRegion ────────────────────────────────────────────────────────
//
/// Evolved from StaticSubgraphBakePass.  The compiler bakes the largest
/// static island reachable from `root`; `members` lists every node inside
/// that island (Fase A: empty; Phase C fills via discovery).
struct StaticBakeRegion {
    std::uint32_t                  bake_id{0};
    GraphNodeId                    root{k_invalid_node};
    std::vector<GraphNodeId>       members;       // empty in Fase A
    std::uint64_t                  fingerprint{0}; // from static_fingerprint
    bool                           is_baked{false};
};

// ── CompiledGpuBatch (alias) ────────────────────────────────────────────────
//
/// Fase A: identical to CompiledLayerBatch.  The alias documents the
/// semantic distinction (GPU-targeted batches) while avoiding a duplicate
/// struct.  Future phases may replace this alias with a diverging type.
using CompiledGpuBatch = CompiledLayerBatch;

// ── MaterializationBoundary ─────────────────────────────────────────────────
//
/// Marks a graph node whose output MUST be materialized to a physical
/// surface because it crosses a fusion or pixel-domain boundary.
enum class MaterializationBoundaryKind : std::uint8_t {
    Neighborhood,   // blur/DOF: reads surrounding pixels (halo)
    DomainChange,   // pixel-domain conversion required (YUV→RGB / RGB→YUV)
    External,       // external frame/semaphore dependency
};

struct MaterializationBoundary {
    GraphNodeId                    node{k_invalid_node};
    MaterializationBoundaryKind    kind{MaterializationBoundaryKind::Neighborhood};
    std::uint16_t                  halo_radius{0};
};

// ── CompiledTemplateProgram ─────────────────────────────────────────────────
//
/// The true compiled program: resolution-independent, cacheable template
/// built from a CompiledFrameGraph.  The `compiled` member is the single
/// source of truth for the execution schedule (levels + operations), the
/// physical resource plan, and the raw node list.  Template-level metadata
/// (fingerprint, schema, manifest, static regions, GPU batches, boundaries)
/// is lifted at construction time and stored alongside.
struct CompiledTemplateProgram {
    // ── Single source of truth: the compiled frame graph ───────────────
    std::shared_ptr<const CompiledFrameGraph> compiled;

    // ── Template-level metadata ────────────────────────────────────────
    ProgramFingerprint                  fingerprint;
    ParameterSchema                     parameters;
    ResourceManifest                    resources;
    std::vector<StaticBakeRegion>       static_regions;
    std::vector<CompiledGpuBatch>       batches;
    std::vector<MaterializationBoundary> boundaries;

    bool                                valid{false};

    // ── Accessors (no copies — delegate to compiled) ───────────────────
    [[nodiscard]] const PhysicalFramebufferAllocationPlan* resource_plan() const noexcept {
        return compiled ? &compiled->physical_framebuffer_plan : nullptr;
    }

    [[nodiscard]] const CompiledFrameProgram* execution() const noexcept {
        return compiled ? &compiled->program : nullptr;
    }

    [[nodiscard]] bool empty() const noexcept {
        return !valid || !compiled || compiled->empty();
    }
};

// ── Template program derivation ─────────────────────────────────────────────
//
/// Lifts a compiled frame graph into a CompiledTemplateProgram.
/// `compiled` is moved into a shared_ptr — the caller's CompiledFrameGraph
/// is consumed (move semantics).  RenderGraph = AST source-language,
/// CompiledFrameGraph = intermediate representation, CompiledTemplateProgram
/// = the true compiled, cacheable template.
///
/// The derivation populates:
///  - fingerprint from structure_hash + kRenderAbiV1
///  - parameters from operations' parameter_offset/size
///  - resources from active binding_meta entries
///  - static_regions from static_bakes
///  - batches from layer_batches
///  - boundaries: empty (Fase G populates via fusion analysis)
[[nodiscard]] CompiledTemplateProgram
compile_template_program(CompiledFrameGraph compiled);

} // namespace chronon3d::graph

// ── std::hash<ProgramFingerprint> ───────────────────────────────────────────
namespace std {
template <>
struct hash<chronon3d::graph::ProgramFingerprint> {
    std::size_t operator()(const chronon3d::graph::ProgramFingerprint& fp) const noexcept {
        std::size_t h = 1469598103934665603ULL;  // FNV-1a offset basis
        auto combine = [&](std::uint64_t v) {
            h ^= static_cast<std::size_t>(v);
            h *= 1099511628211ULL;
        };
        combine(fp.topology_hash);
        combine(fp.renderer_abi);
        combine(fp.quality_profile);
        return h;
    }
};
} // namespace std