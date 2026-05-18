#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace chronon3d {

struct TraceEvent {
    std::string name;
    std::string category;
    int32_t frame{0};
    uint64_t ts_us{0};
    uint64_t dur_us{0};
    uint32_t thread_hash{0};
};

class RenderTrace {
public:
    using Clock = std::chrono::steady_clock;

    RenderTrace();
    RenderTrace(RenderTrace&& other) noexcept;
    RenderTrace& operator=(RenderTrace&& other) noexcept;

    void add(std::string_view name, std::string_view category,
             int32_t frame, Clock::time_point start, Clock::time_point end);

    void set_enabled(bool v) { enabled_ = v; }
    [[nodiscard]] bool enabled() const { return enabled_; }

    [[nodiscard]] std::vector<TraceEvent> events() const;
    void clear();

private:
    static uint64_t to_us(Clock::time_point tp);
    static uint32_t thread_id();

    std::vector<TraceEvent> events_;
    mutable std::unique_ptr<std::mutex> mtx_;
    bool enabled_{true};
};

class TraceScope {
public:
    TraceScope(RenderTrace* t, std::string_view n,
               std::string_view c, int32_t f)
        : trace_(t && t->enabled() ? t : nullptr),
          name_(n), cat_(c), frame_(f),
          start_(RenderTrace::Clock::now()) {}

    ~TraceScope() {
        if (!trace_) return;
        auto end = RenderTrace::Clock::now();
        trace_->add(name_, cat_, frame_, start_, end);
    }

private:
    RenderTrace* trace_;
    std::string_view name_, cat_;
    int32_t frame_;
    RenderTrace::Clock::time_point start_;
};

namespace profiling {
    extern thread_local RenderTrace* g_current_trace;
    extern thread_local int32_t g_current_frame;
}

void write_trace_json(const RenderTrace& t, const std::string& path);

} // namespace chronon3d
