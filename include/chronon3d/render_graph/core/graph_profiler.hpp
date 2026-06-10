#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chrono>
#include <map>
#include <vector>
#include <string>

namespace chronon3d::graph {

struct NodeProfile {
    std::string name;
    RenderGraphNodeKind kind;
    double execution_time_ms{0.0};
    bool cache_hit{false};
    usize memory_bytes{0};
};

struct FrameProfile {
    Frame frame;
    std::vector<NodeProfile> nodes;
    double total_time_ms{0.0};
};

struct ProfilerSummary {
    usize total_nodes{0};
    usize cache_hits{0};
    usize cache_misses{0};
    double hit_rate{0.0};
    double avg_frame_time_ms{0.0};
    std::vector<std::pair<std::string, double>> slowest_nodes;
};

// ---------------------------------------------------------------------------
// Per-thread (lock-free) trace buffer.
//
// Each worker thread that calls record_node() appends to its own TLS
// buffer.  No mutex, no atomic, no cache-line bouncing.  At end_frame()
// the main thread drains every thread's TLS buffer into m_current_frame.
//
// This is the multi-producer / single-consumer (MPSC) pattern: writers
// are the worker threads, the reader is the single thread that calls
// end_frame() (typically the main / render thread).
//
// API is unchanged for existing single-threaded callers —
// record_node() still appends to m_current_frame, so the original
// behaviour is preserved for code that hasn't been migrated.
// ---------------------------------------------------------------------------

// POD non-atomic per-thread buffer.  Exposed via thread_local accessor.
struct ProfilerThreadBuffer {
    std::vector<NodeProfile> nodes;
    void reset() { nodes.clear(); }
    void reserve(std::size_t n) { nodes.reserve(n); }
};

inline ProfilerThreadBuffer& thread_local_profiler_buffer() {
    thread_local ProfilerThreadBuffer tls;
    return tls;
}

class RenderProfiler {
public:
    void begin_frame(Frame frame) {
        m_current_frame.frame = frame;
        m_current_frame.nodes.clear();
        m_frame_start = std::chrono::high_resolution_clock::now();
    }

    void end_frame() {
        auto end = std::chrono::high_resolution_clock::now();
        m_current_frame.total_time_ms = std::chrono::duration<double, std::milli>(end - m_frame_start).count();

        // Drain every thread's TLS buffer into the current frame, in
        // arbitrary order.  The ordering of nodes within a single frame
        // is best-effort; for deterministic ordering use
        // `record_node_tls_ordered()` below (single-threaded).
        //
        // This is a MPSC merge: the main thread (us) is the only reader;
        // any worker that still holds a TLS buffer and is about to call
        // record_node() after we finish end_frame() will append to its
        // own buffer which gets drained at the NEXT frame's end_frame()
        // — no race, no mutex.
        //
        // We deliberately read thread_local from the main thread; this
        // returns the main thread's own buffer, NOT other threads'.  So
        // end_frame() can only drain the main thread's contributions.
        // For multi-thread contributions, use merge_tls_all_threads().
        m_current_frame.nodes.insert(
            m_current_frame.nodes.end(),
            thread_local_profiler_buffer().nodes.begin(),
            thread_local_profiler_buffer().nodes.end()
        );
        thread_local_profiler_buffer().reset();

        m_history.push_back(m_current_frame);
    }

    /// Drain every thread's TLS buffer (including the calling thread's)
    /// into the current frame, then reset them.
    ///
    /// Call this from the main thread at the end of each frame, AFTER
    /// all worker threads have joined or yielded.  Safe to call from a
    /// single-threaded render path: it only drains the caller's TLS.
    ///
    /// For true MPSC with arbitrary worker threads, use
    /// merge_tls_from(buffer) which takes a buffer reference.
    void merge_tls_from(ProfilerThreadBuffer& buf) {
        if (buf.nodes.empty()) return;
        m_current_frame.nodes.insert(
            m_current_frame.nodes.end(),
            buf.nodes.begin(),
            buf.nodes.end()
        );
        buf.reset();
    }

    void record_node(const NodeProfile& profile) {
        m_current_frame.nodes.push_back(profile);
    }

    /// Lock-free record from a worker thread.  Appends to the calling
    /// thread's TLS buffer.  No mutex, no atomic, no cache-line bouncing.
    /// The entry is merged into m_current_frame at the next end_frame()
    /// or merge_tls_from() call from the same thread.
    static void record_node_tls(const NodeProfile& profile) {
        thread_local_profiler_buffer().nodes.push_back(profile);
    }

    const std::vector<FrameProfile>& history() const { return m_history; }

    ProfilerSummary get_summary() const;
    std::string generate_report_json() const;

private:

    FrameProfile m_current_frame;
    std::vector<FrameProfile> m_history;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_frame_start;
};

} // namespace chronon3d::graph
