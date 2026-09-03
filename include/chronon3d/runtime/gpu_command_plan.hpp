#pragma once

// ---------------------------------------------------------------------------
// runtime/gpu_command_plan.hpp
//
// Backend-neutral GPU command plan. The graph/compiler boundary owns the
// structural plan; backends translate its resource states into native sync.
//
//   PassPlan     — ordered GPU work
//   ResourcePlan — logical-to-physical resource bindings
//   BarrierPlan  — resolved resource-state transitions only
//
// No Vulkan type leaks into this contract.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_plan.hpp>
#include <chronon3d/runtime/resource_state.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_table.hpp>
#include <chronon3d/render_graph/pipeline/execution_decision.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace chronon3d::runtime {

enum class YuvExecutionMode : std::uint8_t {
    Full,
    Sparse,
};

enum class GpuPassKind : std::uint8_t {
    Composite = 0,
    Transform = 1,
    AffineTransform = 2,
    Blur = 3,
    Glow = 4,
    ColorAdjust = 5,
    Matte = 6,
    FusedComposite = 7,
    LayerBatch = 8,
    Scale = 9,
    TextBatch = 10,
    YuvOverlay = 11,
};

struct CompositePass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    std::int32_t blend_mode{0};
};

struct TransformPass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    std::int32_t offset_x{0};
    std::int32_t offset_y{0};
    float opacity{1.0f};
};

struct AffineTransformPass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    SurfaceAffineTransform transform{};
};

struct BlurPass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    float radius{0.0f};
    std::int32_t horizontal{1};
};

struct GlowPass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle scratch_horizontal{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle scratch_vertical{kInvalidRenderSurfaceHandle};
    float radius{0.0f};
    float intensity{0.0f};
    float tint[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

struct ColorAdjustPass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    float brightness{0.0f};
    float contrast{1.0f};
    float tint_amount{0.0f};
    float tint[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

struct MattePass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle target{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle matte{kInvalidRenderSurfaceHandle};
    std::int32_t luma{0};
    std::int32_t inverted{0};
};

struct LayerBatchItem {
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    float dst_x{0.0f}, dst_y{0.0f}, dst_w{0.0f}, dst_h{0.0f};
    float src_u0{0.0f}, src_v0{0.0f}, src_u1{1.0f}, src_v1{1.0f};
    float opacity{1.0f};
    std::uint32_t flags{0};
};

struct LayerBatchPass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    std::vector<RenderSurfaceHandle> sources{};
    std::vector<LayerBatchItem> items{};
};

struct FusedCompositePass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    std::vector<RenderSurfaceHandle> layer_sources{};
};

struct ScalePass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    std::int32_t filter_mode{1};
    float scale_x{1.0f};
    float scale_y{1.0f};
};

struct YuvOverlayPass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    PixelFormat format{PixelFormat::Nv12};
    std::optional<raster::BBox> dirty_region{};
    YuvExecutionMode mode{YuvExecutionMode::Full};
};

struct TextBatchPass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    std::vector<RenderSurfaceHandle> atlas_pages{};
    std::vector<GlyphStatic> glyphs{};
    std::vector<TextRunDynamic> runs{};
};

using GpuPassParams = std::variant<
    CompositePass, TransformPass, AffineTransformPass, BlurPass,
    GlowPass, ColorAdjustPass, MattePass, FusedCompositePass, LayerBatchPass,
    ScalePass, TextBatchPass, YuvOverlayPass>;

struct GpuPass {
    GpuPassKind kind{GpuPassKind::Composite};
    GpuPassParams params{CompositePass{}};
    ::chronon3d::graph::FrameParameterSlice dynamic_parameters{};
};

struct PassPlan {
    std::vector<GpuPass> passes;
    [[nodiscard]] std::size_t size() const noexcept { return passes.size(); }
};

enum class ResourceHazard : std::uint8_t {
    FirstWrite = 0,
    ReadAfterWrite,
    WriteAfterRead,
    WriteAfterWrite,
    ReadWriteHazard,
    StateTransition,
};

