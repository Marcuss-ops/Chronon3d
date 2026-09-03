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

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
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
        ranges_.clear();
        producers_.clear();
        transitions_.clear();
    }

    [[nodiscard]] std::size_t transition_count() const noexcept {
        return transitions_.size();
    }

    [[nodiscard]] const std::vector<ResourceTransition>&
    transitions() const noexcept {
        return transitions_;
    }

    TransitionResult apply_use(std::size_t pass,
                               const ResourceUse& use,
                               const ResourceState& desired) {
        ResourceState before = ResourceState::undefined_state();
        std::size_t producer = pass;
        const auto state_it = states_.find(use.resource);
        const bool previous_known =
            !use.discard_previous_contents && state_it != states_.end();
        if (previous_known) {
            before = state_it->second;
            const auto producer_it = producers_.find(use.resource);
            if (producer_it != producers_.end()) producer = producer_it->second;
            const auto range_it = ranges_.find(use.resource);
            if (range_it != ranges_.end() &&
                !ranges_overlap(range_it->second, use.range)) {
                set_state(use.resource, use.range, desired, pass);
                return TransitionResult{TransitionAction::NoBarrier,
                                        std::nullopt};
            }
        }

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
        const ResourceState& first_state) {
        ResourceUse use;
        use.resource = resource;
        use.range = WholeResource{};
        use.intent = UsageIntent::StorageWrite;
        use.discard_previous_contents = true;

        ResourceTransition transition;
        transition.resource = resource;
        transition.range = use.range;
        transition.before = ResourceState::undefined_state();
        transition.after = first_state;
        transition.producer_pass = pass;
        transition.consumer_pass = pass;
        transition.queue_ownership_transfer = false;
        transition.alias_boundary = true;
        transitions_.push_back(transition);

        set_state(resource, use.range, first_state, pass);
        return TransitionResult{TransitionAction::EmitTransition, transition};
    }

private:
    [[nodiscard]] static bool read_only(const ResourceState& state) noexcept {
        return state.reads() && !state.writes();
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
        states_[id] = state;
        ranges_[id] = range;
        producers_[id] = pass;
    }

    std::unordered_map<ResourceId, ResourceState> states_;
    std::unordered_map<ResourceId, ResourceRange> ranges_;
    std::unordered_map<ResourceId, std::size_t> producers_;
    std::vector<ResourceTransition> transitions_;
};

} // namespace chronon3d::runtime
