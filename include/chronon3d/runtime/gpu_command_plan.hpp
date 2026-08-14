#pragma once

// ---------------------------------------------------------------------------
// runtime/gpu_command_plan.hpp
//
// Backend-neutral GPU command plan.  The graph compiler emits one CommandPlan
// per frame:
//
//   PassPlan     — the ordered list of GPU passes (what to run)
//   ResourcePlan — which logical surfaces alias which physical slots
//   BarrierPlan  — where each surface transitions between read/write access
//
// The Vulkan backend consumes a CommandPlan to batch every pass of a frame
// into a single submission instead of one operation-per-submit.  No Vulkan
// type leaks into this contract; surfaces are referenced by opaque
// RenderSurfaceHandle only.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_plan.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace chronon3d::runtime {

enum class GpuPassKind : std::uint8_t {
    Composite = 0,
    Transform = 1,
    AffineTransform = 2,
    Blur = 3,
    Glow = 4,
    ColorAdjust = 5,
    Matte = 6,
};

// ── Per-kind pass payloads ────────────────────────────────────────────────
// Each references logical surfaces by opaque handle and carries only the
// scalar/vector parameters the kernel needs.

struct CompositePass {
    RenderSurfaceHandle destination{kInvalidRenderSurfaceHandle};
    RenderSurfaceHandle source{kInvalidRenderSurfaceHandle};
    std::int32_t blend_mode{0};  // 0 = Normal, 1 = Add
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

using GpuPassParams = std::variant<
    CompositePass, TransformPass, AffineTransformPass, BlurPass,
    GlowPass, ColorAdjustPass, MattePass>;

struct GpuPass {
    GpuPassKind kind{GpuPassKind::Composite};
    GpuPassParams params{CompositePass{}};
};

struct PassPlan {
    std::vector<GpuPass> passes;
    [[nodiscard]] std::size_t size() const noexcept { return passes.size(); }
};

enum class ResourceAccess : std::uint8_t {
    Read,
    Write,
    ReadWrite,
};

struct BarrierTransition {
    std::size_t pass_index{0};
    RenderSurfaceHandle surface{kInvalidRenderSurfaceHandle};
    ResourceAccess access{ResourceAccess::ReadWrite};
};

struct BarrierPlan {
    std::vector<BarrierTransition> transitions;
    [[nodiscard]] std::size_t size() const noexcept { return transitions.size(); }
};

/// The frame-level plan the graph compiler emits and the backend consumes.
struct CommandPlan {
    PassPlan passes;
    ResourcePlan resources;
    BarrierPlan barriers;

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

inline RenderSurfaceHandle destination_of(const CompositePass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const TransformPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const AffineTransformPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const BlurPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const GlowPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const ColorAdjustPass& p) { return p.destination; }
inline RenderSurfaceHandle destination_of(const MattePass& p) { return p.destination; }

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

} // namespace detail

/// Deterministic builder that accumulates GPU passes and derives the resource
/// and barrier plans.  Callers declare each logical surface once, then append
/// passes in execution order; build() aliases transient surfaces with
/// non-overlapping lifetimes via the canonical ResourcePlanner.
class GpuCommandPlanner {
public:
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
            request.kind = ResourceKind::Color;
            request.bytes = desc.bytes;
            request.lifetime = LifetimeClass::FrameTransient;
            request.first = liveness.first;
            request.last = liveness.last;
            request.alignment = desc.alignment;
            request.desc = desc;
            request.surface = handle;
            planner.add(std::move(request));
        }
        plan.resources = planner.build();

        for (std::size_t index = 0; index < m_passes.size(); ++index) {
            const auto destination = detail::destination_handle(m_passes[index]);
            if (destination == kInvalidRenderSurfaceHandle) continue;
            plan.barriers.transitions.push_back(BarrierTransition{
                index, destination, ResourceAccess::Write});
        }
        return plan;
    }

private:
    struct SurfaceLiveness {
        bool seen{false};
        std::size_t first{0};
        std::size_t last{0};
    };

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
    std::unordered_map<RenderSurfaceHandle, ResourceDesc> m_descs;
    std::unordered_map<RenderSurfaceHandle, SurfaceLiveness> m_liveness;
};

/// Propagate a ResourcePlan's physical-slot assignments onto the surface
/// registry.  This is the bridge the backend consumes for memory aliasing:
/// two transient surfaces whose lifetimes never overlap share a physical
/// slot, and the backend can back them with the same device memory.  The
/// registry owns identity only; backing storage remains the backend's
/// responsibility.
inline void bind_plan_slots(const ResourcePlan& plan,
                            RenderSurfaceRegistry& registry) {
    for (const auto& allocation : plan.allocations) {
        if (allocation.surface == kInvalidRenderSurfaceHandle) continue;
        if (allocation.physical_slot == std::numeric_limits<std::size_t>::max()) {
            continue;
        }
        registry.bind_physical_slot(allocation.surface, allocation.physical_slot);
    }
}

} // namespace chronon3d::runtime
