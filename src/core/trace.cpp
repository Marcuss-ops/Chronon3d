#include <chronon3d/core/trace.hpp>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace chronon3d {

namespace cache {
    class FramebufferPool;
}

namespace profiling {
    thread_local RenderTrace* g_current_trace = nullptr;
    thread_local int32_t g_current_frame = 0;
    thread_local RenderCounters* g_current_counters = nullptr;
    thread_local cache::FramebufferPool* g_current_framebuffer_pool = nullptr;
}

RenderTrace::RenderTrace() : mtx_(std::make_unique<std::mutex>()) {}

RenderTrace::RenderTrace(RenderTrace&& other) noexcept {
    if (other.mtx_) {
        std::lock_guard<std::mutex> lock(*other.mtx_);
        events_ = std::move(other.events_);
        enabled_ = other.enabled_;
        mtx_ = std::move(other.mtx_);
    } else {
        enabled_ = other.enabled_;
    }
}

RenderTrace& RenderTrace::operator=(RenderTrace&& other) noexcept {
    if (this != &other) {
        if (mtx_ && other.mtx_) {
            std::lock_guard<std::mutex> lock1(*mtx_);
            std::lock_guard<std::mutex> lock2(*other.mtx_);
            events_ = std::move(other.events_);
            enabled_ = other.enabled_;
            mtx_ = std::move(other.mtx_);
        } else {
            events_ = std::move(other.events_);
            enabled_ = other.enabled_;
            mtx_ = std::move(other.mtx_);
        }
    }
    return *this;
}

uint64_t RenderTrace::to_us(Clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
}

uint32_t RenderTrace::thread_id() {
    static std::atomic<uint32_t> next{1};
    thread_local uint32_t id = next.fetch_add(1);
    return id;
}

void RenderTrace::add(std::string_view name, std::string_view category,
                      int32_t frame, Clock::time_point start, Clock::time_point end) {
    if (!enabled_) return;

    TraceEvent e;
    e.name = std::string(name);
    e.category = std::string(category);
    e.frame = frame;
    e.ts_us = to_us(start);
    e.dur_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    e.thread_hash = thread_id();

    std::lock_guard<std::mutex> lock(*mtx_);
    events_.push_back(std::move(e));
}

std::vector<TraceEvent> RenderTrace::events() const {
    std::lock_guard<std::mutex> lock(*mtx_);
    return events_;
}

void RenderTrace::clear() {
    std::lock_guard<std::mutex> lock(*mtx_);
    events_.clear();
}

void write_trace_json(const RenderTrace& t, const std::string& path) {
    nlohmann::json j;
    j["traceEvents"] = nlohmann::json::array();
    
    auto events = t.events();
    for (const auto& e : events) {
        j["traceEvents"].push_back({
            {"name", e.name},
            {"cat", e.category},
            {"ph", "X"},
            {"ts", e.ts_us},
            {"dur", e.dur_us},
            {"pid", 1},
            {"tid", e.thread_hash},
            {"args", {{"frame", e.frame}}}
        });
    }

    std::ofstream out(path);
    if (out) {
        out << j.dump(2);
    }
}

} // namespace chronon3d