/// A barrier is already resolved by planning. The backend translates the two
/// states; it does not rediscover RAW/WAR/WAW from a second access history.
struct BarrierTransition {
    std::size_t pass_index{0};
    RenderSurfaceHandle surface{kInvalidRenderSurfaceHandle};
    ResourceState before{};
    ResourceState after{};
    ResourceHazard hazard{ResourceHazard::StateTransition};
};

struct BarrierPlan {
    std::vector<BarrierTransition> transitions;
    [[nodiscard]] std::size_t size() const noexcept { return transitions.size(); }
};

struct CommandPlan {
    PassPlan passes;
    ResourcePlan resources;
    BarrierPlan barriers;
    std::shared_ptr<const ::chronon3d::graph::FrameParameterTable> dynamic_parameters;
    std::optional<::chronon3d::graph::ExecutionDecision> execution_decision;

    [[nodiscard]] std::size_t pass_count() const noexcept { return passes.size(); }
};

namespace detail {

inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const CompositePass& p) {
    out.push_back(p.destination);
    out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const TransformPass& p) {
    out.push_back(p.destination);
    out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const AffineTransformPass& p) {
    out.push_back(p.destination);
    out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const BlurPass& p) {
    out.push_back(p.destination);
    out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const GlowPass& p) {
    out.push_back(p.destination);
    out.push_back(p.source);
    out.push_back(p.scratch_horizontal);
    out.push_back(p.scratch_vertical);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const ColorAdjustPass& p) {
    out.push_back(p.destination);
    out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const MattePass& p) {
    out.push_back(p.destination);
    out.push_back(p.target);
    out.push_back(p.matte);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const FusedCompositePass& p) {
    out.push_back(p.destination);
    out.push_back(p.source);
    for (const auto& h : p.layer_sources) out.push_back(h);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const LayerBatchPass& p) {
    out.push_back(p.destination);
    for (const auto& h : p.sources) out.push_back(h);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const ScalePass& p) {
    out.push_back(p.destination);
    out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const YuvOverlayPass& p) {
    out.push_back(p.destination);
    out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const TextBatchPass& p) {
    out.push_back(p.destination);
    for (const auto& h : p.atlas_pages) out.push_back(h);
}

inline RenderSurfaceHandle destination_of(const CompositePass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const TransformPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const AffineTransformPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const BlurPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const GlowPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const ColorAdjustPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const MattePass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const FusedCompositePass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const LayerBatchPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const ScalePass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const TextBatchPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const YuvOverlayPass& p) { return p.destination; }

inline std::vector<RenderSurfaceHandle> referenced_handles(const GpuPass& pass) {
    std::vector<RenderSurfaceHandle> handles;
    std::visit([&](const auto& params) { collect_surface_refs(handles, params); },
               pass.params);
    return handles;
}

inline RenderSurfaceHandle destination_handle(const GpuPass& pass) {
    return std::visit([](const auto& params) { return destination_of(params); },
                      pass.params);
}

inline ResourceHazard classify_hazard(const ResourceState& before,
                                      const ResourceState& after) noexcept {
    if (before.undefined()) return ResourceHazard::FirstWrite;
    const bool before_reads = before.reads();
    const bool before_writes = before.writes();
    const bool after_reads = after.reads();
    const bool after_writes = after.writes();
    if (before_writes && after_reads && after_writes) {
        return ResourceHazard::ReadWriteHazard;
    }
    if (before_writes && after_reads) return ResourceHazard::ReadAfterWrite;
    if (before_reads && after_writes) return ResourceHazard::WriteAfterRead;
    if (before_writes && after_writes) return ResourceHazard::WriteAfterWrite;
    return ResourceHazard::StateTransition;
}

inline bool requires_barrier(const ResourceState& before,
                             const ResourceState& after) noexcept {
    // Preserve externally/previously initialized first reads. A first write
    // owns the contents and must establish the initial layout.
    if (before.undefined()) return after.writes();
    if (before.writes() || after.writes()) return true;
    return before.layout != after.layout || before.queue != after.queue ||
           before.range != after.range;
}

