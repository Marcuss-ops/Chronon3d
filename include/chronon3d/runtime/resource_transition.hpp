#pragma once

// ---------------------------------------------------------------------------
// runtime/resource_transition.hpp
//
// Backend-neutral resource-synchronization primitives (sync2-era contract).
//
//   ResourceRange          — whole / image-subresource / buffer range
//   UsageIntent            — what a pass wants to DO with a resource
//   ResourceUse            — pass declaration: resource + intent + range
//   ResourceStateResolver  — single authority: UsageIntent → ResourceState
//   ResourceTransition     — resolved before/after state pair for a resource
//   ResourceStateTracker   — per-resource/subresource state machine that
//                            derives ResourceTransitions from ResourceUses
//
// The LEGACY BarrierPlan / BarrierTransition (gpu_command_plan.hpp) stays
// untouched and keeps producing its barrier plan for the existing backends.
// This module is the parallel, additive representation: tests compare the
// two outputs while Vulkan still consumes the legacy plan. See
// docs/tickets/TICKET-RESOURCE-STATE-V1.md for the Demolition Debt sheet.
//
// No Vulkan type leaks into this contract. ResourceState/SubresourceRange
// are the canonical enums from resource_state.hpp (extended additively with
// plane aspects, color/video stages/access, color-attachment + video layouts
// and compute/decode/encode queue classes).
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/render_surface.hpp>   // runtime::ResourceKind
#include <chronon3d/runtime/resource_plan.hpp>    // runtime::ResourceId
#include <chronon3d/runtime/resource_state.hpp>   // ResourceState + enums

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

// ── ResourceRange ──────────────────────────────────────────────────────────

/// Sentinel size for a whole-buffer range.
inline constexpr std::uint64_t kWholeBufferSize =
    std::numeric_limits<std::uint64_t>::max();

/// Byte range of a buffer resource. `size == kWholeBufferSize` means the
/// whole buffer from `offset`.
struct BufferRange {
    std::uint64_t offset{0};
    std::uint64_t size{kWholeBufferSize};

    friend bool operator==(const BufferRange&, const BufferRange&) = default;
};

/// Tag for "the entire resource, whatever its shape".
struct WholeResource {
    friend bool operator==(const WholeResource&, const WholeResource&) = default;
};

/// The canonical, backend-neutral resource range. `SubresourceRange` is the
/// image subresource form (aspects incl. Plane0/Plane1/Plane2 + mip/layer
/// span); `BufferRange` the byte form; `WholeResource` the whole-resource
/// wildcard. Image ranges and buffer ranges never overlap.
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
                // Image vs buffer: different domains, never overlap.
                return false;
            }
        },
        a, b);
}

// ── UsageIntent + ResourceUse ──────────────────────────────────────────────

/// What a pass intends to DO with a resource. Passes declare intents, not
/// resolved states: a single central resolver turns the intent into the
/// concrete ResourceState so no backend re-decides stage/layout mapping.
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

/// A pass's declared use of one resource over one range.
struct ResourceUse {
    ResourceId resource{0};
    UsageIntent intent{UsageIntent::SampledRead};
    ResourceRange range{WholeResource{}};
    /// `true` = the pass overwrites the entire range; the previous contents
    /// (and their state) are NOT a valid dependency. Transient resources
    /// start from `Undefined`; imported resources must declare a state.
    bool discard_previous_contents{false};

    friend bool operator==(const ResourceUse&, const ResourceUse&) = default;
};

// ── ResourceStateResolver ──────────────────────────────────────────────────

