// ============================================================================
// tools/trace_gates/trace_sql_report.cpp — plan §24 report tool
//
// Standalone .pftrace report without trace_processor: re-reads the trace with
// the Perfetto pbzero decoders shipped in the SDK header and prints
//
//   1. trace span + slice count
//   2. top-N worst frames — RenderFrame slice time grouped by the resolved
//      `frame` debug annotation
//   3. most expensive node — node_execute slices grouped by the resolved
//      `stable_node_id` debug annotation
//
// Perfetto's TrackEvent incremental state is packet-sequence scoped.  This
// decoder therefore keeps timestamps, defaults, and interned names/categories
// per trusted_packet_sequence_id instead of using one global accumulator.
//
// Usage: trace_sql_report <file.pftrace> [--top-frames N] [--top-nodes N]
// Exit codes: 0 = ok, 1 = parse/report error.
// ============================================================================

#include <perfetto.h>   // pbzero trace decoders (standalone tool, not public API)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

using perfetto::protos::pbzero::ClockSnapshot;
using perfetto::protos::pbzero::DebugAnnotation;
using perfetto::protos::pbzero::DebugAnnotationName;
using perfetto::protos::pbzero::EventCategory;
using perfetto::protos::pbzero::EventName;
using perfetto::protos::pbzero::InternedData;
using perfetto::protos::pbzero::Trace;
using perfetto::protos::pbzero::TracePacket;
using perfetto::protos::pbzero::TracePacketDefaults;
using perfetto::protos::pbzero::TrackEvent;
using perfetto::protos::pbzero::TrackEventDefaults;

namespace {

// These are the two synthetic clocks used by Perfetto TrackEvent.  They are
// intentionally kept local to the report tool: the public SDK does not expose
// TrackEventIncrementalState, but the values are part of the trace format.
constexpr std::uint32_t kClockIdIncremental = 64;
constexpr std::uint32_t kClockIdAbsolute = 65;

struct Aggregate {
    double total_ms{0};
    std::uint64_t count{0};
};

struct SliceInfo {
    std::string name;
    std::string category;
    bool has_frame{false};
    std::uint64_t frame{0};
    bool has_stable_node_id{false};
    std::uint64_t stable_node_id{0};
};

struct SequenceState {
    // TrackEvent packet timestamps use this sequence's incremental clock.
    std::uint64_t timestamp_ns{0};
    std::uint64_t timestamp_unit_multiplier_ns{1};
    std::uint32_t default_clock_id{kClockIdIncremental};
    std::uint64_t default_track_uuid{0};

    // All of these maps are incremental-state scoped, not trace scoped.
    std::map<std::uint64_t, std::string> event_names;
    std::map<std::uint64_t, std::string> event_categories;
    std::map<std::uint64_t, std::string> debug_annotation_names;
};

struct TrackKey {
    // Explicit track UUIDs are session-global. Default tracks are private to
    // their packet sequence, so sequence_id disambiguates them.
    std::uint32_t sequence_id{0};
    std::uint64_t track_uuid{0};

