#pragma once

#include <cstdint>

namespace chronon3d::runtime {

using DeviceId = std::uint32_t;

enum class MemoryDomain : std::uint8_t {
    Cpu,
    Vulkan,
    Cuda,
    External,
};

/// Compiler-visible residency contract for a logical resource. Backends remain
/// responsible for realization; this value only describes where the resource
/// lives and which interop boundaries constrain allocation/aliasing.
struct ResourceResidency {
    MemoryDomain domain{MemoryDomain::Cpu};
    DeviceId device{0};
    bool exportable{false};
    bool importable{false};
    bool decoder_compatible{false};
    bool encoder_compatible{false};

    [[nodiscard]] constexpr bool requires_dedicated_allocation() const noexcept {
        // Encoder-facing resources cross an interop boundary even when they
        // are not otherwise marked exportable. They must never reuse a
        // transient physical slot whose extent/ownership belongs to a
        // different producer.
        return exportable || encoder_compatible || domain == MemoryDomain::External;
    }

    [[nodiscard]] constexpr bool allows_transient_aliasing() const noexcept {
        return !requires_dedicated_allocation();
    }

    friend bool operator==(const ResourceResidency&, const ResourceResidency&) = default;
};

} // namespace chronon3d::runtime
