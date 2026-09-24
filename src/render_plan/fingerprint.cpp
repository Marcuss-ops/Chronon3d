#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/graphics/gradient.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <string_view>
#include <type_traits>
#include <vector>

#include <xxhash.h>

namespace chronon3d::render_plan {
namespace {

class CanonicalPlanWriter {
public:
    explicit CanonicalPlanWriter(std::pmr::memory_resource* resource)
        : bytes_(resource) {
        bytes_.reserve(kInitialScratchBytes);
    }

    void add_bool(bool value) { add_u8(value ? 1U : 0U); }

    template <typename T>
        requires std::is_enum_v<T>
    void add_enum(T value) {
        using U = std::underlying_type_t<T>;
        add_i64(static_cast<std::int64_t>(static_cast<U>(value)));
    }

    void add_frame(Frame value) { add_i64(value.integral()); }

    void add_float(float value) {
        if (value == 0.0F) value = 0.0F;  // canonicalize -0
        std::uint32_t bits = 0;
        if (std::isnan(value)) {
            bits = 0x7fc00000U;
        } else {
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
        }
        add_u32(bits);
    }

    void add_i64(std::int64_t value) {
        add_u64(static_cast<std::uint64_t>(value));
    }

    void add_uint64(std::uint64_t value) { add_u64(value); }

    void add_size(std::size_t value) {
        add_u64(static_cast<std::uint64_t>(value));
    }

    void add_string(std::string_view value) {
        add_size(value.size());
        if (!value.empty()) {
            const auto* first = reinterpret_cast<const std::byte*>(value.data());
            bytes_.insert(bytes_.end(), first, first + value.size());
        }
    }

    [[nodiscard]] std::uint64_t finish() const noexcept {
        return XXH3_64bits(bytes_.data(), bytes_.size());
    }

private:
    static constexpr std::size_t kInitialScratchBytes = 64U * 1024U;

    void add_u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }

    void add_u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            add_u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }

    void add_u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            add_u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }

    std::pmr::vector<std::byte> bytes_;
};

void add_track(CanonicalPlanWriter& out, const AnimationTrackPlan& track) {
    out.add_string(track.property);
    out.add_string(track.easing);
    out.add_size(track.keyframes.size());
    for (const auto& key : track.keyframes) {
        out.add_frame(key.frame);
        out.add_size(key.value.size());
        for (const auto value : key.value) out.add_float(value);
    }
}

template <typename T, typename Add>
void add_optional(CanonicalPlanWriter& out, const std::optional<T>& value, Add add) {
    out.add_bool(value.has_value());
    if (value) add(out, *value);
}

// Cat-3 — the gradient identity field selection/order has ONE seat
// (graphics::write_gradient_identity in <chronon3d/graphics/gradient.hpp>).
// This sink only encodes that sequence into the canonical byte stream; the
// stream is byte-identical to the historical inline lambdas, so the
// "chronon.render-plan.content-fingerprint.v5" contract is unchanged.
struct CanonicalGradientSink {
    CanonicalPlanWriter& out;

    template <typename T>
    void operator()(const T& value) {
        if constexpr (std::is_enum_v<T>) {
            out.add_enum(value);
        } else if constexpr (std::is_same_v<T, chronon3d::Color>) {
            out.add_float(value.r); out.add_float(value.g);
            out.add_float(value.b); out.add_float(value.a);
        } else if constexpr (std::is_same_v<T, chronon3d::Vec2>) {
            out.add_float(value.x); out.add_float(value.y);
        } else {
            out.add_float(value);
        }
    }

    void count(std::size_t size) { out.add_size(size); }
};

void add_gradient(CanonicalPlanWriter& out, const graphics::GradientDefinition& value) {
    CanonicalGradientSink sink{out};
    graphics::write_gradient_identity(value, sink);
}

void add_style(CanonicalPlanWriter& out, const LayerStylePlan& style) {
    out.add_string(style.font);
    add_optional(out, style.font_size,
                 [](auto& writer, float value) { writer.add_float(value); });
    out.add_string(style.fill);

    add_optional(out, style.stroke, [](auto& writer, const StrokeStyle& stroke) {
        writer.add_string(stroke.color);
        add_optional(writer, stroke.width,
                     [](auto& nested, float value) { nested.add_float(value); });
        add_optional(writer, stroke.trim_start,
                     [](auto& nested, float value) { nested.add_float(value); });
        add_optional(writer, stroke.trim_end,
                     [](auto& nested, float value) { nested.add_float(value); });
        add_optional(writer, stroke.gradient,
                     [](auto& nested, const graphics::GradientDefinition& value) {
            add_gradient(nested, value);
        });
    });
    add_optional(out, style.shadow, [](auto& writer, const ShadowStyle& shadow) {
        writer.add_string(shadow.color);
        add_optional(writer, shadow.opacity,
                     [](auto& nested, float value) { nested.add_float(value); });
        add_optional(writer, shadow.blur,
                     [](auto& nested, float value) { nested.add_float(value); });
        for (const auto value : shadow.offset) writer.add_float(value);
        writer.add_size(shadow.offset_dimensions);
    });
    add_optional(out, style.glow, [](auto& writer, const GlowStyle& glow) {
        writer.add_float(glow.radius);
        writer.add_float(glow.intensity);
        writer.add_string(glow.color);
    });
    add_optional(out, style.background,
                 [](auto& writer, const BackgroundStyle& background) {
        writer.add_string(background.color);
        add_optional(writer, background.opacity,
                     [](auto& nested, float value) { nested.add_float(value); });
        add_optional(writer, background.radius,
                     [](auto& nested, float value) { nested.add_float(value); });
        for (const auto value : background.padding) writer.add_float(value);
        writer.add_size(background.padding_dimensions);
    });
}

