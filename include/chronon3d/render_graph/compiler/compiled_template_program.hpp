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
#include <chronon3d/render_graph/core/cache_policy.hpp>  // TemporalClass
#include <chronon3d/render_graph/compiler/parameter_ring.hpp>  // Fase D
#include <chronon3d/render_graph/compiler/command_replay.hpp>  // Fase E

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace chronon3d::graph {

// ── TemporalCapabilities (Fase B) ──────────────────────────────────────────
//
/// Declared by each processor or derived from node metadata at compile time.
/// The temporal analysis pass propagates these through the graph to produce
/// a per-node TemporalClass.
struct TemporalCapabilities {
    bool content_depends_on_time{false};
    bool transform_depends_on_time{false};
    bool parameters_depend_on_time{false};
    bool depends_on_external_frame{false};

    [[nodiscard]] bool fully_static() const noexcept {
        return !content_depends_on_time && !transform_depends_on_time &&
               !parameters_depend_on_time && !depends_on_external_frame;
    }
};

/// Per-node temporal classification, stored once in the compiled template.
struct CompiledTemporalInfo {
    GraphNodeId   node{k_invalid_node};
    TemporalClass classification{TemporalClass::Static};
};

/// Result of the temporal analysis pass.
struct TemporalAnalysisResult {
    std::vector<CompiledTemporalInfo> per_node;
    std::size_t static_count{0};
    std::size_t total_count{0};

    [[nodiscard]] TemporalClass classification(GraphNodeId id) const noexcept {
        if (id >= per_node.size()) return TemporalClass::ExternalDynamic;
        return per_node[id].classification;
    }

    [[nodiscard]] bool empty() const noexcept { return per_node.empty(); }
};

// ── Fase C — physical resource types ────────────────────────────────────────

/// Device-level memory budget for preflight VRAM admission control.
struct DeviceMemoryPlan {
    std::uint64_t persistent_assets{0};   // textures uploaded once
    std::uint64_t baked_surfaces{0};      // static island outputs
    std::uint64_t physical_slots{0};      // aliased transient resources
    std::uint64_t frame_slot_buffers{0};  // per-frame-slot ring buffers
    std::uint64_t decoder_budget{0};      // NVDEC / hardware decoder
    std::uint64_t encoder_budget{0};      // NVENC / hardware encoder
    std::uint64_t atlas_budget{0};        // glyph atlas
    std::uint64_t scratch_budget{0};      // temporary staging / scratch
    std::uint64_t safety_margin{0};       // driver margin (15% default)
    std::uint64_t estimated_peak{0};      // sum of all categories + margin

    static constexpr double kDefaultSafetyFraction = 0.15;

    [[nodiscard]] bool operator==(const DeviceMemoryPlan&) const noexcept = default;
};

/// A physical slot augmented with byte-level sizing (Fase C).
struct SizedPhysicalSlot {
    std::uint32_t slot_id{0};
    std::size_t   max_width{0};
    std::size_t   max_height{0};
    std::uint64_t surface_bytes{0};
};

/// Aggregated physical resource plan (Fase C — evolved from
/// PhysicalFramebufferAllocationPlan).
struct PhysicalResourcePlan {
    std::vector<SizedPhysicalSlot>  sized_slots;
    std::uint32_t                  slot_count{0};
    std::uint32_t                  peak_live_resources{0};
    std::uint32_t                  aliasable_resources{0};
    std::uint64_t                  peak_transient_bytes{0};
    std::uint32_t                  logical_resources{0};
    std::uint32_t                  excluded_persistent{0};
    std::uint32_t                  excluded_async{0};

    [[nodiscard]] bool empty() const noexcept { return sized_slots.empty(); }
};

/// Admission verdict.
enum class AdmissionVerdict : std::uint8_t {
    Admitted,
    Degraded,
    Rejected,
};

/// Structured result of admission control.
struct AdmissionResult {
    AdmissionVerdict verdict{AdmissionVerdict::Admitted};
    DeviceMemoryPlan  plan;
    std::uint64_t    available_vram{0};
    std::string      diagnostic;

    [[nodiscard]] bool ok() const noexcept {
        return verdict != AdmissionVerdict::Rejected;
    }
};

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

    // ── Fase B — temporal analysis ─────────────────────────────────────
    TemporalAnalysisResult             temporal;

    // ── Fase D — parameter ring ────────────────────────────────────────
    ParameterRingDescriptor            param_ring;

    // ── Fase E — command replay ────────────────────────────────────────
    CommandReplayDescriptor            replay;

    bool                                valid{false};

    // ── Accessors (no copies — delegate to compiled) ───────────────────
    [[nodiscard]] const CompiledResourceTable* resource_plan() const noexcept {
        return compiled ? &compiled->physical_framebuffer_plan : nullptr;
    }

    [[nodiscard]] const CompiledFrameProgram* execution() const noexcept {
        return compiled ? &compiled->program : nullptr;
    }

    [[nodiscard]] bool empty() const noexcept {
        return !valid || !compiled || compiled->empty();
    }
};

