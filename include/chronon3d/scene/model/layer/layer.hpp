#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/animation/effects/animated_transform.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/layer/mask.hpp>
// effect_stack.hpp, material_2_5d.hpp, card3d_material.hpp are NOT included.
// Forward-declared below so layer.hpp stays decoupled from feature headers.
#include <chronon3d/scene/model/core/depth_role.hpp>
#include <chronon3d/scene/model/layer/track_matte.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <chronon3d/scene/model/layer/time_remap.hpp>
#include <chronon3d/scene/model/layer/layer_time_resolver.hpp>
#include <chronon3d/assets/asset_manifest.hpp>
// material_2_5d.hpp  — forward-declared
// card3d_material.hpp — forward-declared
#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <string>
#include <vector>
#include <memory>
#include <memory_resource>

namespace chronon3d {

struct EffectStack;
struct Material2_5D;
struct Card3DMaterial;

namespace video { struct VideoSource; }

enum class LayerKind {
    Normal,       // standard layer: draws its own content, then effects applied
    Adjustment,   // no own content: effects applied to everything rendered below it
    Null,         // no rendering at all; useful as a parent for transform hierarchy
    Precomp,      // references a nested composition by name
    Video,        // plays a video file via VideoNode
    Glass,        // frosted glass: blurs background within shapes, then draws content
    // ── P2 primitives (typed kind axis; additive) ───────────────────
    Shape,        // raster / vector shape primitive (formerly implicit via Normal)
    Text,         // text-run / animated-text primitive
    Camera,       // dedicated camera rig layer (drivers apply to the render graph)
    Audio         // audio-only layer (no visual; pre-warm hooks audio implicitly)
};

struct Layer {
    std::pmr::string name;
    std::pmr::string parent_name;
    LayerKind kind{LayerKind::Normal};
    Transform transform{};

    // ── P2: typed-kind predicates (delegate to `kind`) ──────────────
    // Additive-only: each is a pure inline bool based on the enum.
    // Use these instead of downstream string/structural checks.
    [[nodiscard]] constexpr bool is_shape()  const noexcept { return kind == LayerKind::Shape;  }
    [[nodiscard]] constexpr bool is_text()   const noexcept { return kind == LayerKind::Text;   }
    [[nodiscard]] constexpr bool is_camera() const noexcept { return kind == LayerKind::Camera; }
    [[nodiscard]] constexpr bool is_audio()  const noexcept { return kind == LayerKind::Audio;  }
    AnimatedTransform anim_transform{};
    Frame from{0};
    Frame duration{-1};
    Frame time_offset{0};
    TimeRemap time_remap{};
    bool      visible{true};
    bool      uses_2_5d_projection{false};
    bool      hierarchy_resolved{false};
    bool      cache_static{false};
    Mask      mask{};
    std::unique_ptr<EffectStack> m_effects;     // ordered effect stack
    BlendMode blend_mode{BlendMode::Normal};
    CompositeOperator composite_operator{CompositeOperator::SourceOver};
    DepthRole   depth_role{DepthRole::None};
    f32         depth_offset{0.0f};
    LayoutRules layout{};
    TrackMatte  track_matte{};
    std::unique_ptr<Material2_5D> m_material{};
    std::unique_ptr<Card3DMaterial> m_card3d_material{};
    LayerTransitionSpec transition_in{};
    LayerTransitionSpec transition_out{};
    std::pmr::vector<RenderNode> nodes;
    std::pmr::string precomp_composition_name; // for LayerKind::Precomp

    // Video source parameters (isolated from FFmpeg/Backend headers)
    std::unique_ptr<video::VideoSource> video_source;

    // FontEngine pointer for precise text shaping / glyph metrics.
    // Inherited from LayerBuilder when the layer is built.
    FontEngine* font_engine{nullptr};

    // Asset manifest: collected during build, consumed by preflight.
    assets::AssetManifest asset_manifest;

    // Cache for incremental scene fingerprinting
    mutable uint64_t m_static_hash{0};
    mutable bool m_static_hash_computed{false};

    [[nodiscard]] uint64_t get_static_hash() const;

    // ── accessors for pimpl'd members ──────────────────────────────────
    EffectStack&       effects()           { return *m_effects; }
    const EffectStack& effects()     const { return *m_effects; }
    Material2_5D&      material()          { return *m_material; }
    const Material2_5D& material()   const { return *m_material; }
    Card3DMaterial&    card3d_material()   { return *m_card3d_material; }
    const Card3DMaterial& card3d_material() const { return *m_card3d_material; }

    explicit Layer(std::pmr::memory_resource* res = std::pmr::get_default_resource());
    ~Layer();
    
    // Custom copy/move for unique_ptr
    Layer(const Layer& other);
    Layer& operator=(const Layer& other);
    Layer(Layer&& other) noexcept;
    Layer& operator=(Layer&& other) noexcept;

    [[nodiscard]] bool active_at(Frame frame) const {
        if (!visible) return false;
        if (frame < from) return false;
        if (duration < 0) return true;
        return frame < from + duration;
    }

    /// Canonical sub-frame aware local time.
    [[nodiscard]] SampleTime local_time(SampleTime global_time) const {
        return LayerTimeResolver::resolve(global_time, from, time_offset, duration, time_remap);
    }

    /// Legacy integer-frame local time (backward-compatible adapter).
    /// The frame-rate defaults to 30 fps only to preserve existing callers;
    /// new code should use local_time(SampleTime) with an explicit FrameRate.
    [[deprecated("Use local_time(SampleTime)")]]
    [[nodiscard]] Frame local_frame(
        Frame global_frame,
        FrameRate rate = FrameRate{30, 1}) const
    {
        return local_time(SampleTime::from_frame_int(global_frame, rate))
            .integral_frame();
    }
};

} // namespace chronon3d