/// The single authority that maps `UsageIntent` (+ resource kind) to a
/// concrete backend-neutral `ResourceState`. Backends translate the RESULT;
/// they never re-derive stage/layout policy from pass-local `if`s.
///
/// `kind` is currently reserved for future refinement (e.g. depth/stencil
/// reads, YUV-plane layout differences); today the intent alone determines
/// the state.
class ResourceStateResolver {
public:
    [[nodiscard]] ResourceState resolve(UsageIntent intent,
                                        ResourceKind kind) const noexcept {
        (void)kind;  // reserved: layout refinement for depth/plane kinds
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

// ── ResourceTransition ─────────────────────────────────────────────────────

/// A fully-resolved synchronization transition for one resource range.
/// Produced by the tracker; consumed by backends (which translate the two
/// states into native sync primitives) and by the compiled-graph serializer.
struct ResourceTransition {
    ResourceId resource{0};
    ResourceRange range{WholeResource{}};
    ResourceState before{};
    ResourceState after{};
    std::size_t producer_pass{0};  // pass that established `before`
    std::size_t consumer_pass{0};  // pass that requires `after`
    bool queue_ownership_transfer{false};
    bool alias_boundary{false};

    friend bool operator==(const ResourceTransition&,
                           const ResourceTransition&) = default;
};

// ── ResourceStateTracker ───────────────────────────────────────────────────

enum class TransitionAction : std::uint8_t {
    NoBarrier = 0,
    EmitTransition,
};

struct TransitionResult {
    TransitionAction action{TransitionAction::NoBarrier};
    std::optional<ResourceTransition> transition{std::nullopt};
};

/// Per-resource (and per-subresource-range) state machine.
///
/// Hazard rules (for overlapping ranges):
///
///   | previous | current  | barrier             |
///   |----------|----------|---------------------|
///   | read     | read     | no (accumulate)     |
///   | write    | read     | yes — RAW           |
///   | read     | write    | yes — WAR           |
///   | write    | write    | yes — WAW           |
///   | layout A | layout B | yes — StateTransition|
///   | queue A  | queue B  | yes — ownership xfer|
///   | non-overlap | any    | no                  |
///
/// A read→read pair with the same layout and queue accumulates the previous
/// stages/access into the current state WITHOUT emitting a barrier, so the
/// next writer synchronizes with ALL prior readers.
class ResourceStateTracker {
public:
    ResourceStateTracker() = default;

    /// Drop all tracked state and emitted transitions.
    void clear() noexcept {
        states_.clear();
        ranges_.clear();
        producers_.clear();
        transitions_.clear();
    }

    [[nodiscard]] std::size_t transition_count() const noexcept {
        return transitions_.size();
    }

    /// Emitted transitions in application order (deterministic).
    [[nodiscard]] const std::vector<ResourceTransition>&
    transitions() const noexcept {
        return transitions_;
    }

    /// Apply one pass use. `pass` is the consuming pass index.
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
            // A non-overlapping subresource range has no dependency on the
            // previous state: the new range starts fresh (no barrier).
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
            // First use. A first WRITE must establish the initial layout; a
            // first read from Undefined has nothing to synchronize with.
            if (desired.writes()) {
                result = emit(pass, producer, use, before, desired, false);
            }
            set_state(use.resource, use.range, desired, pass);
        } else if (read_only(before) && read_only(desired) &&
                   before.layout == desired.layout &&
                   before.queue == desired.queue) {
            // read→read with identical layout/queue: no barrier; merge the
            // reader stages/access into the current state so the next writer
            // synchronizes with ALL prior readers. The producer (original
            // writer) is preserved.
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

    /// Start a NEW logical lifetime on `resource` (physical-slot reuse /
    /// aliasing). The previous logical owner's state is NOT inherited; a
    /// dedicated transition with `alias_boundary = true` records the first
    /// state of the new owner.
    TransitionResult apply_alias_boundary(
        std::size_t pass,
        ResourceId resource,
        const ResourceState& first_state) {
        ResourceUse use;
        use.resource = resource;
        use.range = WholeResource{};
        use.intent = UsageIntent::StorageWrite;
        use.discard_previous_contents = true;

        ResourceTransition t;
        t.resource = resource;
        t.range = use.range;
        t.before = ResourceState::undefined_state();
        t.after = first_state;
        t.producer_pass = pass;
        t.consumer_pass = pass;
        t.queue_ownership_transfer = false;
        t.alias_boundary = true;
        transitions_.push_back(t);

        set_state(resource, use.range, first_state, pass);
        return TransitionResult{TransitionAction::EmitTransition, t};
    }

private:
    [[nodiscard]] static bool read_only(const ResourceState& s) noexcept {
        return s.reads() && !s.writes();
    }

    TransitionResult emit(std::size_t pass,
                          std::size_t producer,
                          const ResourceUse& use,
                          const ResourceState& before,
                          const ResourceState& after,
                          bool alias_boundary) {
        ResourceTransition t;
        t.resource = use.resource;
        t.range = use.range;
        t.before = before;
        t.after = after;
        t.producer_pass = producer;
        t.consumer_pass = pass;
        t.queue_ownership_transfer = before.queue != after.queue;
        t.alias_boundary = alias_boundary;
        transitions_.push_back(t);
        return TransitionResult{TransitionAction::EmitTransition, t};
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