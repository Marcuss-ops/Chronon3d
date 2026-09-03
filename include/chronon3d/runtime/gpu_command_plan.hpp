#pragma once

// ---------------------------------------------------------------------------
// runtime/gpu_command_plan.hpp
//
// Backend-neutral GPU command plan. Pass declarations are lowered once into
// canonical ResourceTransition records. Backends only translate those records
// into native synchronization primitives.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_plan.hpp>
#include <chronon3d/runtime/resource_transition.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_table.hpp>
#include <chronon3d/render_graph/pipeline/execution_decision.hpp>

#include <algorithm>
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

enum class YuvExecutionMode : std::uint8_t { Full, Sparse };

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

struct CommandPlan {
    PassPlan passes;
    ResourcePlan resources;
    std::vector<ResourceTransition> transitions;
    std::shared_ptr<const ::chronon3d::graph::FrameParameterTable> dynamic_parameters;
    std::optional<::chronon3d::graph::ExecutionDecision> execution_decision;

    [[nodiscard]] std::size_t pass_count() const noexcept { return passes.size(); }
};

namespace detail {

struct DesiredAccess {
    RenderSurfaceHandle surface{kInvalidRenderSurfaceHandle};
    bool reads{false};
    bool writes{false};
};

inline void add_access(std::vector<DesiredAccess>& out,
                       RenderSurfaceHandle surface,
                       bool reads,
                       bool writes) {
    if (surface == kInvalidRenderSurfaceHandle) return;
    for (auto& access : out) {
        if (access.surface == surface) {
            access.reads = access.reads || reads;
            access.writes = access.writes || writes;
            return;
        }
    }
    out.push_back(DesiredAccess{surface, reads, writes});
}

inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const CompositePass& p) {
    out.push_back(p.destination); out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const TransformPass& p) {
    out.push_back(p.destination); out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const AffineTransformPass& p) {
    out.push_back(p.destination); out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const BlurPass& p) {
    out.push_back(p.destination); out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const GlowPass& p) {
    out.push_back(p.destination); out.push_back(p.source);
    out.push_back(p.scratch_horizontal); out.push_back(p.scratch_vertical);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const ColorAdjustPass& p) {
    out.push_back(p.destination); out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const MattePass& p) {
    out.push_back(p.destination); out.push_back(p.target); out.push_back(p.matte);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const FusedCompositePass& p) {
    out.push_back(p.destination); out.push_back(p.source);
    for (const auto handle : p.layer_sources) out.push_back(handle);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const LayerBatchPass& p) {
    out.push_back(p.destination);
    for (const auto handle : p.sources) out.push_back(handle);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const ScalePass& p) {
    out.push_back(p.destination); out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const YuvOverlayPass& p) {
    out.push_back(p.destination); out.push_back(p.source);
}
inline void collect_surface_refs(std::vector<RenderSurfaceHandle>& out,
                                 const TextBatchPass& p) {
    out.push_back(p.destination);
    for (const auto handle : p.atlas_pages) out.push_back(handle);
}

inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const CompositePass& p) {
    add_access(out, p.destination, true, true);
    add_access(out, p.source, true, false);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const TransformPass& p) {
    add_access(out, p.destination, false, true);
    add_access(out, p.source, true, false);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const AffineTransformPass& p) {
    add_access(out, p.destination, false, true);
    add_access(out, p.source, true, false);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const BlurPass& p) {
    add_access(out, p.destination, false, true);
    add_access(out, p.source, true, false);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const GlowPass& p) {
    add_access(out, p.destination, true, true);
    add_access(out, p.source, true, false);
    add_access(out, p.scratch_horizontal, false, true);
    add_access(out, p.scratch_vertical, false, true);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const ColorAdjustPass& p) {
    add_access(out, p.destination, false, true);
    add_access(out, p.source, true, false);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const MattePass& p) {
    add_access(out, p.destination, false, true);
    add_access(out, p.target, true, false);
    add_access(out, p.matte, true, false);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const FusedCompositePass& p) {
    add_access(out, p.destination, true, true);
    add_access(out, p.source, true, false);
    for (const auto handle : p.layer_sources) add_access(out, handle, true, false);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const LayerBatchPass& p) {
    add_access(out, p.destination, true, true);
    for (const auto handle : p.sources) add_access(out, handle, true, false);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const ScalePass& p) {
    add_access(out, p.destination, false, true);
    add_access(out, p.source, true, false);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const YuvOverlayPass& p) {
    add_access(out, p.destination, true, true);
    add_access(out, p.source, true, false);
}
inline void collect_accesses(std::vector<DesiredAccess>& out,
                             const TextBatchPass& p) {
    add_access(out, p.destination, true, true);
    for (const auto handle : p.atlas_pages) add_access(out, handle, true, false);
}

