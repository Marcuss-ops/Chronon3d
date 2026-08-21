// ============================================================================
// tools/trace_gates/trace_correctness_check.cpp
//
// Plan §23 correctness gate: a 10-frame render must produce a trace with
//
//   - 10 "DecodeFrame" slices (chronon.media,   non-terminating flow)
//   - 10 "RenderFrame" slices (chronon.frame,   non-terminating flow)
//   - 10 "EncodeFrame" slices (chronon.encode,  terminating flow)
//   - the same MakeFlowId(job, frame) chaining decode -> render -> encode
//   - no missing frame_id across the three stages (0..9 all present)
//
// The three stages run on three separate threads through the REAL macros
// (CHRONON_TRACE_FLOW_IDS / CHRONON_TRACE_FLOW_END_IDS) and a REAL
// TraceSession (RING_BUFFER, job-end write).  After finish(), the same
// binary re-reads the .pftrace with the Perfetto pbzero decoders shipped in
// the SDK header and asserts the structure — no trace_processor needed.
//
// Exit codes: 0 = GATE_PASS, 1 = GATE_FAIL (details on stdout/stderr).
// ============================================================================

#include "chronon3d/core/tracing/tracing.hpp"
#include "chronon3d/core/tracing/tracing_categories.hpp"
#include "chronon3d/core/tracing/trace_session.hpp"
#include "chronon3d/core/tracing/trace_ids.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using chronon3d::tracing::MakeFlowId;

namespace {
constexpr std::uint64_t kJobId = 0x1234'5678ULL;
constexpr int kFrameCount = 10;
constexpr const char* kStages[3] = {"DecodeFrame", "RenderFrame", "EncodeFrame"};

struct SliceRecord {
    std::uint64_t flow{0};
    std::uint64_t term_flow{0};
    int count{0};
    std::uint64_t track{0};
};

// ── .pftrace inspection via the SDK pbzero decoders ─────────────────────
bool inspect_trace(const std::string& path, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "cannot open " + path; return false; }
    const std::string bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

    using perfetto::protos::pbzero::Trace;
    using perfetto::protos::pbzero::TracePacket;
    using perfetto::protos::pbzero::TrackEvent;
    using perfetto::protos::pbzero::InternedData;
    using perfetto::protos::pbzero::EventName;
    using perfetto::protos::pbzero::DebugAnnotation;

    std::map<std::uint64_t, std::string> event_names;   // interned names
    std::map<std::string, std::map<int, SliceRecord>> slices;
    std::map<std::string, std::set<std::uint64_t>> flow_ids_by_stage_frame;
    std::set<int> frames_seen;