void add_text_animator(CanonicalPlanWriter& out, const TextAnimatorPlan& animator) {
    out.add_string(animator.id);
    out.add_size(animator.selectors.size());
    for (const auto& selector : animator.selectors) {
        out.add_string(selector.id);
        out.add_string(selector.unit);
        out.add_string(selector.shape);
        out.add_string(selector.order);
        out.add_string(selector.combine);
        add_track(out, selector.start);
        add_track(out, selector.end);
        add_track(out, selector.offset);
        add_track(out, selector.amount);
        out.add_bool(selector.exclude_spaces);
        out.add_bool(selector.randomize_order);
        out.add_uint64(selector.random_seed);
    }
    out.add_size(animator.properties.size());
    for (const auto& track : animator.properties) add_track(out, track);
}

void add_camera(CanonicalPlanWriter& out, const CameraPlan& camera) {
    out.add_string(camera.type);
    for (const auto value : camera.position) out.add_float(value);
    for (const auto value : camera.rotation_deg) out.add_float(value);
    out.add_float(camera.fov_deg);
    out.add_float(camera.near);
    out.add_float(camera.far);
    out.add_float(camera.zoom);
}

void add_camera_track(CanonicalPlanWriter& out, const CameraTrackPlan& track) {
    out.add_enum(track.property);
    out.add_string(track.easing);
    out.add_size(track.keyframes.size());
    for (const auto& key : track.keyframes) {
        out.add_frame(key.frame);
        out.add_size(key.value.size());
        for (const auto value : key.value) out.add_float(value);
    }
}

