#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

#include <chronon3d/simd/kernel_resolver.hpp>

namespace chronon3d::simd {

/// Stable ids for the central CPU-kernel registry.  Implementations are
/// registered once during session preparation; render code receives the
/// resolved, typed table and never performs ISA checks in a pixel loop.
enum class KernelId : std::uint16_t {
    Blur,
    Blend,
    Glow,
    Resample,
    ColorMatrix,
};

using KernelTable = PixelKernelSet;

class KernelRegistry {
public:
    KernelRegistry() noexcept : m_sets(make_scalar_sets()) {
        m_registered[static_cast<std::size_t>(CpuIsa::Scalar)] = true;
    }

    template <typename Fn>
    void register_kernel(KernelId id, CpuIsa isa, Fn fn) {
        static_assert(std::is_pointer_v<Fn>,
                      "KernelRegistry requires a typed function pointer");
        ensure_slot(isa);

        if constexpr (std::is_same_v<Fn, BlurKernel::ApplyFn>) {
            require(id, KernelId::Blur);
            m_sets[index(isa)].blur = BlurKernel{fn};
        } else if constexpr (std::is_same_v<Fn, BlendKernel::ApplyFn>) {
            require(id, KernelId::Blend);
            m_sets[index(isa)].blend = BlendKernel{fn};
        } else if constexpr (std::is_same_v<Fn, GlowKernel::ApplyFn>) {
            require(id, KernelId::Glow);
            m_sets[index(isa)].glow = GlowKernel{fn};
        } else if constexpr (std::is_same_v<Fn, ResampleKernel::ApplyFn>) {
            require(id, KernelId::Resample);
            m_sets[index(isa)].resample = ResampleKernel{fn};
        } else if constexpr (std::is_same_v<Fn, ColorMatrixKernel::ApplyFn>) {
            require(id, KernelId::ColorMatrix);
            m_sets[index(isa)].color_matrix = ColorMatrixKernel{fn};
        } else {
            static_assert(std::is_same_v<Fn, void>,
                          "KernelRegistry received an unknown kernel signature");
        }
        m_registered[index(isa)] = true;
    }

    [[nodiscard]] const KernelTable& resolve(const CpuCapabilities& caps) const noexcept {
        const auto wanted = index(caps.highest_isa);
        if (m_registered[wanted]) return m_sets[wanted];
        return m_sets[index(CpuIsa::Scalar)];
    }

    [[nodiscard]] bool has(CpuIsa isa) const noexcept {
        return m_registered[index(isa)];
    }

private:
    static constexpr std::size_t index(CpuIsa isa) noexcept {
        return static_cast<std::size_t>(isa);
    }

    static void require(KernelId actual, KernelId expected) {
        if (actual != expected) {
            throw std::invalid_argument("KernelRegistry: KernelId/signature mismatch");
        }
    }

    static std::array<KernelTable, 5> make_scalar_sets() noexcept {
        return {kScalarSet, kScalarSet, kScalarSet, kScalarSet, kScalarSet};
    }

    void ensure_slot(CpuIsa isa) noexcept {
        const auto slot = index(isa);
        if (m_registered[slot]) return;
        m_sets[slot] = kScalarSet;
        m_registered[slot] = true;
    }

    std::array<KernelTable, 5> m_sets;
    std::array<bool, 5> m_registered{};
};

} // namespace chronon3d::simd
