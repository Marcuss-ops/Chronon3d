#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/runtime/job/temporal_sample_workspace.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>

#include <algorithm>
#include <deque>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace chronon3d::runtime {

struct NativeEchoHistoryState {
    struct Reservation {
        std::size_t bytes_per_frame{0};
        std::size_t keep_frames{0};
        std::size_t bytes{0};
    };
    struct Entry {
        std::int64_t frame{0};
        NativeEchoSurfaceRef surface;
    };
    struct ScratchSet {
        std::uint32_t width{0};
        std::uint32_t height{0};
        std::array<NativeEchoSurfaceRef, 3> surfaces;
        std::size_t bytes{0};
    };
    mutable std::mutex mutex;
    std::unordered_map<std::string, std::deque<Entry>> frames;
    std::unordered_map<std::string, Reservation> reservations;
    std::unordered_map<std::string, bool> rejected_keys;
    std::unordered_set<std::string> rejected_scratch_keys;
    std::unordered_map<std::string, ScratchSet> scratch_sets;
    std::size_t bytes{0};
    std::size_t reserved_bytes{0};
    std::size_t scratch_bytes{0};
    static constexpr std::size_t byte_budget = 512ULL * 1024ULL * 1024ULL;
    static constexpr std::size_t scratch_byte_budget = 128ULL * 1024ULL * 1024ULL;
};

NativeEchoSurface::~NativeEchoSurface() {
    if (handle == kInvalidRenderSurfaceHandle) return;
    try {
        if (backend) (void)backend->release_surface(handle);
        if (registry) (void)registry->release(handle);
    } catch (...) {
        // Native history cleanup runs during reset/destruction. Never allow a
        // backend teardown error to escape a surface lease destructor.
    }
}

JobSession::JobSession(RuntimeContext& runtime)
    : runtime_(&runtime),
      native_echo_history_(std::make_shared<NativeEchoHistoryState>()) {
    initialize_slot_authorities(kPipelineDepth);
}

JobSession::JobSession()
    : native_echo_history_(std::make_shared<NativeEchoHistoryState>()) {
    initialize_slot_authorities(kPipelineDepth);
}

JobSession::JobSession(TemporalSampleTag)
    : native_echo_history_(std::make_shared<NativeEchoHistoryState>()) {
    initialize_slot_authorities(1, 256u * 1024u);
}

std::unique_ptr<JobSession> JobSession::make_temporal_sample_session() {
    return std::unique_ptr<JobSession>(new JobSession(TemporalSampleTag{}));
}

void JobSession::initialize_slot_authorities(
    std::size_t capacity, std::size_t arena_capacity_bytes) {
    gpu_completion_ = std::make_unique<GpuCompletionTracker>(capacity);
    slots_ = std::make_unique<FrameSlotPool>(
        capacity, *gpu_completion_, arena_capacity_bytes);
    evaluated_queue_ = std::make_unique<FrameQueue>(capacity);
    rendered_queue_ = std::make_unique<FrameQueue>(capacity);
}

JobSession::JobSession(JobSession&&) noexcept = default;
JobSession& JobSession::operator=(JobSession&&) noexcept = default;

JobSession::~JobSession() {
    clear_native_echo_history();
}

void RenderErrorSlot::clear() {
    std::lock_guard lock(mutex_);
    error_.reset();
    has_error_.store(false, std::memory_order_release);
}

void RenderErrorSlot::publish_first(const graph::NodeExecutionError& error) {
    std::lock_guard lock(mutex_);
    if (!error_) {
        error_ = std::make_shared<const graph::NodeExecutionError>(error);
        has_error_.store(true, std::memory_order_release);
    }
}

std::shared_ptr<const graph::NodeExecutionError> RenderErrorSlot::load() const {
    std::lock_guard lock(mutex_);
    return error_;
}

JobSession::JobSession(RuntimeContext& runtime, JobSessionConfig config)
    : runtime_(&runtime),
      native_echo_history_(std::make_shared<NativeEchoHistoryState>()),
      program_(std::move(config.program)),
      first_frame_(config.first_frame),
      end_frame_(config.end_frame),
      job_id_(config.job_id) {
    if (!program_) {
        throw std::invalid_argument("JobSession requires a CompiledProgram");
    }
    if (end_frame_ < first_frame_) {
        throw std::invalid_argument("JobSession frame range is inverted");
    }
    initialize_slot_authorities(kPipelineDepth);
}

