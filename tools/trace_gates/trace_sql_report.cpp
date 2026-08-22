// ============================================================================
// tools/trace_gates/trace_sql_report.cpp — plan §24 report tool
//
// Standalone .pftrace report without trace_processor: re-reads the trace with
// the Perfetto pbzero decoders shipped in the SDK header and prints
//
//   1. trace span + slice count
//   2. top-N worst frames — per-frame total slice time, aggregated by the
//      `frame` debug annotation (equivalent PerfettoSQL:
//        SELECT frame, COUNT(*) AS slices, SUM(dur)/1e6 AS total_ms
//        FROM slice WHERE name = 'RenderFrame' GROUP BY frame
//        ORDER BY total_ms DESC LIMIT 20;)
//   3. most expensive node — `node_execute` slices aggregated by the
//      `stable_node_id` debug annotation (plan §7).  Falls back to slice
//      name when no node carries an annotation.
//
// Slice duration = BEGIN..END pair matched per track (nesting-aware stack).
//
// Usage: trace_sql_report <file.pftrace> [--top-frames N] [--top-nodes N]
// Exit codes: 0 = ok, 1 = parse/report error.
// ============================================================================

#include <perfetto.h>   // pbzero trace decoders (standalone tool, not public API)

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using perfetto::protos::pbzero::Trace;
using perfetto::protos::pbzero::TracePacket;
using perfetto::protos::pbzero::TracePacketDefaults;
using perfetto::protos::pbzero::TrackEventDefaults;
using perfetto::protos::pbzero::TrackEvent;
using perfetto::protos::pbzero::InternedData;
using perfetto::protos::pbzero::EventName;
using perfetto::protos::pbzero::DebugAnnotation;

namespace {

struct SliceInfo {
    std::string name;
    std::uint64_t track{0};
    std::uint64_t frame{0};          // frame annotation (0 = none)
    std::uint64_t stable_node_id{0}; // node_execute annotation (0 = none)
};

struct Aggregate {
    double total_ms{0};
    std::uint64_t count{0};
};

std::string read_file(const char* path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { ok = false; return {}; }
    ok = true;
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

std::uint64_t track_of(const TracePacket::Decoder& tp) {
    if (tp.has_trace_packet_defaults()) {
        TracePacketDefaults::Decoder d(tp.trace_packet_defaults().data,
                                       tp.trace_packet_defaults().size);
        if (d.has_track_event_defaults()) {
            TrackEventDefaults::Decoder te(d.track_event_defaults().data,
                                           d.track_event_defaults().size);
            if (te.has_track_uuid()) return te.track_uuid();
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: trace_sql_report <file.pftrace> "
                     "[--top-frames N] [--top-nodes N]\n");
        return 1;
    }
    const char* path = argv[1];
    int top_frames = 20;
    int top_nodes = 10;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--top-frames") == 0 && i + 1 < argc) {
            top_frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--top-nodes") == 0 && i + 1 < argc) {
            top_nodes = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 1;
        }
    }

    bool ok = false;
    const std::string bytes = read_file(path, ok);
    if (!ok) {
        std::fprintf(stderr, "report error: cannot open %s\n", path);
        return 1;
    }

    std::map<std::uint64_t, std::string> event_names;

    // In-process TrackEvent packets carry RELATIVE timestamps (delta from the
    // previous packet of the sequence); trace_processor reconstructs the
    // absolute time by accumulation.  NOTE: the preamble (clock snapshots /
    // config echo) carries ABSOLUTE boot timestamps that must NOT be added to
    // the sum — so the reported span is computed from matched slice
    // endpoints (below), which are immune to the preamble offset.
    std::uint64_t running_ts = 0;
    std::uint64_t span_first = ~0ULL, span_last = 0;

    // Nesting-aware BEGIN/END pairing per track -> durations.  Single pass:
    // interning packets always precede the events that reference them, and
    // END packets carry no name, so matching happens here in file order.
    struct Open { std::string name; std::uint64_t frame; std::uint64_t sid; std::uint64_t ts; };
    std::map<std::uint64_t, std::vector<Open>> stacks;
    std::map<std::string, Aggregate> by_name;                    // name -> ms
    std::map<std::uint64_t, Aggregate> frames;                   // frame -> ms
    std::map<std::uint64_t, Aggregate> nodes;                    // stable_node_id -> ms
    std::uint64_t node_calls_annotated = 0;
    std::uint64_t slice_count = 0;

