#pragma once

// ---------------------------------------------------------------------------
// runtime/resource_transition.hpp
//
// Backend-neutral resource synchronization contract.
//
//   ResourceRange          — whole / image-subresource / buffer range
//   UsageIntent            — what a pass wants to do with a resource
//   ResourceUse            — pass declaration: resource + intent + range
//   ResourceStateResolver  — single authority: UsageIntent -> ResourceState
//   ResourceTransition     — resolved before/after state pair
//   ResourceStateTracker   — derives transitions from declared resource uses
//
// ResourceTransition is the canonical synchronization authority consumed by
// backends. Native APIs only translate the resolved state/range contract; they
// do not maintain a parallel hazard or barrier plan.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_plan.hpp>
#include <chronon3d/runtime/resource_state.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace chronon3d::runtime {

inline constexpr std::uint64_t kWholeBufferSize =
    std::numeric_limits<std::uint64_t>::max();

struct BufferRange {
    std::uint64_t offset{0};
    std::uint64_t size{kWholeBufferSize};

    friend bool operator==(const BufferRange&, const BufferRange&) = default;
};

struct WholeResource {
    friend bool operator==(const WholeResource&, const WholeResource&) = default;
};

using ResourceRange = std::variant<WholeResource, SubresourceRange, BufferRange>;

[[nodiscard]] inline ResourceRange whole_range() noexcept {
    return ResourceRange{WholeResource{}};
}

[[nodiscard]] inline ResourceRange image_range(
    ResourceAspect aspects,
    std::uint32_t first_mip = 0,
    std::uint32_t mip_count = 1,
    std::uint32_t first_layer = 0,
    std::uint32_t layer_count = 1) noexcept {
    return ResourceRange{SubresourceRange{
        aspects, first_mip, mip_count, first_layer, layer_count}};
}

[[nodiscard]] inline ResourceRange buffer_range(
    std::uint64_t offset,
    std::uint64_t size = kWholeBufferSize) noexcept {
    return ResourceRange{BufferRange{offset, size}};
}

[[nodiscard]] inline bool ranges_overlap(const SubresourceRange& a,
                                         const SubresourceRange& b) noexcept {
    return a.overlaps(b);
}

[[nodiscard]] inline bool ranges_overlap(const BufferRange& a,
                                         const BufferRange& b) noexcept {
    if (a.size == kWholeBufferSize || b.size == kWholeBufferSize) return true;
    const auto a_end = a.offset + a.size;
    const auto b_end = b.offset + b.size;
    return a.offset < b_end && b.offset < a_end;
}

[[nodiscard]] inline bool ranges_overlap(const ResourceRange& a,
                                         const ResourceRange& b) noexcept {
    return std::visit(
        [](const auto& x, const auto& y) -> bool {
            using X = std::decay_t<decltype(x)>;
            using Y = std::decay_t<decltype(y)>;
            if constexpr (std::is_same_v<X, WholeResource> ||
                          std::is_same_v<Y, WholeResource>) {
                return true;
            } else if constexpr (std::is_same_v<X, SubresourceRange> &&
                                 std::is_same_v<Y, SubresourceRange>) {
                return x.overlaps(y);
            } else if constexpr (std::is_same_v<X, BufferRange> &&
                                 std::is_same_v<Y, BufferRange>) {
                return ranges_overlap(x, y);
            } else {
                return false;
            }
        },
        a, b);
}

enum class UsageIntent : std::uint16_t {
    SampledRead = 0,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    ColorAttachmentRead,
    ColorAttachmentWrite,
    TransferSrc,
    TransferDst,
    VideoDecodeSrc,
    VideoDecodeDst,
    VideoEncodeSrc,
    VideoEncodeDst,
    HostRead,
    HostWrite,
};

struct ResourceUse {
    ResourceId resource{0};
    UsageIntent intent{UsageIntent::SampledRead};
    ResourceRange range{WholeResource{}};
    bool discard_previous_contents{false};

    friend bool operator==(const ResourceUse&, const ResourceUse&) = default;
};

