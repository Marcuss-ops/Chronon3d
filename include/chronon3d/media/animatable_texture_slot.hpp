// Animatable texture slot — the media layer contract for video-derived fonts
// and alternative animated content sources.
// owned by: Media (source plug-ins) — only Media writes this table.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace chronon3d::media {

/// Policy applied when the supplied key falls outside the cached sample grid.
enum class AnimatableTextureExtrapolation {
    kClamp,   ///< freeze on the nearest edge sample (default)
    kReject,  ///< fail closed with a validation problem
};

struct AnimatableTextureSpec {
    std::string slot_id;             ///< stable slot identifier (e.g. "sys:mono")
    std::uint32_t width = 0;         ///< width of each frame bitmap
    std::uint32_t height = 0;        ///< height of each frame bitmap
    std::uint32_t frame_count = 0;   ///< number of allocated frame bitmaps
    std::uint16_t samples_per_second = 0;  ///< sample rate, must divide frame_count
    std::uint64_t source_hash = 0;   ///< appears in node cache identity
    AnimatableTextureExtrapolation extrapolation =
        AnimatableTextureExtrapolation::kClamp;
};

struct AnimatableTextureProblem {
    std::string code;    ///< stable machine-readable code
    std::string detail; ///< human-readable explanation
};

/// Validates a caller-provided spec and normalises it into a renderable slot.
/// Returns false and fills `problem` on rejection; fail closed.
bool bake_animatable_texture_slot(
    const AnimatableTextureSpec& spec,
    AnimatableTextureSpec* baked,
    AnimatableTextureProblem* problem);

/// Byte-serialisation helpers (no format glamour, stable little-endian layout).
std::size_t encoded_size(const AnimatableTextureSpec& spec);
bool encode(const AnimatableTextureSpec& spec, std::uint8_t* data, std::size_t size);
bool decode(AnimatableTextureSpec* spec, const std::uint8_t* data, std::size_t size);

} // namespace chronon3d::media
