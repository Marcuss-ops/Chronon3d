#pragma once

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>

namespace chronon3d::runtime {

/// Structural identity of an overlay template — everything that defines "the
/// same template" independent of per-overlay content (text strings, image
/// assets, colors, timing).  Two overlays with the same descriptor share one
/// compiled plan; only their instance data differs.
struct OverlayTemplateDesc {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t text_layers{0};
    std::uint32_t image_layers{0};
    std::uint32_t logo_layers{0};
    bool camera_perspective{false};

    friend bool operator==(const OverlayTemplateDesc&, const OverlayTemplateDesc&) = default;
};

struct OverlayTemplateDescHash {
    std::size_t operator()(const OverlayTemplateDesc& desc) const noexcept {
        std::size_t result = static_cast<std::size_t>(desc.width);
        result ^= static_cast<std::size_t>(desc.height) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        result ^= static_cast<std::size_t>(desc.text_layers) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        result ^= static_cast<std::size_t>(desc.image_layers) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        result ^= static_cast<std::size_t>(desc.logo_layers) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        result ^= static_cast<std::size_t>(desc.camera_perspective ? 1u : 0u) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        return result;
    }
};

/// A compiled overlay template: the structural command plan (ordered passes,
/// resource aliasing, barriers) produced once per OverlayTemplateDesc.
/// Instance data (concrete surface handles, transforms, colors) is bound at
/// execution time, not baked into the template.
struct CompiledOverlayTemplate {
    OverlayTemplateDesc desc{};
    CommandPlan plan{};
};

/// Compile-once cache for overlay templates.
///
/// `compile()` returns the cached compiled template for a descriptor,
/// invoking the caller-supplied builder only on the first request (and on a
/// later miss after eviction).  This is the GPU-side analogue of the software
/// SceneProgramCache: the template's pass graph is compiled once and reused
/// across the N overlays that share its structure.  Bounded by entry count
/// (LRU) via the canonical cache::LruCache — no second cache primitive.
class OverlayTemplateCache {
public:
    struct Stats {
        std::size_t hits{0};
        std::size_t misses{0};
        std::size_t evictions{0};
        std::size_t entries{0};
    };

    explicit OverlayTemplateCache(std::size_t capacity_entries = 32);

    /// Resolve (and on miss, compile) the template for `desc`.  `builder` is
    /// invoked with the descriptor exactly once per successful compile.
    CompiledOverlayTemplate compile(
        const OverlayTemplateDesc& desc,
        const std::function<CommandPlan(const OverlayTemplateDesc&)>& builder);

    void clear();
    [[nodiscard]] std::size_t capacity() const;
    [[nodiscard]] Stats stats() const;

private:
    cache::LruCache<OverlayTemplateDesc, CompiledOverlayTemplate, OverlayTemplateDescHash>
        m_cache;
};

} // namespace chronon3d::runtime