class ResourceStateResolver {
public:
    [[nodiscard]] ResourceState resolve(UsageIntent intent,
                                        ResourceKind kind) const noexcept {
        (void)kind;
        switch (intent) {
        case UsageIntent::SampledRead:
            return ResourceState{
                .stages = PipelineStage::FragmentShader |
                          PipelineStage::ComputeShader,
                .access = AccessMask::ShaderRead,
                .layout = ResourceLayout::ShaderReadOnly,
                .queue = QueueClass::GraphicsCompute,
            };
        case UsageIntent::StorageRead:
            return ResourceState{
                .stages = PipelineStage::ComputeShader,
                .access = AccessMask::ShaderRead,
                .layout = ResourceLayout::General,
                .queue = QueueClass::Compute,
            };
        case UsageIntent::StorageWrite:
            return ResourceState{
                .stages = PipelineStage::ComputeShader,
                .access = AccessMask::ShaderWrite,
                .layout = ResourceLayout::General,
                .queue = QueueClass::Compute,
            };
        case UsageIntent::StorageReadWrite:
            return ResourceState{
                .stages = PipelineStage::ComputeShader,
                .access = AccessMask::ShaderRead | AccessMask::ShaderWrite,
                .layout = ResourceLayout::General,
                .queue = QueueClass::Compute,
            };
        case UsageIntent::ColorAttachmentRead:
            return ResourceState{
                .stages = PipelineStage::FragmentShader |
                          PipelineStage::ColorOutput,
                .access = AccessMask::ColorRead,
                .layout = ResourceLayout::ColorAttachment,
                .queue = QueueClass::GraphicsCompute,
            };
        case UsageIntent::ColorAttachmentWrite:
            return ResourceState{
                .stages = PipelineStage::ColorOutput,
                .access = AccessMask::ColorWrite,
                .layout = ResourceLayout::ColorAttachment,
                .queue = QueueClass::GraphicsCompute,
            };
        case UsageIntent::TransferSrc:
            return ResourceState{
                .stages = PipelineStage::Transfer,
                .access = AccessMask::TransferRead,
                .layout = ResourceLayout::TransferSource,
                .queue = QueueClass::Transfer,
            };
        case UsageIntent::TransferDst:
            return ResourceState{
                .stages = PipelineStage::Transfer,
                .access = AccessMask::TransferWrite,
                .layout = ResourceLayout::TransferDestination,
                .queue = QueueClass::Transfer,
            };
        case UsageIntent::VideoDecodeSrc:
            return ResourceState{
                .stages = PipelineStage::VideoDecode,
                .access = AccessMask::VideoDecodeRead,
                .layout = ResourceLayout::VideoDecodeSrc,
                .queue = QueueClass::Decode,
            };
        case UsageIntent::VideoDecodeDst:
            return ResourceState{
                .stages = PipelineStage::VideoDecode,
                .access = AccessMask::VideoDecodeWrite,
                .layout = ResourceLayout::VideoDecodeDst,
                .queue = QueueClass::Decode,
            };
        case UsageIntent::VideoEncodeSrc:
            return ResourceState{
                .stages = PipelineStage::VideoEncode,
                .access = AccessMask::VideoEncodeRead,
                .layout = ResourceLayout::VideoEncodeSrc,
                .queue = QueueClass::Encode,
            };
        case UsageIntent::VideoEncodeDst:
            return ResourceState{
                .stages = PipelineStage::VideoEncode,
                .access = AccessMask::VideoEncodeWrite,
                .layout = ResourceLayout::VideoEncodeDst,
                .queue = QueueClass::Encode,
            };
        case UsageIntent::HostRead:
            return ResourceState{
                .stages = PipelineStage::Host,
                .access = AccessMask::HostRead,
                .layout = ResourceLayout::General,
                .queue = QueueClass::External,
            };
        case UsageIntent::HostWrite:
            return ResourceState{
                .stages = PipelineStage::Host,
                .access = AccessMask::HostWrite,
                .layout = ResourceLayout::General,
                .queue = QueueClass::External,
            };
        }
        return ResourceState::undefined_state();
    }
};

struct ResourceTransition {
    ResourceId resource{0};
    ResourceRange range{WholeResource{}};
    ResourceState before{};
    ResourceState after{};
    std::size_t producer_pass{0};
    std::size_t consumer_pass{0};
    bool queue_ownership_transfer{false};
    bool alias_boundary{false};

    friend bool operator==(const ResourceTransition&,
                           const ResourceTransition&) = default;
};

enum class TransitionAction : std::uint8_t {
    NoBarrier = 0,
    EmitTransition,
};

struct TransitionResult {
    TransitionAction action{TransitionAction::NoBarrier};
    std::optional<ResourceTransition> transition{std::nullopt};
};

class ResourceStateTracker {
public:
    ResourceStateTracker() = default;

    void clear() noexcept {
        states_.clear();
        transitions_.clear();
    }

    [[nodiscard]] std::size_t transition_count() const noexcept {
        return transitions_.size();
    }

    [[nodiscard]] const std::vector<ResourceTransition>&
    transitions() const noexcept {
        return transitions_;
    }

    /// Seed a canonical initial/imported state. This is planner-owned state:
    /// backends consume the resulting ResourceTransition stream and never
    /// reconstruct an external-resource state machine of their own.
    void seed_state(ResourceId resource,
                    const ResourceRange& range,
                    const ResourceState& state,
                    std::size_t producer_pass = 0) {
        set_state(resource, range, state, producer_pass);
    }

