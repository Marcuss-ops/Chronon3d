#pragma once

#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>

#include <cstddef>
#include <functional>
#include <string>

namespace chronon3d::media {

/// Operations supplied by the concrete decode/render/encode owner. The
/// executor owns ordering and validation only; callbacks must submit all
/// produced packets to the MuxSession passed to them.
struct GopExecutionCallbacks {
    std::function<bool(const GopWorkUnit&, MuxSession&)> copy_packets;
    std::function<bool(const GopWorkUnit&, MuxSession&)> reencode;
};

struct GopExecutionResult {
    std::size_t copied_gops{0};
    std::size_t reencoded_gops{0};
};

/// Execute the compiler-owned GOP schedule in source order. This class does
/// not resolve policy and never creates a mux: copied and encoded packets are
/// deliberately routed through the same caller-owned MuxSession.
class GopExecutionExecutor final {
public:
    [[nodiscard]] static bool execute(
        const GopExecutionPlan& plan,
        MuxSession& mux,
        const GopExecutionCallbacks& callbacks,
        GopExecutionResult& result,
        std::string& reason) noexcept;
};

} // namespace chronon3d::media
