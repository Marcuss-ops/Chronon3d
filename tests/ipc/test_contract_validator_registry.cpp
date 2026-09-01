// ---------------------------------------------------------------------------
// tests/ipc/test_contract_validator_registry.cpp
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include "src/ipc/contract_validator_registry.hpp"

#include <nlohmann/json.hpp>

#include <future>
#include <string>
#include <vector>

namespace {

nlohmann::json composition() {
    return {
        {"schema", "chronon.composition"},
        {"version", 1},
        {"id", "demo"},
        {"category", "test"},
        {"width", 640},
        {"height", 360},
        {"fps", {{"numerator", 30}, {"denominator", 1}}},
        {"duration", 30},
    };
}

nlohmann::json render_plan() {
    return {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {
            {"width", 640}, {"height", 360}, {"fps_num", 30},
            {"fps_den", 1}, {"duration_frames", 30}
        }},
        {"layers", nlohmann::json::array({
            {{"id", "background"}, {"type", "color"},
             {"color", {0.0, 0.0, 0.0, 1.0}}}
        })},
        {"output", {{"path", "frame.png"}, {"format", "png"}}},
    };
}

nlohmann::json render_settings() {
    return {
        {"schema", "chronon.render-settings"},
        {"version", 1},
        {"width", 640},
        {"height", 360},
        {"antialiasing_samples", 1},
        {"motion_blur", false},
        {"dirty_rects", false},
        {"deterministic", true},
    };
}

} // namespace

TEST_CASE("IPC contract registry compiles the three schemas once") {
    const auto& first = chronon3d::ipc::builtin_contract_validators();
    const auto& second = chronon3d::ipc::builtin_contract_validators();

    CHECK(&first == &second);
    CHECK(first.compiled_validator_count() == 3);
    CHECK(first.validate(chronon3d::ipc::ContractId::CompositionV1, composition()));
    CHECK(first.validate(chronon3d::ipc::ContractId::RenderPlanV2, render_plan()));
    CHECK(first.validate(chronon3d::ipc::ContractId::RenderSettingsV1, render_settings()));
}

TEST_CASE("IPC contract registry rejects structural drift") {
    const auto& registry = chronon3d::ipc::builtin_contract_validators();
    chronon3d::ipc::ContractValidationError error;

    auto bad_composition = composition();
    bad_composition["unexpected"] = true;
    CHECK_FALSE(registry.validate(
        chronon3d::ipc::ContractId::CompositionV1, bad_composition, &error));
    CHECK(error.contract == "chronon.composition.v1");
    CHECK_FALSE(error.message.empty());

    auto bad_settings = render_settings();
    bad_settings["width"] = 0;
    CHECK_FALSE(registry.validate(
        chronon3d::ipc::ContractId::RenderSettingsV1, bad_settings, &error));
    CHECK(error.contract == "chronon.render-settings.v1");

    auto bad_plan = render_plan();
    bad_plan["output"].erase("path");
    CHECK_FALSE(registry.validate(
        chronon3d::ipc::ContractId::RenderPlanV2, bad_plan, &error));
    CHECK(error.contract == "chronon.render-plan.v2");
}

TEST_CASE("IPC contract registry supports concurrent request validation") {
    const auto& registry = chronon3d::ipc::builtin_contract_validators();
    std::vector<std::future<bool>> requests;
    requests.reserve(12);

    for (int index = 0; index < 12; ++index) {
        requests.push_back(std::async(std::launch::async, [&registry, index] {
            const auto document = index % 3 == 0
                ? composition()
                : index % 3 == 1 ? render_plan() : render_settings();
            const auto contract = index % 3 == 0
                ? chronon3d::ipc::ContractId::CompositionV1
                : index % 3 == 1
                    ? chronon3d::ipc::ContractId::RenderPlanV2
                    : chronon3d::ipc::ContractId::RenderSettingsV1;
            return registry.validate(contract, document);
        }));
    }

    for (auto& request : requests) CHECK(request.get());
}