    Trace::Decoder trace(reinterpret_cast<const uint8_t*>(bytes.data()),
                         bytes.size());
    for (auto pit = trace.packet(); pit; ++pit) {
        const auto packet = *pit;
        TracePacket::Decoder tp(packet.data, packet.size);
        if (tp.has_timestamp()) running_ts += tp.timestamp();
        if (tp.has_incremental_state_cleared() && tp.incremental_state_cleared()) {
            event_names.clear();
        }
        if (tp.has_interned_data()) {
            InternedData::Decoder id(tp.interned_data().data,
                                     tp.interned_data().size);
            for (auto eit = id.event_names(); eit; ++eit) {
                const auto ev = *eit;
                EventName::Decoder e(ev.data, ev.size);
                event_names[e.iid()] = e.name().ToStdString();
            }
        }
        if (!tp.has_track_event()) continue;
        TrackEvent::Decoder te(tp.track_event().data, tp.track_event().size);
        if (!te.has_type()) continue;
        const int32_t type = te.type();
        if (type != perfetto::protos::pbzero::TrackEvent_Type::TYPE_SLICE_BEGIN &&
            type != perfetto::protos::pbzero::TrackEvent_Type::TYPE_SLICE_END) {
            continue;
        }
        // Resolve the event name (BEGIN carries it; END is empty and matched
        // by stack position).
        std::string name;
        if (te.has_name()) name = te.name().ToStdString();
        else if (te.has_name_iid()) {
            auto it = event_names.find(te.name_iid());
            if (it != event_names.end()) name = it->second;
        }

        const std::uint64_t track =
            te.has_track_uuid() ? te.track_uuid() : track_of(tp);
        auto& stack = stacks[track];
        if (type == perfetto::protos::pbzero::TrackEvent_Type::TYPE_SLICE_BEGIN) {
            SliceInfo si;
            si.name = std::move(name);
            si.track = track;
            for (auto ait = te.debug_annotations(); ait; ++ait) {
                const auto ann = *ait;
                DebugAnnotation::Decoder da(ann.data, ann.size);
                if (!da.has_name() || !da.has_uint_value()) continue;
                const std::string an = da.name().ToStdString();
                const auto val = da.uint_value();
                if (an == "frame") si.frame = val;
                if (an == "stable_node_id") si.stable_node_id = val;
            }
            stack.push_back(Open{si.name, si.frame, si.stable_node_id, running_ts});
        } else {
            if (stack.empty()) continue;
            const Open top = stack.back();
            stack.pop_back();
            const double ms = static_cast<double>(running_ts - top.ts) / 1e6;
            if (top.ts < span_first) span_first = top.ts;
            if (running_ts > span_last) span_last = running_ts;
            ++slice_count;
            by_name[top.name].total_ms += ms;
            by_name[top.name].count++;
            if (top.frame != 0) {
                frames[top.frame].total_ms += ms;
                frames[top.frame].count++;
            }
            if (top.name == "node_execute" && top.sid != 0) {
                nodes[top.sid].total_ms += ms;
                nodes[top.sid].count++;
                ++node_calls_annotated;
            }
        }
    }

    const double span_ms =
        (span_first == ~0ULL) ? 0.0 : (span_last - span_first) / 1e6;
    std::printf("== trace report: %s ==\n", path);
    std::printf("  span: %.1f ms (%llu .. %llu ns), %llu slices\n",
                span_ms, static_cast<unsigned long long>(span_first),
                static_cast<unsigned long long>(span_last),
                static_cast<unsigned long long>(slice_count));
    std::printf("\n");

    std::printf("== top %d worst frames (annotated total slice time) ==\n",
                top_frames);
    std::printf("  %-6s %-8s %-10s %-10s\n", "rank", "frame", "slices", "total_ms");
    std::vector<std::pair<std::uint64_t, Aggregate>> frame_list(frames.begin(),
                                                                frames.end());
    std::sort(frame_list.begin(), frame_list.end(),
              [](const auto& a, const auto& b) {
                  return a.second.total_ms > b.second.total_ms;
              });
    int shown = 0;
    for (const auto& [f, agg] : frame_list) {
        if (shown++ >= top_frames) break;
        std::printf("  %-6d %-8llu %-10llu %-10.3f\n", shown, f,
                    static_cast<unsigned long long>(agg.count), agg.total_ms);
    }
    if (frame_list.empty()) {
        std::printf("  (no `frame` annotations found in this trace)\n");
    }
    std::printf("\n");

    std::printf("== most expensive nodes (node_execute) ==\n");
    if (node_calls_annotated == 0) {
        std::printf("  (no stable_node_id annotations; grouping by slice name)\n");
        std::vector<std::pair<std::string, Aggregate>> by_name_list(by_name.begin(),
                                                                    by_name.end());
        std::sort(by_name_list.begin(), by_name_list.end(),
                  [](const auto& a, const auto& b) {
                      return a.second.total_ms > b.second.total_ms;
                  });
        std::printf("  %-6s %-28s %-8s %-10s %-10s\n",
                    "rank", "slice", "calls", "total_ms", "avg_ms");
        shown = 0;
        for (const auto& [n, agg] : by_name_list) {
            if (shown++ >= top_nodes) break;
            std::printf("  %-6d %-28s %-8llu %-10.3f %-10.3f\n", shown,
                        n.c_str(), static_cast<unsigned long long>(agg.count),
                        agg.total_ms,
                        agg.count ? agg.total_ms / static_cast<double>(agg.count) : 0.0);
        }
    } else {
        std::vector<std::pair<std::uint64_t, Aggregate>> node_list(nodes.begin(),
                                                                   nodes.end());
        std::sort(node_list.begin(), node_list.end(),
                  [](const auto& a, const auto& b) {
                      return a.second.total_ms > b.second.total_ms;
                  });
        std::printf("  %-6s %-16s %-8s %-10s %-10s\n",
                    "rank", "stable_node_id", "calls", "total_ms", "avg_ms");
        shown = 0;
        for (const auto& [sid, agg] : node_list) {
            if (shown++ >= top_nodes) break;
            std::printf("  %-6d %-16llu %-8llu %-10.3f %-10.3f\n", shown,
                        static_cast<unsigned long long>(sid),
                        static_cast<unsigned long long>(agg.count),
                        agg.total_ms,
                        agg.count ? agg.total_ms / static_cast<double>(agg.count) : 0.0);
        }
    }
    return 0;
}
