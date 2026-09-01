// ── End-to-end IPC validation test ─────────────────────────────────
// Exercises the full ContractValidatorRegistry boundary:
// schema compile-once → thread-safe validate → error surface.
// Does NOT require the daemon binary or full pipeline link.
#include <doctest/doctest.h>

#include "src/ipc/contract_validator_registry.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <string>
#include <thread>

using namespace chronon3d::ipc;

// ── Helpers ─────────────────────────────────────────────────────────

static nlohmann::json make_valid_composition() {
    return {
        {"schema", "chronon.composition"},
        {"version", 1},
        {"id", "e2e-test-comp"},
        {"category", "proof"},
        {"width", 1920},
        {"height", 1080},
        {"fps", {{"numerator", 30}, {"denominator", 1}}},
        {"duration", 150}
    };
}

static nlohmann::json make_valid_render_plan() {
    return {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 1920}, {"height", 1080},
                     {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 150}}},
        {"layers", nlohmann::json::array()},
        {"output", {{"path", "/tmp/out.png"}, {"format", "png"}}}
    };
}

static nlohmann::json make_valid_render_settings() {
    return {
        {"schema", "chronon.render-settings"},
        {"version", 1},
        {"width", 1920},
        {"height", 1080},
        {"antialiasing_samples", 4},
        {"motion_blur", false},
        {"dirty_rects", false},
        {"deterministic", true}
    };
}

// ── Tests ───────────────────────────────────────────────────────────

TEST_CASE("e2e: builtin_contract_validators is singleton") {
    const auto& r1 = builtin_contract_validators();
    const auto& r2 = builtin_contract_validators();
    CHECK(&r1 == &r2);
    CHECK(r1.compiled_validator_count() == 3);
}

TEST_CASE("e2e: all three schemas pass valid documents") {
    const auto& v = builtin_contract_validators();

    SUBCASE("composition") {
        auto doc = make_valid_composition();
        ContractValidationError err;
        bool ok = v.validate(ContractId::CompositionV1, doc, &err);
        INFO("Error: ", err.contract, " — ", err.message);
        CHECK(ok);
    }

    SUBCASE("render_plan") {
        auto doc = make_valid_render_plan();
        ContractValidationError err;
        bool ok = v.validate(ContractId::RenderPlanV2, doc, &err);
        INFO("Error: ", err.contract, " — ", err.message);
        CHECK(ok);
    }

    SUBCASE("render_settings") {
        auto doc = make_valid_render_settings();
        ContractValidationError err;
        bool ok = v.validate(ContractId::RenderSettingsV1, doc, &err);
        INFO("Error: ", err.contract, " — ", err.message);
        CHECK(ok);
    }
}

TEST_CASE("e2e: type mismatch fails with structured error") {
    const auto& v = builtin_contract_validators();
    nlohmann::json doc = {
        {"schema", "chronon.composition"},
        {"version", 1},
        {"id", "bad-width"},
        {"width", "not_a_number"},
        {"height", 1080},
        {"duration", 150}
    };

    ContractValidationError err;
    bool ok = v.validate(ContractId::CompositionV1, doc, &err);
    CHECK(!ok);
    CHECK(err.contract == "chronon.composition.v1");
    CHECK(!err.message.empty());
}

TEST_CASE("e2e: missing required field 'id' fails") {
    const auto& v = builtin_contract_validators();
    nlohmann::json doc = {
        {"schema", "chronon.composition"},
        {"version", 1},
        {"width", 1920},
        {"height", 1080},
        {"fps", {{"numerator", 30}, {"denominator", 1}}},
        {"duration", 150}
    };

    ContractValidationError err;
    bool ok = v.validate(ContractId::CompositionV1, doc, &err);
    CHECK(!ok);
}

TEST_CASE("e2e: validate_json parses and validates string input") {
    const auto& v = builtin_contract_validators();
    std::string valid_json = make_valid_composition().dump();
    std::string invalid_json = R"({"schema":"chronon.composition","width":"bad","version":1,"id":"x"})";

    ContractValidationError err;
    CHECK(v.validate_json(ContractId::CompositionV1, valid_json, &err));
    CHECK(!v.validate_json(ContractId::CompositionV1, invalid_json, &err));
}

TEST_CASE("e2e: malformed JSON returns false with parse error") {
    const auto& v = builtin_contract_validators();
    ContractValidationError err;
    bool ok = v.validate_json(ContractId::CompositionV1, "not json at all", &err);
    CHECK(!ok);
    bool mentions_json = err.message.find("invalid JSON") != std::string::npos;
    bool mentions_parse = err.message.find("parse") != std::string::npos;
    CHECK((mentions_json || mentions_parse));
}

TEST_CASE("e2e: unknown contract ID returns false") {
    const auto& v = builtin_contract_validators();
    auto unknown = static_cast<ContractId>(99);
    ContractValidationError err;
    bool ok = v.validate(unknown, nlohmann::json::object(), &err);
    CHECK(!ok);
    CHECK(err.message == "validator is not registered");
}

TEST_CASE("e2e: concurrent validation from 4 threads, 4000 iterations") {
    const auto& v = builtin_contract_validators();
    auto doc = make_valid_composition();

    std::atomic<int> pass_count{0};
    std::atomic<int> fail_count{0};

    auto worker = [&]() {
        for (int i = 0; i < 1000; ++i) {
            ContractValidationError err;
            if (v.validate(ContractId::CompositionV1, doc, &err)) {
                pass_count.fetch_add(1);
            } else {
                fail_count.fetch_add(1);
            }
        }
    };

    std::thread t1(worker);
    std::thread t2(worker);
    std::thread t3(worker);
    std::thread t4(worker);
    t1.join(); t2.join(); t3.join(); t4.join();

    CHECK(pass_count.load() == 4000);
    CHECK(fail_count.load() == 0);
}

TEST_CASE("e2e: additionalProperties rejected") {
    const auto& v = builtin_contract_validators();
    auto doc = make_valid_composition();
    doc["extra_field"] = "should be rejected";

    ContractValidationError err;
    bool ok = v.validate(ContractId::CompositionV1, doc, &err);
    CHECK(!ok);
    bool mentions_extra = err.message.find("additional") != std::string::npos;
    bool mentions_field = err.message.find("extra_field") != std::string::npos;
    CHECK((mentions_extra || mentions_field));
}

TEST_CASE("e2e: invalid render-settings field fails") {
    const auto& v = builtin_contract_validators();
    nlohmann::json doc = {
        {"schema", "chronon.render-settings"},
        {"version", 1},
        {"width", -1},  // negative width
        {"height", 1080}
    };

    ContractValidationError err;
    bool ok = v.validate(ContractId::RenderSettingsV1, doc, &err);
    CHECK(!ok);
}