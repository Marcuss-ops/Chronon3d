#pragma once

#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace chronon3d::graphics {

/// Seed for one organic paint blob. Positions are in normalized [0, 1]
/// coordinates; the radius is the blob's reach in the same normalized space.
struct PaintBlob {
    Vec2 center{0.5f, 0.5f};
    float radius{0.25f};
    float elongation{1.0f};
    float rotation_radians{0.0f};
};

/// PaintMaskGenerator — the single V1 primitive behind every paint reveal.
///
/// It composes the engine's deterministic Field2D noise with an organic blob
/// cover field and a growing threshold: the same generator produces wipes,
/// blobs, ink spread, dry brush and merges purely by parameterization, so the
/// paint family lowers to primitives the renderer already samples
/// (MaskType::Field2D) instead of growing a per-look effect.
///
/// Coverage is monotonic in progress: growing the reveal progress only grows
/// the revealed area, which is the acceptance gate the paint family certifies
/// (coverage(t1) <= coverage(t2) for t1 <= t2 with fixed everything else).
struct PaintMaskGenerator {
    Field2D noise{};
    std::vector<PaintBlob> blobs{};
    /// 0 = nothing revealed, 1 = complete reveal.
    float progress{0.0f};
    /// Softness of the threshold edge in field-value units, (0, 1].
    float feather{0.12f};
    /// Grain contribution of the noise field in [0, 1]; 0 gives clean blobs.
    float dry_brush{0.35f};
    /// Edge warp amount applied by the noise before thresholding, in [0, 1].
    float edge_warp{0.5f};

    /// Evaluates the raw cover field before thresholding, in [0, 1]. The blob
    /// term is the smooth maximum of every blob's warped radial falloff, so
    /// nearby blobs merge organically instead of intersecting hard.
    [[nodiscard]] float cover_field(Vec2 point) const noexcept {
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            return 0.0f;
        const float clamped_progress = std::clamp(progress, 0.0f, 1.0f);
        if (blobs.empty())
            return clamped_progress >= 1.0f ? 1.0f : 0.0f;

        const float warp = std::clamp(edge_warp, 0.0f, 1.0f) * 0.35f;
        const float grain = std::clamp(dry_brush, 0.0f, 1.0f);
        const float noise_scale = std::max(noise.frequency, 1.0e-4f);

        float cover = 0.0f;
        for (const PaintBlob& blob : blobs) {
            // Progress grows each blob's reach from zero to at least its full
            // radius (the 1.15 headroom lets the largest blob overfill the
            // frame at progress 1).
            const float reach = blob.radius * 2.4f * clamped_progress;
            if (reach <= 1.0e-6f)
                continue;
            Vec2 delta = point - blob.center;
            if (blob.rotation_radians != 0.0f) {
                const float c = std::cos(-blob.rotation_radians);
                const float s = std::sin(-blob.rotation_radians);
                delta = Vec2{delta.x * c - delta.y * s, delta.x * s + delta.y * c};
            }
            const float elongation = std::max(blob.elongation, 0.05f);
            delta.y /= elongation;
            const float distance = glm::length(delta);
            float falloff = 1.0f - distance / reach;
            if (falloff <= 0.0f)
                continue;
            // Warped edge: sample the noise at the point (and, deterministically,
            // along the radius) to roughen the boundary without breaking
            // monotonicity — the warp only redistributes field value inside the
            // reach, it never injects coverage outside a fully grown blob.
            const float sample = noise.sample(point);
            const float radial_sample = noise.sample(Vec2{
                blob.center.x + delta.x * std::max(noise_scale, 1.0f),
                blob.center.y + delta.y * std::max(noise_scale, 1.0f)});
            const float edge = (sample - 0.5f) * warp + (radial_sample - 0.5f) * grain * 0.5f;
            falloff = std::clamp(falloff + edge * (1.0f - clamped_progress * 0.25f), 0.0f, 1.0f);
            // Smooth union: blends overlapping blobs instead of hard-max.
            cover = cover + falloff - cover * falloff;
        }
        return std::clamp(cover, 0.0f, 1.0f);
    }

