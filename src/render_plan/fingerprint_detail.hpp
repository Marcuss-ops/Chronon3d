#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace chronon3d::render_plan::fingerprint_detail {

class CanonicalPlanWriter {
public:
    explicit CanonicalPlanWriter(std::pmr::memory_resource* resource);
    void add_bool(bool value);
    template <typename T> requires std::is_enum_v<T>
    void add_enum(T value) {
        using U = std::underlying_type_t<T>;
        add_i64(static_cast<std::int64_t>(static_cast<U>(value)));
    }
    void add_frame(Frame value);
    void add_float(float value);
    void add_double(double value);
    void add_i64(std::int64_t value);
    void add_uint64(std::uint64_t value);
    void add_uint32(std::uint32_t value);
    void add_size(std::size_t value);
    void add_string(std::string_view value);
    [[nodiscard]] std::uint64_t finish() const noexcept;
private:
    void add_u8(std::uint8_t value);
    void add_u32(std::uint32_t value);
    void add_u64(std::uint64_t value);
    std::pmr::vector<std::byte> bytes_;
};

template <typename T, typename Add>
void add_optional(CanonicalPlanWriter& out, const std::optional<T>& value, Add add) {
    out.add_bool(value.has_value());
    if (value) add(out, *value);
}

void add_spring(CanonicalPlanWriter&, const SpringPlan&);
void add_track(CanonicalPlanWriter&, const AnimationTrackPlan&);
void add_gradient(CanonicalPlanWriter&, const graphics::GradientDefinition&);
void add_field2d(CanonicalPlanWriter&, const graphics::Field2D&);
void add_gradient_animation(CanonicalPlanWriter&, const GradientAnimationPlan&);
void add_style(CanonicalPlanWriter&, const LayerStylePlan&);
void add_text_animator(CanonicalPlanWriter&, const TextAnimatorPlan&);
void add_transition(CanonicalPlanWriter&, const TransitionPlan&);
void add_camera(CanonicalPlanWriter&, const CameraPlan&);
void add_camera_track(CanonicalPlanWriter&, const CameraTrackPlan&);
void add_audio_mix(CanonicalPlanWriter&, const AudioMixPlan&);
void add_stagger(CanonicalPlanWriter&, const LayerPlan::StaggerPlan&);
void add_layer(CanonicalPlanWriter&, const LayerPlan&);

} // namespace chronon3d::render_plan::fingerprint_detail
