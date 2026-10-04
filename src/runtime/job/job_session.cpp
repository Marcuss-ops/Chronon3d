#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>

namespace chronon3d::runtime {

struct NativeEchoHistoryState {
    struct Entry {
        std::int64_t frame{0};
        NativeEchoSurfaceRef surface;
    };
    mutable std::mutex mutex;
    std::unordered_map<std::string, std::deque<Entry>> frames;
    std::size_t bytes{0};
    static constexpr std::size_t byte_budget = 512ULL * 1024ULL * 1024ULL;
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
      native_echo_history_(std::make_shared<NativeEchoHistoryState>()) {}

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
    {
        std::lock_guard lock(native_echo_history_->mutex);
        auto& frames = native_echo_history_->frames[std::string(key)];
        const auto position = std::lower_bound(frames.begin(), frames.end(), frame,
            [](const NativeEchoHistoryState::Entry& candidate,
               std::int64_t target) { return candidate.frame < target; });
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
        while (native_echo_history_->bytes > NativeEchoHistoryState::byte_budget) {
            auto oldest = native_echo_history_->frames.end();
            for (auto it = native_echo_history_->frames.begin();
                 it != native_echo_history_->frames.end(); ++it) {
                if (!it->second.empty() && (oldest == native_echo_history_->frames.end() ||
                    it->second.front().frame < oldest->second.front().frame))
                    oldest = it;
            }
            if (oldest == native_echo_history_->frames.end()) break;
            native_echo_history_->bytes -= oldest->second.front().surface->desc.bytes;
            retired.push_back(std::move(oldest->second.front().surface));
            oldest->second.pop_front();
            if (oldest->second.empty()) native_echo_history_->frames.erase(oldest);
        }
    }
    return true;
}

void JobSession::clear_native_echo_history() noexcept {
    if (!native_echo_history_) return;
    std::unordered_map<std::string, std::deque<NativeEchoHistoryState::Entry>> retired;
    {
        std::lock_guard lock(native_echo_history_->mutex);
        retired.swap(native_echo_history_->frames);
        native_echo_history_->bytes = 0;
    }
}

void JobSession::reset_job() {
    begin_job();
    reset_frame_values();
    reset_temporal_history();
    reset_scene_state();
    memory_aggregate_state->reset();
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