inline void merge_state(ResourceState& target, const ResourceState& incoming) noexcept {
    target.stages = target.stages | incoming.stages;
    target.access = target.access | incoming.access;
    if (target.layout == ResourceLayout::Undefined) target.layout = incoming.layout;
    if (target.queue != incoming.queue) target.queue = incoming.queue;
    target.range = incoming.range;
}

} // namespace detail

/// Deterministic GPU plan builder. ResourcePlanner still materializes the
/// ResourcePlan for this legacy command-builder boundary, but barrier hazards
/// are resolved exactly once here and never rediscovered in a backend.
class GpuCommandPlanner {
public:
    void set_execution_decision(
        std::optional<::chronon3d::graph::ExecutionDecision> decision) {
        m_execution_decision = decision;
    }

    void set_dynamic_parameters(
        std::shared_ptr<const ::chronon3d::graph::FrameParameterTable> parameters) {
        m_dynamic_parameters = std::move(parameters);
    }

    void declare_surface(RenderSurfaceHandle handle, ResourceDesc desc) {
        m_descs[handle] = desc;
    }

    void composite(CompositePass pass) { append(GpuPass{GpuPassKind::Composite, std::move(pass)}); }
    void transform(TransformPass pass) { append(GpuPass{GpuPassKind::Transform, std::move(pass)}); }
    void affine(AffineTransformPass pass) { append(GpuPass{GpuPassKind::AffineTransform, std::move(pass)}); }
    void blur(BlurPass pass) { append(GpuPass{GpuPassKind::Blur, std::move(pass)}); }
    void glow(GlowPass pass) { append(GpuPass{GpuPassKind::Glow, std::move(pass)}); }
    void color_adjust(ColorAdjustPass pass) { append(GpuPass{GpuPassKind::ColorAdjust, std::move(pass)}); }
    void matte(MattePass pass) { append(GpuPass{GpuPassKind::Matte, std::move(pass)}); }
    void fused_composite(FusedCompositePass pass) { append(GpuPass{GpuPassKind::FusedComposite, std::move(pass)}); }
    void layer_batch(LayerBatchPass pass) { append(GpuPass{GpuPassKind::LayerBatch, std::move(pass)}); }
    void scale(ScalePass pass) { append(GpuPass{GpuPassKind::Scale, std::move(pass)}); }
    void text_batch(TextBatchPass pass) { append(GpuPass{GpuPassKind::TextBatch, std::move(pass)}); }
    void yuv_overlay(YuvOverlayPass pass) { append(GpuPass{GpuPassKind::YuvOverlay, std::move(pass)}); }

    void bind_last_pass_parameters(::chronon3d::graph::FrameParameterSlice slice) {
        if (!m_passes.empty()) m_passes.back().dynamic_parameters = slice;
    }

    [[nodiscard]] std::size_t pass_count() const noexcept { return m_passes.size(); }

    CommandPlan build() const {
        CommandPlan plan;
        plan.passes.passes = m_passes;

        ResourcePlanner planner;
        for (const auto& [handle, liveness] : m_liveness) {
            const auto desc_it = m_descs.find(handle);
            if (desc_it == m_descs.end()) continue;
            const auto& desc = desc_it->second;

            ResourceRequest request;
            request.id = "surface:" + std::to_string(handle);
            request.desc = desc;
            request.desc.kind = ResourceKind::Color;
            request.first = liveness.first;
            request.last = liveness.last;
            request.surface = handle;
            planner.add(std::move(request));
        }
        plan.resources = planner.build();
        plan.dynamic_parameters = m_dynamic_parameters;
        plan.execution_decision = m_execution_decision;
        build_barrier_plan(plan);
        return plan;
    }

private:
    struct SurfaceLiveness {
        bool seen{false};
        std::size_t first{0};
        std::size_t last{0};
    };

    struct DesiredAccess {
        RenderSurfaceHandle surface{kInvalidRenderSurfaceHandle};
        ResourceState state{};
    };

    static std::size_t physical_slot_for(const ResourcePlan& resources,
                                         RenderSurfaceHandle surface) noexcept {
        for (const auto& allocation : resources.allocations) {
            if (allocation.surface == surface) return allocation.physical_slot;
        }
        return std::numeric_limits<std::size_t>::max();
    }