void add_layer(CanonicalPlanWriter& out, const LayerPlan& layer) {
    out.add_string(layer.id);
    out.add_enum(layer.type);
    out.add_string(layer.asset);
    out.add_string(layer.source);
    out.add_string(layer.text);
    out.add_string(layer.font);
    add_optional(out, layer.style,
                 [](auto& writer, const LayerStylePlan& value) {
                     add_style(writer, value);
                 });
    // Preserve the historical optional-presence byte for every layer; only
    // present shape payloads append the additive shape contract fields.
    out.add_bool(layer.shape.has_value());
    if (layer.shape) {
        const auto& shape = *layer.shape;
        out.add_enum(shape.type);
        out.add_float(shape.radius);
        add_optional(out, shape.fill_color, [](auto& nested, const auto& color) {
            for (const auto component : color) nested.add_float(component);
        });
        add_optional(out, shape.gradient, [](auto& nested,
                                                 const graphics::GradientDefinition& value) {
            add_gradient(nested, value);
        });
        add_optional(out, shape.stroke, [](auto& nested, const StrokeStyle& stroke) {
            nested.add_string(stroke.color);
            add_optional(nested, stroke.width, [](auto& w, float v) { w.add_float(v); });
            add_optional(nested, stroke.solid_color, [](auto& w, const auto& color) {
                for (const auto component : color) w.add_float(component);
            });
            add_optional(nested, stroke.trim_start, [](auto& w, float v) { w.add_float(v); });
            add_optional(nested, stroke.trim_end, [](auto& w, float v) { w.add_float(v); });
            add_optional(nested, stroke.gradient, [](auto& w,
                    const graphics::GradientDefinition& value) {
                add_gradient(w, value);
            });
        });
        out.add_size(shape.path.size());
        for (const auto& command : shape.path) {
            out.add_enum(command.type);
            for (const auto value : command.point) out.add_float(value);
            for (const auto value : command.control1) out.add_float(value);
            for (const auto value : command.control2) out.add_float(value);
        }
        out.add_size(shape.operators.size());
        for (const auto& op : shape.operators) {
            out.add_enum(op.kind);
            if (op.kind == PathOperatorKindPlan::RoundCorners) {
                out.add_float(op.radius);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::ZigZag) {
                out.add_float(op.size);
                out.add_float(op.points_per_unit);
                out.add_bool(op.smooth);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::OffsetPath) {
                out.add_float(op.amount);
                out.add_float(op.miter_limit);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::PuckerBloat) {
                out.add_float(op.amount);
                for (const auto value : op.anchor) out.add_float(value);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::Twist) {
                out.add_float(op.degrees);
                for (const auto value : op.center) out.add_float(value);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::WigglePath) {
                out.add_float(op.amount);
                out.add_float(op.frequency);
                out.add_uint64(op.seed);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::Merge) {
                out.add_size(op.paths.size());
                for (const auto& subpath : op.paths) {
                    out.add_size(subpath.size());
                    for (const auto& command : subpath) {
                        out.add_enum(command.type);
                        for (const auto value : command.point) out.add_float(value);
                        for (const auto value : command.control1) out.add_float(value);
                        for (const auto value : command.control2) out.add_float(value);
                    }
                }
                continue;
            }
            const auto& repeater = op.repeater;
            out.add_size(repeater.copies);
            for (const auto value : repeater.position) out.add_float(value);
            for (const auto value : repeater.scale) out.add_float(value);
            out.add_float(repeater.rotation_degrees);
            for (const auto value : repeater.anchor) out.add_float(value);
            out.add_float(repeater.opacity_decay);
        }
    }
    if (layer.path_mask) {
        out.add_bool(true);
        out.add_float(layer.path_mask->feather);
        out.add_float(layer.path_mask->expansion);
        out.add_bool(layer.path_mask->inverted);
        out.add_size(layer.path_mask->path.size());
        for (const auto& command : layer.path_mask->path) {
            out.add_enum(command.type);
            for (const auto value : command.point) out.add_float(value);
            for (const auto value : command.control1) out.add_float(value);
            for (const auto value : command.control2) out.add_float(value);
        }
    }
    if (!layer.lut_effects.empty()) {
        out.add_bool(true);
        out.add_size(layer.lut_effects.size());
        for (const auto& effect : layer.lut_effects) {
            out.add_string(effect.asset);
            out.add_float(effect.amount);
        }
    }

    for (const auto value : layer.size) out.add_float(value);
    out.add_size(layer.size_dimensions);
    for (const auto value : layer.color) out.add_float(value);
    for (const auto value : layer.position) out.add_float(value);
    out.add_size(layer.position_dimensions);
    for (const auto value : layer.scale) out.add_float(value);
    out.add_size(layer.scale_dimensions);
    for (const auto value : layer.rotation) out.add_float(value);
    out.add_size(layer.rotation_dimensions);
    out.add_bool(layer.enable_3d);
    out.add_bool(layer.screen_space);
    add_optional(out, layer.depth_role,
                 [](auto& writer, const std::string& value) { writer.add_string(value); });
    out.add_float(layer.depth_offset);

    add_optional(out, layer.radius,
                 [](auto& writer, float value) { writer.add_float(value); });

    add_optional(out, layer.start_frame,
                 [](auto& writer, Frame value) { writer.add_frame(value); });
    add_optional(out, layer.duration_frames,
                 [](auto& writer, Frame value) { writer.add_frame(value); });
    add_optional(out, layer.fit,
                 [](auto& writer, FitMode value) { writer.add_enum(value); });

    add_optional(out, layer.animation,
                 [](auto& writer, const AnimationPlan& animation) {
        writer.add_size(animation.tracks.size());
        for (const auto& track : animation.tracks) add_track(writer, track);
    });

    out.add_size(layer.text_animators.size());
    for (const auto& animator : layer.text_animators)
        add_text_animator(out, animator);

    add_optional(out, layer.blend_mode,
                 [](auto& writer, chronon3d::BlendMode value) {
                     writer.add_enum(value);
                 });
    add_optional(out, layer.opacity,
                 [](auto& writer, float value) { writer.add_float(value); });
    out.add_bool(layer.loop);
}

}  // namespace

std::uint64_t compute_render_plan_content_fingerprint(const RenderPlan& plan) {
    std::array<std::byte, 64U * 1024U> scratch_storage{};
    std::pmr::monotonic_buffer_resource scratch{
        scratch_storage.data(), scratch_storage.size()};
    CanonicalPlanWriter out{&scratch};

    out.add_string("chronon3d.render-plan.content-fingerprint.v5");
    out.add_string(plan.schema);
    out.add_i64(plan.canvas.width);
    out.add_i64(plan.canvas.height);
    out.add_i64(plan.canvas.fps.num());
    out.add_i64(plan.canvas.fps.den());
    out.add_frame(plan.canvas.duration);
    add_optional(out, plan.camera,
                 [](auto& writer, const CameraPlan& camera) { add_camera(writer, camera); });
    add_optional(out, plan.camera_animation,
                 [](auto& writer, const std::vector<CameraTrackPlan>& tracks) {
                     writer.add_size(tracks.size());
                     for (const auto& track : tracks) add_camera_track(writer, track);
                 });
    out.add_uint64(plan.budget.max_temporal_pixels);
    out.add_size(plan.layers.size());
    for (const auto& layer : plan.layers) add_layer(out, layer);

    return out.finish();
}

}  // namespace chronon3d::render_plan