// ── PreparedFrameProgram (Fase 4 — static bake in prepare) ──────────────────
//
/// Result of the prepare() phase.  Pre-computes everything the frame loop
/// needs so render(f) is a pure dispatch — no allocation, no shaping,
/// no upload, no bake.  Baked surfaces are produced once in prepare() and
/// reused across all frames.
struct BakedSurfaceHandle {
    std::uint32_t index{0};
    [[nodiscard]] bool valid() const noexcept { return index != 0; }
};

struct PreparedStaticBake {
    std::uint32_t bake_id{0};
    GraphNodeId   root{k_invalid_node};
    BakedSurfaceHandle surface;
    std::vector<GraphNodeId> interior_nodes;  // members that are fully skipped
};

/// Holds everything the prepare() phase produces.  After prepare(), the
/// per-frame render() path never allocates surfaces, never shapes fonts,
/// never uploads glyphs, and never bakes static regions.
struct PreparedFrameProgram {
    /// Mask of nodes to skip during frame execution (interior static nodes
    /// whose output has been pre-baked).
    std::vector<bool> interior_node_skip;

    /// Per-region bake results.  bake_id → prepared region.
    std::unordered_map<std::uint32_t, PreparedStaticBake> baked_regions;

    /// Total interior nodes skipped per frame (telemetry).
    std::size_t skipped_interior_nodes{0};

    bool valid{false};
};

/// Pre-compute everything that can be resolved before the first frame.
/// After this call, render(f) does zero allocation, zero shaping, and
/// zero bake — pure dispatch.
///
/// The caller owns the returned program and must keep it alive for the
/// lifetime of the frame loop.
///
/// For Phase 4 the implementation bakes maximal static islands and
/// pre-resolves surface residency.  Font shaping / glyph upload / descriptor
/// table preallocation land in subsequent phases.
[[nodiscard]] PreparedFrameProgram prepare(const CompiledTemplateProgram& program);

/// Phase 5 — pre-allocate every physical GPU surface from the compiled
/// interval-coloring plan.  Must be called once after prepare() and before
/// the first frame.  After this call:
///
///   vkCreateImage / frame       = 0
///   vkCreateImageView / frame   = 0
///   vkAllocateMemory / frame    = 0
///
/// The pool-based fallback stays alive for unplanned surfaces.
void preallocate_surfaces(const CompiledTemplateProgram& program,
                          RenderBackend* backend,
                          std::uint32_t canvas_width,
                          std::uint32_t canvas_height);

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

// ── Fase B — temporal analysis entry points ─────────────────────────────────
//
/// Topological temporal classification.  The rule: a node is Static when its
/// own state is static AND all its inputs are static.  Derived classes are
/// ranked (TransformDynamic < ParameterDynamic < ContentDynamic <
/// ExternalDynamic) and propagated along the graph.
[[nodiscard]] TemporalAnalysisResult classify_temporal(
    const CompiledFrameGraph& compiled);

/// Maximal static island discovery.  Walks the compiled levels top-down,
/// merges maximal connected Static regions into a single StaticBakeRegion
/// (BakeResourceId), and returns the discovered regions.
[[nodiscard]] std::vector<StaticBakeRegion> bake_maximal_static_islands(
    const CompiledFrameGraph& compiled,
    const TemporalAnalysisResult& temporal);

/// Merge islands that are contiguous in execution order when their union
/// stays static.  Conservative: only merges when all inputs of the child
/// island are within the same region or already static.
[[nodiscard]] std::vector<StaticBakeRegion> merge_contiguous_static_regions(
    std::vector<StaticBakeRegion> regions,
    const CompiledFrameGraph& compiled,
    const TemporalAnalysisResult& temporal);

// ── Fase C — physical resource analysis entry points ────────────────────────
//
/// Lift the deterministic interval-coloring plan into a byte-level resource
/// plan.  `bytes_per_pixel` defaults to 16 (RGBA32F).
[[nodiscard]] PhysicalResourcePlan analyze_physical_resources(
    const CompiledFrameGraph& compiled,
    std::uint32_t bytes_per_pixel = 16);

/// Build a device memory plan from template-level analysis.
[[nodiscard]] DeviceMemoryPlan build_device_memory_plan(
    const PhysicalResourcePlan& resources,
    std::uint64_t decoder_budget = 0,
    std::uint64_t encoder_budget = 0,
    std::uint64_t atlas_budget = 0,
    std::uint64_t scratch_budget = 0);

/// Admission control: reject or degrade the job when the estimated peak
/// exceeds `available_vram`.
[[nodiscard]] AdmissionResult admit_or_degrade_job(
    const DeviceMemoryPlan& plan,
    std::uint64_t available_vram);

} // namespace chronon3d::graph

// ── Fase C: physical resource analysis + admission ─────────────────────────
// (separate anonymous block — these types land in chronon3d::graph above)
// See forward declarations + full definitions in the block below the
// hash specialization.

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