JobSession::JobSession(RuntimeContext& runtime, Frame first_frame,
                       Frame end_frame, std::uint64_t job_id)
    : runtime_(&runtime),
      native_echo_history_(std::make_shared<NativeEchoHistoryState>()),
      first_frame_(first_frame),
      end_frame_(end_frame),
      job_id_(job_id) {
    if (end_frame_ < first_frame_) {
        throw std::invalid_argument("JobSession frame range is inverted");
    }
    initialize_slot_authorities(kPipelineDepth);
}

graph::GraphExecutor* JobSession::executor() const noexcept {
    return executor_ ? executor_ : (runtime_ ? &runtime_->executor() : nullptr);
}

std::vector<NativeEchoSurfaceRef> JobSession::native_echo_frames(
    std::string_view key, std::int64_t current_frame,
    std::uint32_t echoes, std::uint32_t frame_offset) const {
    std::vector<NativeEchoSurfaceRef> result(echoes);
    if (!native_echo_history_ || key.empty() || frame_offset == 0) return result;
    std::lock_guard lock(native_echo_history_->mutex);
    const auto found = native_echo_history_->frames.find(std::string(key));
    if (found == native_echo_history_->frames.end()) return result;
    const auto& frames = found->second;
    for (std::uint32_t i = 1; i <= echoes; ++i) {
        const auto target = current_frame -
            static_cast<std::int64_t>(i) * static_cast<std::int64_t>(frame_offset);
        const auto entry = std::lower_bound(frames.begin(), frames.end(), target,
            [](const NativeEchoHistoryState::Entry& candidate,
               std::int64_t frame) { return candidate.frame < frame; });
        if (entry != frames.end() && entry->frame == target)
            result[i - 1] = entry->surface;
    }
    return result;
}

TemporalSampleWorkspace& JobSession::temporal_sample_workspace(
    FrameSlotId slot_id) {
    if (slot_id >= temporal_sample_workspaces_.size())
        throw std::out_of_range("JobSession temporal workspace slot is invalid");
    auto& workspace = temporal_sample_workspaces_[slot_id];
    if (!workspace) workspace = std::make_unique<TemporalSampleWorkspace>();
    return *workspace;
}

bool JobSession::reserve_native_echo_history(
    std::string_view key, std::size_t bytes_per_frame,
    std::size_t keep_frames) {
    if (key.empty()) return false;
    if (!native_echo_history_)
        native_echo_history_ = std::make_shared<NativeEchoHistoryState>();
    const std::string owned_key(key);
    std::size_t required_bytes = 0;
    bool rejected = false;
    {
        std::lock_guard lock(native_echo_history_->mutex);
        if (native_echo_history_->rejected_keys.contains(owned_key)) return false;
        if (const auto existing = native_echo_history_->reservations.find(owned_key);
            existing != native_echo_history_->reservations.end()) {
            return existing->second.bytes_per_frame == bytes_per_frame &&
                   existing->second.keep_frames == keep_frames;
        }
        const bool dimensions_fit = bytes_per_frame != 0 && keep_frames != 0 &&
            bytes_per_frame <= NativeEchoHistoryState::byte_budget &&
            keep_frames <= NativeEchoHistoryState::byte_budget / bytes_per_frame;
        if (!dimensions_fit) {
            native_echo_history_->rejected_keys.emplace(owned_key, true);
            rejected = true;
        } else {
            required_bytes = bytes_per_frame * keep_frames;
            if (required_bytes > NativeEchoHistoryState::byte_budget -
                                     native_echo_history_->reserved_bytes) {
                native_echo_history_->rejected_keys.emplace(owned_key, true);
                rejected = true;
            } else {
                native_echo_history_->reservations.emplace(
                    owned_key, NativeEchoHistoryState::Reservation{
                        bytes_per_frame, keep_frames, required_bytes});
                native_echo_history_->reserved_bytes += required_bytes;
                return true;
            }
        }
    }
    if (rejected)
        spdlog::warn("event=native_echo_history_admission_denied key={} bytes_per_frame={} keep_frames={} required_bytes={} budget_bytes={}; using software Echo for this key",
                     owned_key, bytes_per_frame, keep_frames, required_bytes,
                     NativeEchoHistoryState::byte_budget);
    return false;
}

