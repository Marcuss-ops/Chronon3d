#pragma once

#include <cstddef>
#include <cstdint>

namespace chronon3d::backends::vulkan {

// First GPU-driven particle step: bounded, stateless emitters.  The CPU
// sampler remains the authority for parity tests; this path is intentionally
// limited to procedural billboards until sprite/path support is certified.
inline constexpr std::uint32_t kGpuParticleMaxCount = 4096;
inline constexpr std::size_t kGpuParticleLutEntries = 256;

struct alignas(16) GpuParticleEmitterUniform {
    std::uint32_t header[4]{};          // count, seed, shape, flags
    float emitter[4]{};                 // size.xy, lifetime_min, lifetime_max
    float velocity_size[4]{};           // velocity_min.xy, size_min, size_max
    float velocity_gravity[4]{};        // velocity_max.xy, gravity, turbulence
    float flow_twinkle[4]{};            // strength, scale, speed, twinkle.amount
    float motion[4]{};                  // twinkle.speed, depth_min, depth_max, seconds
    float sprite[4]{};                  // sprite scale/rotation ranges
    float angular[4]{};                 // angular velocity ranges, blend mode
    float dynamics[4]{};                // air_drag, emitter_velocity.xy, emissive
    float color_lut[kGpuParticleLutEntries][4]{};
    float over_life_lut[kGpuParticleLutEntries][4]{}; // r=size, g=opacity
};

static_assert(sizeof(GpuParticleEmitterUniform) == 8336,
              "particle emitter UBO must match particle_emitter.comp std140 ABI");
static_assert(offsetof(GpuParticleEmitterUniform, color_lut) == 144,
              "particle emitter scalar prefix must stay 144 bytes");
static_assert(offsetof(GpuParticleEmitterUniform, over_life_lut) == 4240,
              "particle over-life LUT must follow the color LUT");

// Keep the instance storage 256-byte aligned so the same buffer can back both
// the uniform descriptor (offset 0) and the storage descriptor (offset N).
inline constexpr std::size_t kGpuParticleInstanceOffset =
    ((sizeof(GpuParticleEmitterUniform) + 255U) / 256U) * 256U;

} // namespace chronon3d::backends::vulkan
