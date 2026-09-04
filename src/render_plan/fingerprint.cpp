#include <chronon3d/render_plan/render_plan.hpp>

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

void add_style(CanonicalPlanWriter& out, const LayerStylePlan& style) {
    out.add_string(style.font);
    add_optional(out, style.font_size,
                 [](auto& writer, float value) { writer.add_float(value); });
    out.add_string(style.fill);

    add_optional(out, style.stroke, [](auto& writer, const StrokeStyle& stroke) {
        writer.add_string(stroke.color);
        add_optional(writer, stroke.width,
                     [](auto& nested, float value) { nested.add_float(value); });
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

    for (const auto value : layer.size) out.add_float(value);
    out.add_size(layer.size_dimensions);
    for (const auto value : layer.color) out.add_float(value);
    for (const auto value : layer.position) out.add_float(value);
    out.add_size(layer.position_dimensions);
    for (const auto value : layer.scale) out.add_float(value);
    out.add_size(layer.scale_dimensions);
    for (const auto value : layer.rotation) out.add_float(value);
    out.add_size(layer.rotation_dimensions);

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

    out.add_string("chronon3d.render-plan.content-fingerprint.v4");
    out.add_string(plan.schema);
    out.add_i64(plan.canvas.width);
    out.add_i64(plan.canvas.height);
    out.add_i64(plan.canvas.fps.num());
    out.add_i64(plan.canvas.fps.den());
    out.add_frame(plan.canvas.duration);
    out.add_uint64(plan.budget.max_temporal_pixels);
    out.add_size(plan.layers.size());
    for (const auto& layer : plan.layers) add_layer(out, layer);

    return out.finish();
}

}  // namespace chronon3d::render_plan