    TransitionResult apply_use(std::size_t pass,
                               const ResourceUse& use,
                               const ResourceState& desired) {
        if (use.discard_previous_contents) {
            erase_overlaps(use.resource, use.range);
        }

        TrackedState* previous = nullptr;
        if (!use.discard_previous_contents) {
            previous = unique_overlap(use.resource, use.range);
        }

        ResourceState before = previous
            ? previous->state : ResourceState::undefined_state();
        const std::size_t producer = previous ? previous->producer_pass : pass;

        TransitionResult result;
        if (before.undefined()) {
            if (desired.writes()) {
                result = emit(pass, producer, use, before, desired, false);
            }
            set_state(use.resource, use.range, desired, pass);
        } else if (read_only(before) && read_only(desired) &&
                   before.layout == desired.layout &&
                   before.queue == desired.queue) {
            ResourceState merged = before;
            merged.stages = merged.stages | desired.stages;
            merged.access = merged.access | desired.access;
            set_state(use.resource, use.range, merged, producer);
            return TransitionResult{TransitionAction::NoBarrier, std::nullopt};
        } else {
            result = emit(pass, producer, use, before, desired, false);
            set_state(use.resource, use.range, desired, pass);
        }
        return result;
    }

    TransitionResult apply_alias_boundary(
        std::size_t pass,
        ResourceId resource,
        const ResourceRange& range,
        const ResourceState& first_state) {
        erase_overlaps(resource, range);

        // An alias boundary discards the previous logical resource's image
        // contents, but it must still order all memory accesses performed
        // through the previous physical-slot owner before the first access by
        // the new owner. Keep that semantic dependency in the canonical
        // transition stream so native backends only translate it.
        ResourceState alias_source{
            .stages = PipelineStage::AllCommands,
            .access = AccessMask::MemoryRead | AccessMask::MemoryWrite,
            .layout = ResourceLayout::Undefined,
            .queue = first_state.queue,
        };

        ResourceTransition transition;
        transition.resource = resource;
        transition.range = range;
        transition.before = alias_source;
        transition.after = first_state;
        transition.producer_pass = pass;
        transition.consumer_pass = pass;
        transition.queue_ownership_transfer = false;
        transition.alias_boundary = true;
        transitions_.push_back(transition);

        set_state(resource, range, first_state, pass);
        return TransitionResult{TransitionAction::EmitTransition, transition};
    }

    TransitionResult apply_alias_boundary(
        std::size_t pass,
        ResourceId resource,
        const ResourceState& first_state) {
        return apply_alias_boundary(
            pass, resource, ResourceRange{WholeResource{}}, first_state);
    }

private:
    struct TrackedState {
        ResourceRange range{WholeResource{}};
        ResourceState state{};
        std::size_t producer_pass{0};
    };

    [[nodiscard]] static bool read_only(const ResourceState& state) noexcept {
        return state.reads() && !state.writes();
    }

    TrackedState* unique_overlap(ResourceId resource,
                                 const ResourceRange& range) {
        const auto it = states_.find(resource);
        if (it == states_.end()) return nullptr;

        TrackedState* match = nullptr;
        for (auto& tracked : it->second) {
            if (!ranges_overlap(tracked.range, range)) continue;
            if (match != nullptr) {
                throw std::logic_error(
                    "ResourceStateTracker: one use overlaps multiple tracked "
                    "subresources; split it into canonical ranges");
            }
            match = &tracked;
        }
        return match;
    }

    void erase_overlaps(ResourceId resource, const ResourceRange& range) {
        const auto it = states_.find(resource);
        if (it == states_.end()) return;
        auto& tracked = it->second;
        tracked.erase(
            std::remove_if(
                tracked.begin(), tracked.end(),
                [&](const TrackedState& state) {
                    return ranges_overlap(state.range, range);
                }),
            tracked.end());
        if (tracked.empty()) states_.erase(it);
    }

    TransitionResult emit(std::size_t pass,
                          std::size_t producer,
                          const ResourceUse& use,
                          const ResourceState& before,
                          const ResourceState& after,
                          bool alias_boundary) {
        ResourceTransition transition;
        transition.resource = use.resource;
        transition.range = use.range;
        transition.before = before;
        transition.after = after;
        transition.producer_pass = producer;
        transition.consumer_pass = pass;
        transition.queue_ownership_transfer =
            !before.undefined() && before.queue != after.queue;
        transition.alias_boundary = alias_boundary;
        transitions_.push_back(transition);
        return TransitionResult{TransitionAction::EmitTransition, transition};
    }

    void set_state(ResourceId id,
                   const ResourceRange& range,
                   const ResourceState& state,
                   std::size_t pass) {
        erase_overlaps(id, range);
        states_[id].push_back(TrackedState{range, state, pass});
    }

    std::unordered_map<ResourceId, std::vector<TrackedState>> states_;
    std::vector<ResourceTransition> transitions_;
};

} // namespace chronon3d::runtime
