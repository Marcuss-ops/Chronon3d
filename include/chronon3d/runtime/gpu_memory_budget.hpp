#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include <chronon3d/core/types/error.hpp>

namespace chronon3d::runtime {

struct GpuMemoryBudgetPolicy {
    std::uint64_t soft_limit_bytes{0};
    std::uint64_t hard_limit_bytes{0};
    double soft_limit_ratio{0.80};
    double hard_limit_ratio{0.92};
    double fragmentation_warning_ratio{0.35};
};

enum class GpuMemoryPressure : std::uint8_t {
    Normal = 0,
    Soft,
    Critical,
};

[[nodiscard]] constexpr std::string_view to_string(GpuMemoryPressure pressure) noexcept {
    switch (pressure) {
        case GpuMemoryPressure::Normal: return "Normal";
        case GpuMemoryPressure::Soft: return "Soft";
        case GpuMemoryPressure::Critical: return "Critical";
    }
    return "Unknown";
}

/// Canonical detail adapter for the budget resolver snapshot. A critical
/// pressure reading is telemetry; device loss is assigned only when the
/// backend actually reports that operation failure.
[[nodiscard]] inline chronon::Error canonical_error(GpuMemoryPressure pressure) {
    return chronon::from_legacy<GpuMemoryPressure>(
        chronon::ErrorCategory::Recoverable,
        "gpu_memory",
        "chronon3d::runtime::GpuMemoryPressure",
        to_string(pressure),
        "GPU memory pressure resolved");
}

struct GpuMemoryBudgetSnapshot {
    std::uint64_t usage_bytes{0};
    std::uint64_t budget_bytes{0};
    std::uint64_t allocation_bytes{0};
    std::uint64_t block_bytes{0};
    std::uint64_t soft_limit_bytes{0};
    std::uint64_t hard_limit_bytes{0};
    double fragmentation_ratio{0.0};
    GpuMemoryPressure pressure{GpuMemoryPressure::Normal};

    [[nodiscard]] bool fragmentation_warning(
        const GpuMemoryBudgetPolicy& policy) const noexcept {
        return fragmentation_ratio >=
            std::clamp(policy.fragmentation_warning_ratio, 0.0, 1.0);
    }
};

/// Canonical GPU-memory policy resolver. Backends feed their native allocator
/// telemetry into this resolver; they do not invent backend-specific pressure
/// thresholds. A zero explicit byte limit means "derive from device budget".
class GpuMemoryBudgetResolver {
public:
    explicit GpuMemoryBudgetResolver(GpuMemoryBudgetPolicy policy = {}) noexcept
        : policy_(sanitize(policy)) {}

    void set_policy(GpuMemoryBudgetPolicy policy) noexcept {
        policy_ = sanitize(policy);
    }

    [[nodiscard]] const GpuMemoryBudgetPolicy& policy() const noexcept {
        return policy_;
    }

    [[nodiscard]] GpuMemoryBudgetSnapshot resolve(
        std::uint64_t usage_bytes,
        std::uint64_t budget_bytes,
        std::uint64_t allocation_bytes = 0,
        std::uint64_t block_bytes = 0) const noexcept {
        GpuMemoryBudgetSnapshot snapshot{};
        snapshot.usage_bytes = usage_bytes;
        snapshot.budget_bytes = budget_bytes;
        snapshot.allocation_bytes = allocation_bytes;
        snapshot.block_bytes = block_bytes;
        snapshot.soft_limit_bytes = effective_limit(
            policy_.soft_limit_bytes, policy_.soft_limit_ratio, budget_bytes);
        snapshot.hard_limit_bytes = effective_limit(
            policy_.hard_limit_bytes, policy_.hard_limit_ratio, budget_bytes);

        if (snapshot.hard_limit_bytes != 0 && snapshot.soft_limit_bytes != 0 &&
            snapshot.hard_limit_bytes < snapshot.soft_limit_bytes) {
            snapshot.hard_limit_bytes = snapshot.soft_limit_bytes;
        }

        if (block_bytes != 0 && allocation_bytes <= block_bytes) {
            snapshot.fragmentation_ratio = 1.0 -
                static_cast<double>(allocation_bytes) /
                static_cast<double>(block_bytes);
        }

        if (snapshot.hard_limit_bytes != 0 &&
            usage_bytes >= snapshot.hard_limit_bytes) {
            snapshot.pressure = GpuMemoryPressure::Critical;
        } else if (snapshot.soft_limit_bytes != 0 &&
                   usage_bytes >= snapshot.soft_limit_bytes) {
            snapshot.pressure = GpuMemoryPressure::Soft;
        }
        return snapshot;
    }

    [[nodiscard]] bool can_reserve(
        const GpuMemoryBudgetSnapshot& snapshot,
        std::uint64_t bytes) const noexcept {
        if (snapshot.hard_limit_bytes == 0) return true;
        if (snapshot.usage_bytes >= snapshot.hard_limit_bytes) return false;
        if (bytes > snapshot.hard_limit_bytes) return false;
        return bytes <= snapshot.hard_limit_bytes - snapshot.usage_bytes;
    }

private:
    static GpuMemoryBudgetPolicy sanitize(GpuMemoryBudgetPolicy policy) noexcept {
        policy.soft_limit_ratio = std::clamp(policy.soft_limit_ratio, 0.0, 1.0);
        policy.hard_limit_ratio = std::clamp(policy.hard_limit_ratio, 0.0, 1.0);
        policy.fragmentation_warning_ratio =
            std::clamp(policy.fragmentation_warning_ratio, 0.0, 1.0);
        if (policy.hard_limit_bytes != 0 && policy.soft_limit_bytes != 0 &&
            policy.hard_limit_bytes < policy.soft_limit_bytes) {
            policy.hard_limit_bytes = policy.soft_limit_bytes;
        }
        if (policy.hard_limit_ratio < policy.soft_limit_ratio) {
            policy.hard_limit_ratio = policy.soft_limit_ratio;
        }
        return policy;
    }

    static std::uint64_t effective_limit(
        std::uint64_t explicit_bytes,
        double ratio,
        std::uint64_t budget_bytes) noexcept {
        if (explicit_bytes != 0) return explicit_bytes;
        if (budget_bytes == 0 || ratio <= 0.0) return 0;
        const long double scaled =
            static_cast<long double>(budget_bytes) * static_cast<long double>(ratio);
        if (scaled >= static_cast<long double>(
                std::numeric_limits<std::uint64_t>::max())) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return static_cast<std::uint64_t>(scaled);
    }

    GpuMemoryBudgetPolicy policy_{};
};

} // namespace chronon3d::runtime
