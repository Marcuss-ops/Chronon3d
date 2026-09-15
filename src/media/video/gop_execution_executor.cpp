#include <chronon3d/media/video/gop_execution_executor.hpp>

#include <spdlog/spdlog.h>

#include <exception>
#include <string>

namespace chronon3d::media {
namespace {

bool valid_schedule(const GopExecutionPlan& plan, std::string& reason) {
    if (plan.work_units.empty()) {
        reason = "gop schedule is empty";
        return false;
    }
    if (plan.total_gops != 0 && plan.work_units.size() != plan.total_gops) {
        reason = "gop schedule count does not match total_gops";
        return false;
    }

    std::size_t copies = 0;
    std::size_t reencodes = 0;
    for (std::size_t index = 0; index < plan.work_units.size(); ++index) {
        const auto& unit = plan.work_units[index];
        if (unit.ordinal != index) {
            reason = "gop schedule ordinals are not contiguous";
            return false;
        }
        if (unit.first_pts > unit.last_pts || unit.first_dts > unit.last_dts) {
            reason = "gop schedule timestamp envelope is inverted";
            return false;
        }
        if (!unit.source_packets.valid()) {
            reason = "gop schedule is missing the source packet range";
            return false;
        }
        if (unit.copies_packets()) {
            if (!unit.compatibility.safe_to_splice()) {
                reason = "copied gop has no verified splice compatibility";
                return false;
            }
            ++copies;
        } else {
            if (!unit.decode_range.valid() || !unit.render_range.valid() ||
                !unit.encode_range.valid()) {
                reason = "reencode gop is missing an execution range";
                return false;
            }
            ++reencodes;
        }
    }
    if (plan.copy_gops != 0 && copies != plan.copy_gops) {
        reason = "copied gop count does not match schedule";
        return false;
    }
    if (plan.reencode_gops != 0 && reencodes != plan.reencode_gops) {
        reason = "reencoded gop count does not match schedule";
        return false;
    }
    if (plan.hybrid != (copies > 0 && reencodes > 0)) {
        reason = "hybrid flag does not match schedule";
        return false;
    }
    if (copies > 0 && !plan.safe_to_splice) {
        reason = "schedule contains copied gops but is not splice-certified";
        return false;
    }
    if (plan.all_copy != (copies > 0 && reencodes == 0)) {
        reason = "all_copy flag does not match schedule";
        return false;
    }
    return true;
}

} // namespace

bool GopExecutionExecutor::execute(
    const GopExecutionPlan& plan,
    MuxSession& mux,
    const GopExecutionCallbacks& callbacks,
    GopExecutionResult& result,
    std::string& reason) noexcept {
    result = {};
    reason.clear();
    try {
        if (!valid_schedule(plan, reason)) return false;
        for (const auto& unit : plan.work_units) {
            const auto& operation = unit.copies_packets()
                ? callbacks.copy_packets : callbacks.reencode;
            if (!operation) {
                reason = unit.copies_packets()
                    ? "copy callback is not installed"
                    : "reencode callback is not installed";
                return false;
            }
            if (!operation(unit, mux)) {
                reason = unit.copies_packets()
                    ? "copy callback failed"
                    : "reencode callback failed";
                return false;
            }
            if (unit.copies_packets()) ++result.copied_gops;
            else ++result.reencoded_gops;
        }
        return true;
    } catch (const std::exception& exception) {
        // AUDIT F17 — a throwing callback used to degrade to a bare `reason`
        // string with no log at the catch point, so an unexpected exception was
        // indistinguishable from a planned "callback returned false" failure in
        // the downstream structured-downgrade report. It is still reported as
        // `false` (execute() is noexcept and the caller formats `reason`), but
        // the throw is now named, logged, and classified as `threw`.
        reason = std::string{"gop executor callback threw: "} + exception.what();
        spdlog::error("[gop-executor] callback threw: {}", exception.what());
        return false;
    } catch (...) {
        reason = "gop executor callback threw (unknown exception)";
        spdlog::error("[gop-executor] callback threw an unknown exception");
        return false;
    }
}

} // namespace chronon3d::media