inline std::vector<RenderSurfaceHandle> referenced_handles(const GpuPass& pass) {
    std::vector<RenderSurfaceHandle> handles;
    std::visit([&](const auto& params) { collect_surface_refs(handles, params); },
               pass.params);
    return handles;
}

inline std::vector<DesiredAccess> desired_accesses(const GpuPass& pass) {
    std::vector<DesiredAccess> accesses;
    std::visit([&](const auto& params) { collect_accesses(accesses, params); },
               pass.params);
    return accesses;
}

} // namespace detail

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
        declare_surface(handle, std::move(desc), std::nullopt, std::nullopt);
    }

    /// Declare an imported/external surface with explicit boundary states.
    /// These states are consumed by the canonical ResourceStateTracker; the
    /// backend receives only the resulting ResourceTransition stream.
    void declare_surface(
        RenderSurfaceHandle handle,
        ResourceDesc desc,
        std::optional<ResourceState> initial_state,
        std::optional<ResourceState> final_state) {
        m_descs[handle] = std::move(desc);
        m_boundary_states[handle] = SurfaceBoundaryState{
            std::move(initial_state), std::move(final_state)};
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

        // ResourceId is the stable ResourcePlan request index. Sort handles so
        // unordered-map iteration can never perturb the compiled sync stream.
        std::vector<RenderSurfaceHandle> handles;
        handles.reserve(m_liveness.size());
        for (const auto& [handle, unused] : m_liveness) {
            (void)unused;
            handles.push_back(handle);
        }
        std::sort(handles.begin(), handles.end());

        ResourcePlanner planner;
        for (const auto handle : handles) {
            const auto live_it = m_liveness.find(handle);
            const auto desc_it = m_descs.find(handle);
            if (live_it == m_liveness.end() || desc_it == m_descs.end()) continue;
            planner.add(ResourceRequest{
                "surface:" + std::to_string(handle),
                desc_it->second,
                live_it->second.first,
                live_it->second.last,
                handle});
        }
        plan.resources = planner.build();
        plan.dynamic_parameters = m_dynamic_parameters;
        plan.execution_decision = m_execution_decision;
        build_transitions(plan);
        return plan;
    }