    bool operator<(const TrackKey& other) const {
        if (sequence_id != other.sequence_id) return sequence_id < other.sequence_id;
        return track_uuid < other.track_uuid;
    }
};

struct OpenSlice {
    SliceInfo info;
    std::uint64_t timestamp_ns{0};
};

std::string read_file(const char* path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ok = false;
        return {};
    }
    ok = true;
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

std::uint64_t saturating_mul(std::uint64_t value, std::uint64_t multiplier) {
    if (multiplier != 0 && value > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return value * multiplier;
}

void reset_incremental_state(SequenceState& state) {
    state.event_names.clear();
    state.event_categories.clear();
    state.debug_annotation_names.clear();
    state.default_track_uuid = 0;
    state.default_clock_id = kClockIdIncremental;
    // Perfetto's own tracker keeps the current incremental timestamp across a
    // state reset unless a ClockSnapshot establishes a new origin.
}

void consume_clock_snapshot(const TracePacket::Decoder& packet, SequenceState& state) {
    if (!packet.has_clock_snapshot()) return;

    ClockSnapshot::Decoder snapshot(packet.clock_snapshot());
    for (auto it = snapshot.clocks(); it; ++it) {
        ClockSnapshot::Clock::Decoder clock(*it);
        if (!clock.has_clock_id() || !clock.has_timestamp()) continue;
        if (clock.clock_id() != kClockIdIncremental || !clock.is_incremental()) continue;
        state.timestamp_unit_multiplier_ns =
            clock.has_unit_multiplier_ns() ? clock.unit_multiplier_ns() : 1;
        state.timestamp_ns = saturating_mul(
            clock.timestamp(), state.timestamp_unit_multiplier_ns);
        break;
    }
}

void consume_defaults(const TracePacket::Decoder& packet, SequenceState& state) {
    if (!packet.has_trace_packet_defaults()) return;

    TracePacketDefaults::Decoder defaults(packet.trace_packet_defaults());
    if (defaults.has_timestamp_clock_id()) {
        state.default_clock_id = defaults.timestamp_clock_id();
    }
    if (defaults.has_track_event_defaults()) {
        TrackEventDefaults::Decoder track_defaults(defaults.track_event_defaults());
        if (track_defaults.has_track_uuid()) {
            state.default_track_uuid = track_defaults.track_uuid();
        }
    }
}

void consume_interned_data(const TracePacket::Decoder& packet, SequenceState& state) {
    if (!packet.has_interned_data()) return;

    InternedData::Decoder interned(packet.interned_data());
    for (auto it = interned.event_names(); it; ++it) {
        EventName::Decoder entry(*it);
        if (entry.has_iid() && entry.has_name()) {
            state.event_names[entry.iid()] = entry.name().ToStdString();
        }
    }
    for (auto it = interned.event_categories(); it; ++it) {
        EventCategory::Decoder entry(*it);
        if (entry.has_iid() && entry.has_name()) {
            state.event_categories[entry.iid()] = entry.name().ToStdString();
        }
    }
    for (auto it = interned.debug_annotation_names(); it; ++it) {
        DebugAnnotationName::Decoder entry(*it);
        if (entry.has_iid() && entry.has_name()) {
            state.debug_annotation_names[entry.iid()] = entry.name().ToStdString();
        }
    }
}

std::uint64_t consume_packet_timestamp(
    const TracePacket::Decoder& packet, SequenceState& state) {
    if (!packet.has_timestamp()) return state.timestamp_ns;

    const std::uint32_t clock_id = packet.has_timestamp_clock_id()
        ? packet.timestamp_clock_id()
        : state.default_clock_id;
    const std::uint64_t raw_timestamp = packet.timestamp();

    if (clock_id == kClockIdIncremental) {
        state.timestamp_ns = std::min(
            std::numeric_limits<std::uint64_t>::max(),
            state.timestamp_ns +
                saturating_mul(raw_timestamp, state.timestamp_unit_multiplier_ns));
    } else if (clock_id == kClockIdAbsolute) {
        // Perfetto emits kClockIdAbsolute when a writer has to restart from an
        // absolute timestamp. Its value is encoded in the writer's units.
        state.timestamp_ns = saturating_mul(
            raw_timestamp, state.timestamp_unit_multiplier_ns);
    } else {
        // Built-in clocks in TracePacket are already expressed in nanoseconds.
        state.timestamp_ns = raw_timestamp;
    }
    return state.timestamp_ns;
}

std::string resolve_event_name(
    const TrackEvent::Decoder& event, const SequenceState& state) {
    if (event.has_name()) return event.name().ToStdString();
    if (event.has_name_iid()) {
        auto it = state.event_names.find(event.name_iid());
        if (it != state.event_names.end()) return it->second;
    }
    return {};
}

std::string resolve_category(
    const TrackEvent::Decoder& event, const SequenceState& state) {
    if (event.has_category_iids()) {
        for (auto it = event.category_iids(); it; ++it) {
            auto category = state.event_categories.find(*it);
            if (category != state.event_categories.end()) return category->second;
        }
    }
    if (event.has_categories()) {
        auto categories = event.categories();
        if (categories) {
            const auto category = *categories;
            return category.ToStdString();
        }
    }
    return {};
}

void read_annotations(
    const TrackEvent::Decoder& event,
    const SequenceState& state,
    SliceInfo& info) {
    for (auto it = event.debug_annotations(); it; ++it) {
        DebugAnnotation::Decoder annotation(*it);
        std::string name;
        if (annotation.has_name()) {
            name = annotation.name().ToStdString();
        } else if (annotation.has_name_iid()) {
            auto name_it = state.debug_annotation_names.find(annotation.name_iid());
            if (name_it != state.debug_annotation_names.end()) name = name_it->second;
        }
        if (!annotation.has_uint_value()) continue;
        if (name == "frame") {
            info.has_frame = true;
            info.frame = annotation.uint_value();
        } else if (name == "stable_node_id") {
            info.has_stable_node_id = true;
            info.stable_node_id = annotation.uint_value();
        }
    }
}

TrackKey track_key(
    std::uint32_t sequence_id,
    const TrackEvent::Decoder& event,
    const SequenceState& state) {
    if (event.has_track_uuid()) return TrackKey{0, event.track_uuid()};
    // The implicit/default track is the sequence-local stack. Its UUID can
    // change when incremental defaults are re-established, but an END packet
    // still closes the sequence's existing stack.
    (void)state;
    return TrackKey{sequence_id, 0};
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
    if (top_frames < 0 || top_nodes < 0) {
        std::fprintf(stderr, "report error: limits must be non-negative\n");
        return 1;
    }

    bool ok = false;
    const std::string bytes = read_file(path, ok);
    if (!ok) {
        std::fprintf(stderr, "report error: cannot open %s\n", path);
        return 1;
    }

    // Incremental state is scoped to trusted_packet_sequence_id.  A sequence
    // id is mandatory for TrackEvent packets, but packet 0 is a safe fallback
    // for malformed/hand-authored traces so the report remains fail-soft.
    std::map<std::uint32_t, SequenceState> sequences;
    std::map<TrackKey, std::vector<OpenSlice>> stacks;
    std::map<std::string, Aggregate> by_name;
    std::map<std::uint64_t, Aggregate> frames;
    std::map<std::uint64_t, Aggregate> nodes;

    std::uint64_t span_first = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t span_last = 0;
    std::uint64_t slice_count = 0;
    std::uint64_t unmatched_ends = 0;
    std::uint64_t incomplete_begins = 0;
    std::uint64_t node_calls_annotated = 0;

    Trace::Decoder trace(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    for (auto pit = trace.packet(); pit; ++pit) {
        const auto packet = *pit;
        TracePacket::Decoder trace_packet(packet.data, packet.size);
        const std::uint32_t sequence_id = trace_packet.has_trusted_packet_sequence_id()
            ? trace_packet.trusted_packet_sequence_id()
            : 0;
        SequenceState& sequence = sequences[sequence_id];

        if (trace_packet.has_sequence_flags() &&
            (trace_packet.sequence_flags() &
             perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED)) {
            reset_incremental_state(sequence);
        }
        // Match Perfetto's state-update order: clocks, reset/defaults, then
        // timestamp and interned metadata for the packet's event.
        consume_clock_snapshot(trace_packet, sequence);
        consume_defaults(trace_packet, sequence);
        const std::uint64_t timestamp_ns = consume_packet_timestamp(trace_packet, sequence);
        consume_interned_data(trace_packet, sequence);

        if (!trace_packet.has_track_event()) continue;
        TrackEvent::Decoder event(trace_packet.track_event());
        if (!event.has_type()) continue;
        const auto type = event.type();
        if (type != perfetto::protos::pbzero::TrackEvent_Type::TYPE_SLICE_BEGIN &&
            type != perfetto::protos::pbzero::TrackEvent_Type::TYPE_SLICE_END) {
            continue;
        }

        const TrackKey key = track_key(sequence_id, event, sequence);
        auto& stack = stacks[key];
        if (type == perfetto::protos::pbzero::TrackEvent_Type::TYPE_SLICE_BEGIN) {
            SliceInfo info;
            info.name = resolve_event_name(event, sequence);
            info.category = resolve_category(event, sequence);
            read_annotations(event, sequence, info);
            stack.push_back(OpenSlice{std::move(info), timestamp_ns});
            continue;
        }

        if (stack.empty()) {
            ++unmatched_ends;
            continue;
        }
        OpenSlice open = std::move(stack.back());
        stack.pop_back();
        if (timestamp_ns < open.timestamp_ns) {
            // A cross-clock or malformed trace must not turn unsigned
            // subtraction into a multi-million-year duration.
            ++unmatched_ends;
            continue;
        }

        const double ms = static_cast<double>(timestamp_ns - open.timestamp_ns) / 1e6;
        if (open.timestamp_ns < span_first) span_first = open.timestamp_ns;
        if (timestamp_ns > span_last) span_last = timestamp_ns;
        ++slice_count;
        by_name[open.info.name].total_ms += ms;
        by_name[open.info.name].count++;

        // The SQL contract is RenderFrame GROUP BY frame. Do not let nested
        // node/effect slices inflate the frame total or make frame 0 vanish.
        if (open.info.name == "RenderFrame" && open.info.has_frame) {
            frames[open.info.frame].total_ms += ms;
            frames[open.info.frame].count++;
        }
        if (open.info.name == "node_execute" && open.info.has_stable_node_id) {
            nodes[open.info.stable_node_id].total_ms += ms;
            nodes[open.info.stable_node_id].count++;
            ++node_calls_annotated;
        }
    }

    for (const auto& [key, stack] : stacks) {
        (void)key;
        incomplete_begins += stack.size();
    }

    const double span_ms = span_first == std::numeric_limits<std::uint64_t>::max()
        ? 0.0
        : static_cast<double>(span_last - span_first) / 1e6;
    std::printf("== trace report: %s ==\n", path);
    std::printf("  span: %.3f ms (%llu .. %llu ns), %llu slices\n",
                span_ms,
                static_cast<unsigned long long>(span_first),
                static_cast<unsigned long long>(span_last),
                static_cast<unsigned long long>(slice_count));
    std::printf("  sequences: %llu, unmatched_ends: %llu, incomplete_begins: %llu\n",
                static_cast<unsigned long long>(sequences.size()),
                static_cast<unsigned long long>(unmatched_ends),
                static_cast<unsigned long long>(incomplete_begins));
    std::printf("\n");

    std::printf("== top %d worst frames (RenderFrame annotated total slice time) ==\n",
                top_frames);
    std::printf("  %-6s %-8s %-10s %-10s\n", "rank", "frame", "slices", "total_ms");
    std::vector<std::pair<std::uint64_t, Aggregate>> frame_list(frames.begin(), frames.end());
    std::sort(frame_list.begin(), frame_list.end(),
              [](const auto& a, const auto& b) {
                  if (a.second.total_ms != b.second.total_ms) {
                      return a.second.total_ms > b.second.total_ms;
                  }
                  return a.first < b.first;
              });
    int shown = 0;
    for (const auto& [frame, aggregate] : frame_list) {
        if (shown++ >= top_frames) break;
        std::printf("  %-6d %-8llu %-10llu %-10.3f\n",
                    shown,
                    static_cast<unsigned long long>(frame),
                    static_cast<unsigned long long>(aggregate.count),
                    aggregate.total_ms);
    }
    if (frame_list.empty()) {
        std::printf("  (no annotated RenderFrame slices found)\n");
    }
    std::printf("\n");

    std::printf("== most expensive nodes (node_execute) ==\n");
    if (node_calls_annotated == 0) {
        std::printf("  (no stable_node_id annotations; grouping by slice name)\n");
        std::vector<std::pair<std::string, Aggregate>> by_name_list(by_name.begin(), by_name.end());
        std::sort(by_name_list.begin(), by_name_list.end(),
                  [](const auto& a, const auto& b) {
                      if (a.second.total_ms != b.second.total_ms) {
                          return a.second.total_ms > b.second.total_ms;
                      }
                      return a.first < b.first;
                  });
        std::printf("  %-6s %-28s %-8s %-10s %-10s\n",
                    "rank", "slice", "calls", "total_ms", "avg_ms");
        shown = 0;
        for (const auto& [name, aggregate] : by_name_list) {
            if (shown++ >= top_nodes) break;
            std::printf("  %-6d %-28s %-8llu %-10.3f %-10.3f\n",
                        shown,
                        name.c_str(),
                        static_cast<unsigned long long>(aggregate.count),
                        aggregate.total_ms,
                        aggregate.count
                            ? aggregate.total_ms / static_cast<double>(aggregate.count)
                            : 0.0);
        }
    } else {
        std::vector<std::pair<std::uint64_t, Aggregate>> node_list(nodes.begin(), nodes.end());
        std::sort(node_list.begin(), node_list.end(),
                  [](const auto& a, const auto& b) {
                      if (a.second.total_ms != b.second.total_ms) {
                          return a.second.total_ms > b.second.total_ms;
                      }
                      return a.first < b.first;
                  });
        std::printf("  %-6s %-16s %-8s %-10s %-10s\n",
                    "rank", "stable_node_id", "calls", "total_ms", "avg_ms");
        shown = 0;
        for (const auto& [stable_node_id, aggregate] : node_list) {
            if (shown++ >= top_nodes) break;
            std::printf("  %-6d %-16llu %-8llu %-10.3f %-10.3f\n",
                        shown,
                        static_cast<unsigned long long>(stable_node_id),
                        static_cast<unsigned long long>(aggregate.count),
                        aggregate.total_ms,
                        aggregate.count
                            ? aggregate.total_ms / static_cast<double>(aggregate.count)
                            : 0.0);
        }
    }
    return 0;
}