    /// Evaluates the final paint mask: the thresholded, feathered cover field.
    ///
    /// The threshold slides down with progress while the cover field grows, so
    /// both V1 acceptance anchors hold by construction: progress 0 reveals
    /// nothing (threshold 1-feather, cover 0) and progress 1 reveals the
    /// complete frame (threshold 0 — every cover value passes). Between the
    /// anchors the smoothstep keeps the edge soft, and per-pixel output is
    /// monotonic non-decreasing in progress.
    [[nodiscard]] float sample(Vec2 point) const noexcept {
        const float feather = std::clamp(this->feather, 1.0e-4f, 1.0f);
        const float clamped_progress = std::clamp(progress, 0.0f, 1.0f);
        const float threshold_low = (1.0f - clamped_progress) * (1.0f - feather);
        const float value = cover_field(point);
        if (value >= threshold_low)
            return 1.0f;
        const float t = std::clamp((value - (threshold_low - feather)) / feather, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    /// Deterministic factory: one organic blob reveal.
    [[nodiscard]] static PaintMaskGenerator blob(std::uint32_t seed,
                                                 Vec2 center = Vec2{0.5f, 0.5f},
                                                 float radius = 0.55f) noexcept {
        PaintMaskGenerator generator;
        generator.noise.generator = Field2DGenerator::Simplex;
        generator.noise.seed = seed;
        generator.noise.frequency = 3.5f;
        generator.blobs = {PaintBlob{center, radius, 1.0f, 0.0f}};
        generator.feather = 0.18f;
        generator.dry_brush = 0.25f;
        generator.edge_warp = 0.55f;
        return generator;
    }

    /// Deterministic factory: dry-brush reveal with heavy grain and many
    /// noise octaves for incomplete, granular edges.
    [[nodiscard]] static PaintMaskGenerator dry_brush_reveal(std::uint32_t seed) noexcept {
        PaintMaskGenerator generator;
        generator.noise.generator = Field2DGenerator::Fractal;
        generator.noise.seed = seed;
        generator.noise.frequency = 5.0f;
        generator.noise.octaves = 5;
        generator.noise.persistence = 0.55f;
        generator.blobs = {PaintBlob{Vec2{0.5f, 0.5f}, 0.62f, 1.0f, 0.0f}};
        generator.feather = 0.30f;
        generator.dry_brush = 0.75f;
        generator.edge_warp = 0.80f;
        return generator;
    }

    /// Deterministic factory: multi-blob merge — several seeds growing into
    /// one continuous cover.
    [[nodiscard]] static PaintMaskGenerator multi_blob(std::uint32_t seed,
                                                       std::size_t count = 4) noexcept {
        PaintMaskGenerator generator;
        generator.noise.generator = Field2DGenerator::Simplex;
        generator.noise.seed = seed;
        generator.noise.frequency = 4.0f;
        count = std::clamp(count, std::size_t{2}, std::size_t{8});
        generator.blobs.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const float angle = static_cast<float>(index) * 6.2831853f / static_cast<float>(count)
                + static_cast<float>(seed % 7U) * 0.1f;
            const float radius = 0.45f + 0.08f * static_cast<float>((seed >> (index % 8)) & 3U);
            generator.blobs.push_back(PaintBlob{
                Vec2{0.5f + std::cos(angle) * 0.22f, 0.5f + std::sin(angle) * 0.22f},
                radius, 1.0f + 0.2f * static_cast<float>(index % 3), angle});
        }
        generator.feather = 0.15f;
        generator.dry_brush = 0.30f;
        generator.edge_warp = 0.60f;
        return generator;
    }

    /// Deterministic factory: directional paint wipe — a wide elongated blob
    /// sweeping across the frame along `angle_radians`.
    [[nodiscard]] static PaintMaskGenerator wipe(std::uint32_t seed,
                                                 float angle_radians = 0.0f) noexcept {
        PaintMaskGenerator generator;
        generator.noise.generator = Field2DGenerator::Perlin;
        generator.noise.seed = seed;
        generator.noise.frequency = 2.5f;
        PaintBlob brush;
        brush.center = Vec2{0.5f, 0.5f};
        brush.radius = 0.70f;
        brush.elongation = 4.5f;
        brush.rotation_radians = angle_radians;
        generator.blobs = {brush};
        generator.feather = 0.14f;
        generator.dry_brush = 0.20f;
        generator.edge_warp = 0.45f;
        return generator;
    }
};

} // namespace chronon3d::graphics