    Trace::Decoder trace(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    // NOTE: this amalgamated SDK ships pbzero iterators without free
    // begin()/end(), so every repeated field is walked explicitly.
    for (auto pit = trace.packet(); pit; ++pit) {
        const auto packet = *pit;
        TracePacket::Decoder tp(packet.data, packet.size);
        if (tp.has_incremental_state_cleared() && tp.incremental_state_cleared()) {
            event_names.clear();
        }
        if (tp.has_interned_data()) {
            InternedData::Decoder id(tp.interned_data().data, tp.interned_data().size);
            for (auto eit = id.event_names(); eit; ++eit) {
                const auto ev = *eit;
                EventName::Decoder e(ev.data, ev.size);
                event_names[e.iid()] = e.name().ToStdString();
            }
        }
        if (!tp.has_track_event()) continue;
        TrackEvent::Decoder te(tp.track_event().data, tp.track_event().size);
        if (te.type() != perfetto::protos::pbzero::TrackEvent_Type::TYPE_SLICE_BEGIN) {
            continue;
        }
        std::string name;
        if (te.has_name()) name = te.name().ToStdString();
        else if (te.has_name_iid()) {
            auto it = event_names.find(te.name_iid());
            if (it != event_names.end()) name = it->second;
        }
        if (name.empty()) continue;

        // job / frame debug annotations
        std::uint64_t job = 0, frame = 0;
        bool have_job = false, have_frame = false;
        for (auto ait = te.debug_annotations(); ait; ++ait) {
            const auto ann = *ait;
            DebugAnnotation::Decoder da(ann.data, ann.size);
            if (!da.has_name()) continue;
            const std::string an = da.name().ToStdString();
            if (an == "job" && da.has_uint_value()) { job = da.uint_value(); have_job = true; }
            if (an == "frame" && da.has_uint_value()) { frame = da.uint_value(); have_frame = true; }
        }

        SliceRecord rec;
        for (auto fit = te.flow_ids(); fit; ++fit) rec.flow = *fit;        // non-terminating
        for (auto fit = te.terminating_flow_ids(); fit; ++fit) rec.term_flow = *fit;

        if (name == "DecodeFrame" || name == "RenderFrame" || name == "EncodeFrame") {
            if (have_frame) frames_seen.insert(static_cast<int>(frame));
            slices[name][static_cast<int>(frame)].count++;
            if (rec.flow) slices[name][static_cast<int>(frame)].flow = rec.flow;
            if (rec.term_flow) slices[name][static_cast<int>(frame)].term_flow = rec.term_flow;
            slices[name][static_cast<int>(frame)].track = te.track_uuid();
            if (have_frame) {
                flow_ids_by_stage_frame[name].insert(rec.flow ? rec.flow : rec.term_flow);
            }
            if (!have_job || job != kJobId) {
                err = "stage " + name + " frame " + std::to_string(frame) +
                      ": missing/mismatched job annotation";
                return false;
            }
        }
    }

    // 1. Exactly 10 begin slices per stage, one per frame 0..9.
    for (const char* stage : kStages) {
        const std::string s(stage);
        auto it = slices.find(s);
        if (it == slices.end() || it->second.size() != kFrameCount) {
            err = "stage " + s + ": expected " + std::to_string(kFrameCount) +
                  " frames, found " + std::to_string(it == slices.end() ? 0 : it->second.size());
            return false;
        }
        for (int f = 0; f < kFrameCount; ++f) {
            auto rec = it->second.find(f);
            if (rec == it->second.end() || rec->second.count != 1) {
                err = "stage " + s + ": frame " + std::to_string(f) + " missing or duplicated";
                return false;
            }
        }
    }

    // 2. Flow semantics: decode/render non-terminating, encode terminating;
    //    the same flow id chains all three stages per frame.
    for (int f = 0; f < kFrameCount; ++f) {
        const auto& dec = slices["DecodeFrame"][f];
        const auto& ren = slices["RenderFrame"][f];
        const auto& enc = slices["EncodeFrame"][f];
        if (dec.flow == 0 || ren.flow == 0 || dec.term_flow != 0 || ren.term_flow != 0) {
            err = "frame " + std::to_string(f) + ": decode/render must carry a "
                  "non-terminating flow only";
            return false;
        }
        if (enc.term_flow == 0 || enc.flow != 0) {
            err = "frame " + std::to_string(f) + ": encode must carry a terminating flow";
            return false;
        }
        if (dec.flow != ren.flow || ren.flow != enc.term_flow) {
            err = "frame " + std::to_string(f) + ": flow chain broken (decode=" +
                  std::to_string(dec.flow) + " render=" + std::to_string(ren.flow) +
                  " encode=" + std::to_string(enc.term_flow) + ")";
            return false;
        }
    }

    // 3. No missing frame_id: the union of annotated frames is exactly 0..9.
    if (static_cast<int>(frames_seen.size()) != kFrameCount) {
        err = "missing frame_id: expected " + std::to_string(kFrameCount) +
              " distinct frames, found " + std::to_string(frames_seen.size());
        return false;
    }
    for (int f = 0; f < kFrameCount; ++f) {
        if (frames_seen.count(f) == 0) {
            err = "missing frame_id " + std::to_string(f);
            return false;
        }
    }

    std::printf("  trace: 30 slices (10 DecodeFrame + 10 RenderFrame + 10 EncodeFrame)\n");
    std::printf("  flows: per-frame chain decode=%s render=%s encode=%s\n",
                "non-terminating", "non-terminating", "terminating");
    std::printf("  frames: 0..%d all present, flow id chains intact\n", kFrameCount - 1);
    return true;
}
} // namespace

int main() {
    const std::string output = "/tmp/chronon3d_trace_gate.pftrace";

    using namespace chronon3d::trace;
    TraceOptions o;
    o.enabled = true;
    o.output = output;
    o.level = TraceLevel::kFull;   // all categories (debug/slow included)
    o.buffer_mb = 8;
    TraceSession s;
    auto r = s.start(o);
    if (!r) {
        std::printf("GATE_FAIL: TraceSession start failed (%d)\n", (int)r.error());
        return 1;
    }

    // Three stages, three threads, real macros + shared MakeFlowId(job, frame).
    // The event name MUST be a static string literal (plan §7 — no dynamic
    // names in TRACE_EVENT), hence one explicit loop per stage.
    std::thread decode([] {
        for (int f = 0; f < kFrameCount; ++f) {
            const auto flow = MakeFlowId(kJobId, static_cast<std::uint64_t>(f));
            CHRONON_TRACE_FLOW_IDS("chronon.media", "DecodeFrame", flow,
                                   kJobId, static_cast<std::uint64_t>(f));
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    std::thread render([] {
        for (int f = 0; f < kFrameCount; ++f) {
            const auto flow = MakeFlowId(kJobId, static_cast<std::uint64_t>(f));
            CHRONON_TRACE_FLOW_IDS("chronon.frame", "RenderFrame", flow,
                                   kJobId, static_cast<std::uint64_t>(f));
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    std::thread encode([] {
        for (int f = 0; f < kFrameCount; ++f) {
            const auto flow = MakeFlowId(kJobId, static_cast<std::uint64_t>(f));
            CHRONON_TRACE_FLOW_END_IDS("chronon.encode", "EncodeFrame", flow,
                                       kJobId, static_cast<std::uint64_t>(f));
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    decode.join();
    render.join();
    encode.join();

    auto f = s.finish();
    if (!f) {
        std::printf("GATE_FAIL: TraceSession finish failed (%d)\n", (int)f.error());
        return 1;
    }

    std::string err;
    if (!inspect_trace(output, err)) {
        std::printf("GATE_FAIL: %s\n", err.c_str());
        return 1;
    }
    std::printf("GATE_PASS: correctness gate — 10-frame trace structure verified\n");
    return 0;
}