    static void add_desired(std::vector<DesiredAccess>& accesses,
                            RenderSurfaceHandle surface,
                            const ResourceState& state) {
        for (auto& existing : accesses) {
            if (existing.surface == surface) {
                detail::merge_state(existing.state, state);
                return;
            }
        }
        accesses.push_back(DesiredAccess{surface, state});
    }

    void build_barrier_plan(CommandPlan& plan) const {
        std::unordered_map<std::size_t, ResourceState> physical_states;
        std::unordered_map<std::size_t, RenderSurfaceHandle> physical_owners;
        std::unordered_map<RenderSurfaceHandle, ResourceState> external_states;

        for (std::size_t index = 0; index < m_passes.size(); ++index) {
            const auto& pass = m_passes[index];
            const auto destination = detail::destination_handle(pass);
            const auto references = detail::referenced_handles(pass);
            std::vector<DesiredAccess> desired;
            desired.reserve(references.size());

            // collect_surface_refs always emits destination first. Skip only
            // that occurrence; an in-place source using the same handle must
            // still become a read+write state rather than silently losing its read.
            bool skipped_destination_occurrence = false;
            for (const auto handle : references) {
                if (handle == kInvalidRenderSurfaceHandle) continue;
                if (!skipped_destination_occurrence && handle == destination) {
                    skipped_destination_occurrence = true;
                    continue;
                }
                add_desired(desired, handle, ResourceState::compute_read());
            }
            if (destination != kInvalidRenderSurfaceHandle) {
                add_desired(desired, destination, ResourceState::compute_write());
            }

            for (const auto& access : desired) {
                const auto physical_slot = physical_slot_for(plan.resources, access.surface);
                ResourceState before = ResourceState::undefined_state(access.state.range);

                if (physical_slot != std::numeric_limits<std::size_t>::max()) {
                    const auto owner_it = physical_owners.find(physical_slot);
                    const auto state_it = physical_states.find(physical_slot);
                    if (owner_it != physical_owners.end() &&
                        owner_it->second == access.surface &&
                        state_it != physical_states.end()) {
                        before = state_it->second;
                    }
                    // A physical slot reused by a different logical resource
                    // starts a new logical lifetime. Old contents are not a
                    // valid dependency of the new resource.
                    physical_owners[physical_slot] = access.surface;
                    physical_states[physical_slot] = access.state;
                } else {
                    const auto state_it = external_states.find(access.surface);
                    if (state_it != external_states.end()) before = state_it->second;
                    external_states[access.surface] = access.state;
                }

                if (!detail::requires_barrier(before, access.state)) continue;
                plan.barriers.transitions.push_back(BarrierTransition{
                    index,
                    access.surface,
                    before,
                    access.state,
                    detail::classify_hazard(before, access.state)});
            }
        }
    }

    void append(GpuPass pass) {
        const auto index = m_passes.size();
        for (const auto handle : detail::referenced_handles(pass)) {
            if (handle == kInvalidRenderSurfaceHandle) continue;
            auto& liveness = m_liveness[handle];
            if (!liveness.seen) {
                liveness.seen = true;
                liveness.first = index;
                liveness.last = index;
            } else {
                liveness.last = index;
            }
        }
        m_passes.push_back(std::move(pass));
    }

    std::vector<GpuPass> m_passes;
    std::shared_ptr<const ::chronon3d::graph::FrameParameterTable> m_dynamic_parameters;
    std::optional<::chronon3d::graph::ExecutionDecision> m_execution_decision;
    std::unordered_map<RenderSurfaceHandle, ResourceDesc> m_descs;
    std::unordered_map<RenderSurfaceHandle, SurfaceLiveness> m_liveness;
};

inline void bind_plan_slots(const ResourcePlan& plan,
                            RenderSurfaceRegistry& registry) {
    for (const auto& allocation : plan.allocations) {
        if (allocation.surface == kInvalidRenderSurfaceHandle) continue;
        if (allocation.physical_slot == std::numeric_limits<std::size_t>::max()) continue;
        registry.bind_physical_slot(allocation.surface, allocation.physical_slot);
    }
}

} // namespace chronon3d::runtime