std::optional<std::array<RenderSurfaceHandle, 3>> JobSession::native_echo_scratch(
    std::string_view key, FrameSlotId slot_id, const SurfaceDesc& desc,
    graph::RenderBackend& backend, RenderSurfaceRegistry& registry) {
    if (key.empty() || desc.width == 0 || desc.height == 0 ||
        !backend.supports_native_surfaces()) return std::nullopt;
    if (!native_echo_history_)
        native_echo_history_ = std::make_shared<NativeEchoHistoryState>();

    const std::string scratch_key = std::to_string(slot_id) + ":" + std::string(key);
    std::lock_guard lock(native_echo_history_->mutex);
    if (native_echo_history_->rejected_scratch_keys.contains(scratch_key))
        return std::nullopt;
    auto found = native_echo_history_->scratch_sets.find(scratch_key);
    if (found != native_echo_history_->scratch_sets.end() &&
        found->second.width == desc.width && found->second.height == desc.height &&
        std::all_of(found->second.surfaces.begin(), found->second.surfaces.end(),
            [&](const NativeEchoSurfaceRef& surface) {
                return surface && backend.is_native_surface_valid(surface->handle);
            })) {
        return std::array<RenderSurfaceHandle, 3>{
            found->second.surfaces[0]->handle,
            found->second.surfaces[1]->handle,
            found->second.surfaces[2]->handle};
    }

    const auto per_surface_bytes = desc.tight_bytes();
    if (per_surface_bytes == 0 ||
        per_surface_bytes > NativeEchoHistoryState::scratch_byte_budget / 3U) {
        native_echo_history_->rejected_scratch_keys.emplace(scratch_key);
        spdlog::warn("event=native_echo_scratch_admission_denied key={} slot={} bytes_per_surface={} scratch_budget_bytes={}; using software Echo",
                     key, slot_id, per_surface_bytes,
                     NativeEchoHistoryState::scratch_byte_budget);
        return std::nullopt;
    }
    const auto required_bytes = per_surface_bytes * 3U;
    const auto existing_bytes = found == native_echo_history_->scratch_sets.end()
        ? 0U : found->second.bytes;
    if (required_bytes > NativeEchoHistoryState::scratch_byte_budget -
            (native_echo_history_->scratch_bytes - existing_bytes)) {
        native_echo_history_->rejected_scratch_keys.emplace(scratch_key);
        spdlog::warn("event=native_echo_scratch_admission_denied key={} slot={} required_bytes={} retained_bytes={} scratch_budget_bytes={}; using software Echo",
                     key, slot_id, required_bytes,
                     native_echo_history_->scratch_bytes - existing_bytes,
                     NativeEchoHistoryState::scratch_byte_budget);
        return std::nullopt;
    }

    NativeEchoHistoryState::ScratchSet replacement;
    replacement.width = desc.width;
    replacement.height = desc.height;
    replacement.bytes = required_bytes;
    auto retained_desc = desc;
    retained_desc.lifetime = LifetimeClass::JobPersistent;
    for (auto& surface : replacement.surfaces) {
        auto lease = std::make_shared<NativeEchoSurface>();
        lease->desc = retained_desc;
        lease->backend = &backend;
        lease->registry = &registry;
        const auto handle = registry.create(retained_desc);
        if (handle == kInvalidRenderSurfaceHandle) {
            native_echo_history_->rejected_scratch_keys.emplace(scratch_key);
            spdlog::warn("event=native_echo_scratch_create_failed key={} slot={} width={} height={}; using software Echo",
                         key, slot_id, desc.width, desc.height);
            return std::nullopt;
        }
        lease->handle = handle;
        const auto created = backend.create_surface(handle, retained_desc);
        if (!created.ok()) {
            native_echo_history_->rejected_scratch_keys.emplace(scratch_key);
            spdlog::warn("event=native_echo_scratch_create_failed key={} slot={} width={} height={}; using software Echo",
                         key, slot_id, desc.width, desc.height);
            return std::nullopt;
        }
        surface = std::move(lease);
    }

    std::array<RenderSurfaceHandle, 3> handles{
        replacement.surfaces[0]->handle,
        replacement.surfaces[1]->handle,
        replacement.surfaces[2]->handle};
    native_echo_history_->scratch_bytes =
        native_echo_history_->scratch_bytes - existing_bytes + required_bytes;
    native_echo_history_->scratch_sets.insert_or_assign(scratch_key,
                                                          std::move(replacement));
    return handles;
}