private:
    struct SurfaceLiveness {
        bool seen{false};
        std::size_t first{0};
        std::size_t last{0};
    };

    struct SurfaceBoundaryState {
        std::optional<ResourceState> initial{};
        std::optional<ResourceState> final{};
    };

    static std::optional<ResourceId> resource_for_surface(
        const ResourcePlan& resources,
        RenderSurfaceHandle surface) noexcept {
        for (std::size_t i = 0; i < resources.requests.size(); ++i) {
            if (resources.requests[i].surface == surface) {
                return static_cast<ResourceId>(i);
            }
        }
        return std::nullopt;
    }

    static std::vector<ResourceRange> canonical_ranges(
        const ResourceDesc& desc) {
        if (desc.kind == ResourceKind::Bytes) {
            return {whole_range()};
        }
        if (desc.format.pixel == PixelFormat::Nv12 ||
            desc.format.pixel == PixelFormat::P010) {
            return {
                image_range(ResourceAspect::Plane0),
                image_range(ResourceAspect::Plane1)};
        }
        if (desc.kind == ResourceKind::Depth) {
            return {image_range(ResourceAspect::Depth)};
        }
        return {image_range(ResourceAspect::Color)};
    }

    static ResourceState state_for_range(ResourceState state,
                                         const ResourceRange& range) noexcept {
        if (const auto* image = std::get_if<SubresourceRange>(&range)) {
            state.range = *image;
        }
        return state;
    }

    static bool has_alias_predecessor(const ResourcePlan& resources,
                                      ResourceId resource) noexcept {
        const auto* allocation = resources.allocation_for(resource);
        if (!allocation ||
            allocation->physical_slot == std::numeric_limits<std::size_t>::max()) {
            return false;
        }
        const auto& current = resources.requests[resource];
        for (std::size_t i = 0; i < resources.allocations.size(); ++i) {
            if (i == resource) continue;
            const auto& other_allocation = resources.allocations[i];
            if (other_allocation.physical_slot != allocation->physical_slot) continue;
            if (resources.requests[i].last < current.first) return true;
        }
        return false;
    }

    void seed_boundary_states(const ResourcePlan& resources,
                              ResourceStateTracker& tracker) const {
        for (const auto& [surface, boundary] : m_boundary_states) {
            if (!boundary.initial.has_value()) continue;
            const auto resource = resource_for_surface(resources, surface);
            if (!resource.has_value()) continue;
            const auto& request = resources.requests[*resource];
            for (const auto& range : canonical_ranges(request.desc)) {
                tracker.seed_state(
                    *resource,
                    range,
                    state_for_range(*boundary.initial, range),
                    request.first);
            }
        }
    }

    void finalize_boundary_states(const ResourcePlan& resources,
                                  ResourceStateTracker& tracker) const {
        const auto boundary_pass = m_passes.size();
        for (const auto& [surface, boundary] : m_boundary_states) {
            if (!boundary.final.has_value()) continue;
            const auto resource = resource_for_surface(resources, surface);
            if (!resource.has_value()) continue;
            const auto& request = resources.requests[*resource];
            for (const auto& range : canonical_ranges(request.desc)) {
                ResourceUse use{
                    *resource,
                    UsageIntent::HostRead,
                    range,
                    false};
                tracker.apply_use(
                    boundary_pass,
                    use,
                    state_for_range(*boundary.final, range));
            }
        }
    }

    void build_transitions(CommandPlan& plan) const {
        ResourceStateResolver resolver;
        ResourceStateTracker tracker;
        seed_boundary_states(plan.resources, tracker);

        for (std::size_t pass_index = 0; pass_index < m_passes.size(); ++pass_index) {
            for (const auto& access : detail::desired_accesses(m_passes[pass_index])) {
                const auto resource = resource_for_surface(plan.resources, access.surface);
                // Undeclared surfaces are explicitly outside the compiled
                // resource table and stay on the backend's unplanned boundary.
                if (!resource.has_value()) continue;

                UsageIntent intent = UsageIntent::StorageRead;
                if (access.reads && access.writes) intent = UsageIntent::StorageReadWrite;
                else if (access.writes) intent = UsageIntent::StorageWrite;

                const auto& request = plan.resources.requests[*resource];
                const bool alias_first_use =
                    pass_index == request.first &&
                    has_alias_predecessor(plan.resources, *resource);

                for (const auto& range : canonical_ranges(request.desc)) {
                    ResourceUse use{*resource, intent, range, false};
                    auto state = state_for_range(
                        resolver.resolve(intent, request.desc.kind), range);
                    if (alias_first_use) {
                        tracker.apply_alias_boundary(
                            pass_index, *resource, range, state);
                    } else {
                        tracker.apply_use(pass_index, use, state);
                    }
                }
            }
        }

        finalize_boundary_states(plan.resources, tracker);
        plan.transitions = tracker.transitions();
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
    std::unordered_map<RenderSurfaceHandle, SurfaceBoundaryState> m_boundary_states;
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