bool JobSession::record_native_echo_frame(
    std::string_view key, std::int64_t frame,
    RenderSurfaceHandle source, std::size_t keep_frames,
    graph::RenderBackend& backend, RenderSurfaceRegistry& registry) {
    if (key.empty() || keep_frames == 0 ||
        source == kInvalidRenderSurfaceHandle) return false;
    if (!backend.supports_native_surfaces() || !backend.is_native_surface_valid(source))
        return false;
    const auto source_record = registry.lookup(source);
    if (!source_record || !is_rgb_surface_format(source_record->desc.format)) return false;

    auto desc = source_record->desc;
    desc.usage = ResourceUsage::Storage;
    desc.lifetime = LifetimeClass::JobPersistent;
    desc.bytes = desc.tight_bytes();
    if (desc.bytes == 0 || desc.bytes > NativeEchoHistoryState::byte_budget) return false;
    if (!reserve_native_echo_history(key, desc.bytes, keep_frames)) return false;

    const auto handle = registry.create(desc);
    if (handle == kInvalidRenderSurfaceHandle) return false;
    const auto created = backend.create_surface(handle, desc);
    if (!created.ok()) {
        (void)backend.release_surface(handle);
        (void)registry.release(handle);
        return false;
    }
    const auto copied = backend.copy_surface(handle, source);
    if (!copied.ok()) {
        (void)backend.release_surface(handle);
        (void)registry.release(handle);
        return false;
    }
    auto surface = std::make_shared<NativeEchoSurface>();
    surface->handle = handle;
    surface->desc = desc;
    surface->backend = &backend;
    surface->registry = &registry;

    if (!native_echo_history_)
        native_echo_history_ = std::make_shared<NativeEchoHistoryState>();
    std::vector<NativeEchoSurfaceRef> retired;
    bool accepted = false;
    {
        std::lock_guard lock(native_echo_history_->mutex);
        const std::string owned_key(key);
        const auto reservation = native_echo_history_->reservations.find(owned_key);
        if (reservation == native_echo_history_->reservations.end() ||
            reservation->second.bytes_per_frame != desc.bytes ||
            reservation->second.keep_frames != keep_frames) {
            accepted = false;
        } else {
            auto& frames = native_echo_history_->frames[owned_key];
            auto projected_bytes = native_echo_history_->bytes + desc.bytes;
            const auto position = std::lower_bound(frames.begin(), frames.end(), frame,
                [](const NativeEchoHistoryState::Entry& candidate,
                   std::int64_t target) { return candidate.frame < target; });
            if (position != frames.end() && position->frame == frame) {
                projected_bytes -= position->surface->desc.bytes;
            } else if (frames.size() >= keep_frames) {
                projected_bytes -= frames.front().surface->desc.bytes;
            }
            if (projected_bytes <= NativeEchoHistoryState::byte_budget &&
                projected_bytes <= native_echo_history_->reserved_bytes) {
                if (position != frames.end() && position->frame == frame) {
                    native_echo_history_->bytes -= position->surface->desc.bytes;
                    retired.push_back(std::move(position->surface));
                    *position = NativeEchoHistoryState::Entry{frame, surface};
                } else {
                    frames.insert(position, NativeEchoHistoryState::Entry{frame, surface});
                }
                native_echo_history_->bytes += desc.bytes;
                while (frames.size() > keep_frames) {
                    native_echo_history_->bytes -= frames.front().surface->desc.bytes;
                    retired.push_back(std::move(frames.front().surface));
                    frames.pop_front();
                }
                accepted = true;
            } else if (frames.empty()) {
                native_echo_history_->frames.erase(owned_key);
            }
        }
    }
    return accepted;
}

void JobSession::clear_native_echo_history() noexcept {
    if (!native_echo_history_) return;
    std::unordered_map<std::string, std::deque<NativeEchoHistoryState::Entry>> retired;
    std::unordered_map<std::string, NativeEchoHistoryState::ScratchSet> retired_scratch;
    {
        std::lock_guard lock(native_echo_history_->mutex);
        retired.swap(native_echo_history_->frames);
        retired_scratch.swap(native_echo_history_->scratch_sets);
        native_echo_history_->rejected_scratch_keys.clear();
        native_echo_history_->bytes = 0;
        native_echo_history_->scratch_bytes = 0;
        native_echo_history_->reservations.clear();
        native_echo_history_->rejected_keys.clear();
        native_echo_history_->reserved_bytes = 0;
    }
}

void JobSession::reset_job() {
    begin_job();
    reset_frame_values();
    reset_temporal_history();
    reset_scene_state();
    memory_aggregate_state->reset();
    effect_usage_.reset();
    // FIXING.md H-07 — job-lifetime peak: shrink only when the buffer has
    // grown pathologically (16 MiB ≈ 4× 1080p float scratch). Keeps warm
    // reuse for normal jobs; bounds idle memory for outlier frames.
    static constexpr std::size_t kShrinkThreshold = 16 * 1024 * 1024 / sizeof(float);
    if (download_scratch_.capacity() > kShrinkThreshold) {
        download_scratch_.clear();
        download_scratch_.shrink_to_fit();
    }
    complete_job();
}

}  // namespace chronon3d::runtime
