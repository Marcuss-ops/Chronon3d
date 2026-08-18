#include <doctest/doctest.h>

#include <chronon3d/c_api/chronon3d.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

TEST_CASE("C ABI reports the v2 contract") {
    CHECK(chronon_abi_version() == 2);
    CHECK(std::string(chronon_status_name(CHRONON_ERROR_INVALID_PLAN)) ==
          "INVALID_PLAN");
    CHECK(std::string(chronon_status_name(CHRONON_ERROR_ABI_MISMATCH)) ==
          "ABI_MISMATCH");
}

TEST_CASE("C ABI v2 exposes structured configuration diagnostics") {
    chronon_engine_config config{
        sizeof(config), chronon_abi_version() + 1u, nullptr, 0};
    chronon_error_info error{sizeof(error), CHRONON_OK, nullptr};
    chronon_engine* engine = nullptr;

    CHECK(chronon_engine_create_v2(&config, &engine, &error) ==
          CHRONON_ERROR_ABI_MISMATCH);
    CHECK(engine == nullptr);
    CHECK(error.status == CHRONON_ERROR_ABI_MISMATCH);
    REQUIRE(error.message != nullptr);
    CHECK(std::string(error.message).find("ABI version mismatch") !=
          std::string::npos);
    REQUIRE(error.code != nullptr);
    CHECK(std::string(error.code) == "ABI_MISMATCH");
    REQUIRE(error.component != nullptr);
    CHECK(std::string(error.component) == "c_api");
}

TEST_CASE("C ABI v2 exposes the engine's last structured error") {
    chronon_engine_config config{
        sizeof(config), chronon_abi_version(), nullptr, 0};
    chronon_error_info error{sizeof(error), CHRONON_OK, nullptr};
    chronon_engine* engine = nullptr;
    REQUIRE(chronon_engine_create_v2(&config, &engine, &error) == CHRONON_OK);
    REQUIRE(engine != nullptr);

    const std::string invalid =
        R"({"schema":"chronon.render-plan","version":1,"layers":[],"output":{"path":"out.png"}})";
    chronon_plan* plan = nullptr;
    CHECK(chronon_plan_compile_json_n(
              engine, invalid.data(), invalid.size(), &plan) != CHRONON_OK);

    chronon_error_info info{sizeof(info), CHRONON_OK, nullptr};
    CHECK(chronon_engine_last_error_info(engine, &info) == CHRONON_OK);
    CHECK(info.status != CHRONON_OK);
    REQUIRE(info.message != nullptr);
    CHECK(info.message[0] != '\0');
    REQUIRE(info.code != nullptr);
    CHECK(std::string(info.code) == "PARSE_FAILED");
    REQUIRE(info.component != nullptr);
    CHECK(std::string(info.component) == "render_plan");

    chronon_engine_destroy(engine);
}

TEST_CASE("C ABI v2 maps a missing asset to ASSET_NOT_FOUND") {
    // AssetResolver::mount() requires an ABSOLUTE root; the CWD is absolute.
    const std::string cwd = std::filesystem::current_path().string();
    chronon_engine_config config{
        sizeof(config), chronon_abi_version(), cwd.c_str(), 0};
    chronon_error_info error{sizeof(error), CHRONON_OK, nullptr};
    chronon_engine* engine = nullptr;
    REQUIRE(chronon_engine_create_v2(&config, &engine, &error) == CHRONON_OK);
    REQUIRE(engine != nullptr);

    const std::string missing =
        R"({"schema":"chronon.render-plan","version":1,"canvas":{"width":32,"height":32,"fps":30,"duration_frames":1},"layers":[{"id":"img","type":"image","asset":"cabi_missing_asset_never_exists.png"}],"output":{"path":"out.png"}})";
    chronon_plan* plan = nullptr;
    const chronon_status status = chronon_plan_compile_json_n(
        engine, missing.data(), missing.size(), &plan);
    CHECK(status == CHRONON_ERROR_ASSET_NOT_FOUND);

    chronon_error_info info{sizeof(info), CHRONON_OK, nullptr};
    CHECK(chronon_engine_last_error_info(engine, &info) == CHRONON_OK);
    CHECK(info.status == CHRONON_ERROR_ASSET_NOT_FOUND);
    REQUIRE(info.code != nullptr);
    CHECK(std::string(info.code) == "ASSET_NOT_FOUND");
    REQUIRE(info.asset != nullptr);
    CHECK(std::string(info.asset) == "cabi_missing_asset_never_exists.png");
    REQUIRE(info.component != nullptr);
    CHECK(std::string(info.component) == "asset_resolver");

    chronon_engine_destroy(engine);
}

constexpr char kPlan[] =
    "{\"schema\":\"chronon.render-plan\",\"version\":1,"
    "\"canvas\":{\"width\":320,\"height\":180,\"fps\":30,"
    "\"duration_frames\":2},\"layers\":[{\"id\":\"bg\","
    "\"type\":\"color\",\"color\":[0.2,0.4,0.6,1.0]}],"
    "\"output\":{\"path\":\"out.png\"}}";

struct EngineFixture {
    chronon_engine* engine{nullptr};
    chronon_plan* plan{nullptr};

    explicit EngineFixture(std::string_view plan_source = kPlan) {
        chronon_engine_config config{
            sizeof(config), chronon_abi_version(), nullptr, 0};
        chronon_error_info error{sizeof(error), CHRONON_OK, nullptr};
        REQUIRE(chronon_engine_create_v2(&config, &engine, &error) == CHRONON_OK);
        REQUIRE(engine != nullptr);
        REQUIRE(chronon_plan_compile_json_n(
            engine, plan_source.data(), plan_source.size(), &plan) == CHRONON_OK);
        REQUIRE(plan != nullptr);
    }

    ~EngineFixture() {
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
    }

    EngineFixture(const EngineFixture&) = delete;
    EngineFixture& operator=(const EngineFixture&) = delete;
};

std::filesystem::path unique_output_path(const char* name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string("chronon3d_c_api_") + name + "_" + std::to_string(stamp) + ".mp4");
}

bool has_sdk_temp_sibling(const std::filesystem::path& output) {
    const auto parent = output.parent_path();
    const auto stem = output.filename().string();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(parent, error)) {
        if (error) return false;
        const auto name = entry.path().filename().string();
        if (name.find(stem + ".chronon.tmp.") == 0) return true;
    }
    return false;
}

} // namespace

TEST_CASE("C ABI v2 create/destroy and explicit JSON length") {
    chronon_engine_config config{
        sizeof(config), chronon_abi_version(), nullptr, 0};
    chronon_error_info error{sizeof(error), CHRONON_OK, nullptr};
    chronon_engine* engine = nullptr;
    REQUIRE(chronon_engine_create_v2(&config, &engine, &error) == CHRONON_OK);
    REQUIRE(engine != nullptr);

    std::string source{kPlan};
    source += " trailing bytes are not part of the JSON payload";
    chronon_plan* plan = nullptr;
    CHECK(chronon_plan_compile_json_n(
              engine, source.data(), sizeof(kPlan) - 1, &plan) == CHRONON_OK);
    CHECK(plan != nullptr);

    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);
    chronon_engine_destroy(nullptr);
}

TEST_CASE("C ABI v2 rejects a prepared asset changed before render") {
    const auto source_root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path();
    const auto temp_root = std::filesystem::temp_directory_path() /
        ("chronon3d_c_api_asset_change_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp_root / "assets");
    std::filesystem::copy_file(
        source_root / "assets/test_image.png",
        temp_root / "assets/test_image.png",
        std::filesystem::copy_options::overwrite_existing);

    chronon_engine* engine = nullptr;
    chronon_plan* plan = nullptr;
    const auto root_string = temp_root.string();
    chronon_engine_config config{
        sizeof(config), chronon_abi_version(), root_string.c_str(), 0};
    chronon_error_info error{sizeof(error), CHRONON_OK, nullptr};
    REQUIRE(chronon_engine_create_v2(&config, &engine, &error) == CHRONON_OK);

    const std::string source =
        R"({"schema":"chronon.render-plan","version":1,"canvas":{"width":32,"height":32,"fps":30,"duration_frames":1},"layers":[{"id":"image","type":"image","asset":"assets/test_image.png"}],"output":{"path":"out.png"}})";
    REQUIRE(chronon_plan_compile_json_n(
        engine, source.data(), source.size(), &plan) == CHRONON_OK);

    std::ofstream changed(temp_root / "assets/test_image.png",
                          std::ios::binary | std::ios::trunc);
    changed << "changed-after-preflight";
    changed.close();

    chronon_frame_info info{};
    CHECK(chronon_render_frame_into(engine, plan, 0, nullptr, 0, &info) ==
          CHRONON_ERROR_ASSET_CHANGED);
    CHECK(std::string(chronon_engine_last_error(engine)).find(
              "assets/test_image.png") != std::string::npos);

    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);
    std::error_code cleanup_error;
    std::filesystem::remove_all(temp_root, cleanup_error);
}

TEST_CASE("C ABI v2 caller-owned buffer query, too-small failure, and second render") {
    EngineFixture fixture;
    chronon_frame_info info{};

    CHECK(chronon_render_frame_into(
              fixture.engine, fixture.plan, 0, nullptr, 0, &info) ==
          CHRONON_ERROR_BUFFER_TOO_SMALL);
    REQUIRE(info.size > 0);
    CHECK(info.width == 320);
    CHECK(info.height == 180);
    CHECK(info.stride == 320u * 4u);
    CHECK(info.pixel_format != 0);

    std::vector<std::uint8_t> too_small(static_cast<std::size_t>(info.size) - 1u, 0xA5);
    chronon_frame_info failed_info{};
    CHECK(chronon_render_frame_into(
              fixture.engine, fixture.plan, 0, too_small.data(), too_small.size(),
              &failed_info) == CHRONON_ERROR_BUFFER_TOO_SMALL);
    CHECK(failed_info.size == info.size);
    CHECK(std::all_of(too_small.begin(), too_small.end(),
                      [](std::uint8_t value) { return value == 0xA5; }));

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(info.size));
    CHECK(chronon_render_frame_into(
              fixture.engine, fixture.plan, 0, pixels.data(), pixels.size(), &info) ==
          CHRONON_OK);
    CHECK(std::any_of(pixels.begin(), pixels.end(),
                      [](std::uint8_t value) { return value != 0; }));

    std::fill(pixels.begin(), pixels.end(), 0);
    CHECK(chronon_render_frame_into(
              fixture.engine, fixture.plan, 1, pixels.data(), pixels.size(), &info) ==
          CHRONON_OK);
    CHECK(std::any_of(pixels.begin(), pixels.end(),
                      [](std::uint8_t value) { return value != 0; }));
}

struct CancellationState {
    std::atomic<bool> cancelled{true};
};

struct LogState {
    int calls{0};
    int last_level{-1};
    std::string component;
    std::string message;
};

void capture_log(int level, const char* component, const char* message, void* user) {
    auto& state = *static_cast<LogState*>(user);
    ++state.calls;
    state.last_level = level;
    state.component = component ? component : "";
    state.message = message ? message : "";
}

TEST_CASE("C ABI exposes a per-engine structured log callback") {
    EngineFixture fixture;
    LogState logs;
    REQUIRE(chronon_engine_set_log_callback(
                fixture.engine, capture_log, &logs) == CHRONON_OK);

    const auto output = unique_output_path("log_callback");
    const auto status = chronon_render_file(
        fixture.engine, fixture.plan, output.c_str(), 1, 0, 30, 1, nullptr);
    CHECK(status == CHRONON_ERROR_RENDER_FAILED);
    CHECK(logs.calls >= 1);
    CHECK(logs.last_level == 4); // sdk::LogLevel::Error
    CHECK_FALSE(logs.component.empty());
    CHECK_FALSE(logs.message.empty());

    CHECK(chronon_engine_set_log_callback(fixture.engine, nullptr, nullptr) ==
          CHRONON_OK);
}

#if defined(CHRONON3D_ENABLE_VIDEO) && CHRONON3D_ENABLE_VIDEO
TEST_CASE("C ABI v2 cancellation leaves no output or SDK temp sibling") {
    EngineFixture fixture;
    const auto output = unique_output_path("cancel");
    CancellationState cancellation;
    chronon_render_callbacks callbacks{};
    callbacks.is_cancelled = [](void* user) {
        return static_cast<CancellationState*>(user)->cancelled.load() ? 1 : 0;
    };
    callbacks.user = &cancellation;

    const auto status = chronon_render_file(
        fixture.engine, fixture.plan, output.c_str(), 0, 1, 30, 1, &callbacks);
    CHECK(status == CHRONON_ERROR_CANCELLED);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK_FALSE(has_sdk_temp_sibling(output));

    std::error_code error;
    std::filesystem::remove(output, error);
}
#else
TEST_CASE("C ABI v2 cancellation certification requires video support" * doctest::skip()) { // TICKET-CABI-V2-VIDEO-CERT
}
#endif

TEST_CASE("C ABI v2 invalid file range leaves no output or SDK temp sibling") {
    EngineFixture fixture;
    const auto output = unique_output_path("unsupported");
    chronon_render_callbacks callbacks{};
    callbacks.is_cancelled = [](void*) { return 0; };

    const auto status = chronon_render_file(
        fixture.engine, fixture.plan, output.c_str(), 1, 0, 30, 1, &callbacks);
    CHECK(status != CHRONON_OK);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK_FALSE(has_sdk_temp_sibling(output));
}

TEST_CASE("C ABI v2 two engines render in parallel") {
    EngineFixture first;
    EngineFixture second;
    chronon_frame_info first_info{};
    chronon_frame_info second_info{};
    REQUIRE(chronon_render_frame_into(
                first.engine, first.plan, 0, nullptr, 0, &first_info) ==
            CHRONON_ERROR_BUFFER_TOO_SMALL);
    REQUIRE(chronon_render_frame_into(
                second.engine, second.plan, 0, nullptr, 0, &second_info) ==
            CHRONON_ERROR_BUFFER_TOO_SMALL);
    std::vector<std::uint8_t> first_pixels(first_info.size);
    std::vector<std::uint8_t> second_pixels(second_info.size);
    std::atomic<bool> started{false};
    std::atomic<chronon_status> first_status{CHRONON_ERROR_UNKNOWN};
    std::atomic<chronon_status> second_status{CHRONON_ERROR_UNKNOWN};

    std::thread first_thread([&] {
        started.store(true, std::memory_order_release);
        first_status.store(chronon_render_frame_into(
            first.engine, first.plan, 0, first_pixels.data(), first_pixels.size(),
            &first_info));
    });
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    std::thread second_thread([&] {
        second_status.store(chronon_render_frame_into(
            second.engine, second.plan, 0, second_pixels.data(), second_pixels.size(),
            &second_info));
    });
    first_thread.join();
    second_thread.join();

    CHECK(first_status.load() == CHRONON_OK);
    CHECK(second_status.load() == CHRONON_OK);
    CHECK(std::any_of(first_pixels.begin(), first_pixels.end(),
                      [](std::uint8_t value) { return value != 0; }));
    CHECK(std::any_of(second_pixels.begin(), second_pixels.end(),
                      [](std::uint8_t value) { return value != 0; }));
}

std::string make_busy_plan() {
    std::string plan =
        "{\"schema\":\"chronon.render-plan\",\"version\":1,"
        "\"canvas\":{\"width\":1920,\"height\":1080,\"fps\":30,"
        "\"duration_frames\":2},\"layers\":[";
    for (int index = 0; index < 64; ++index) {
        if (index != 0) plan += ',';
        plan += "{\"id\":\"busy" + std::to_string(index) +
            "\",\"type\":\"color\",\"color\":[0.2,0.4,0.6,1.0]}";
    }
    plan += "],\"output\":{\"path\":\"out.png\"}}";
    return plan;
}

TEST_CASE("C ABI v2 same engine reports BUSY without deadlock") {
    EngineFixture fixture{make_busy_plan()};
    chronon_frame_info query{};
    REQUIRE(chronon_render_frame_into(
                fixture.engine, fixture.plan, 0, nullptr, 0, &query) ==
            CHRONON_ERROR_BUFFER_TOO_SMALL);

    constexpr int kThreads = 16;
    constexpr int kWaves = 4;
    std::atomic<int> busy_count{0};
    std::atomic<int> success_count{0};

    for (int wave = 0; wave < kWaves && busy_count.load() == 0; ++wave) {
        std::atomic<bool> start{false};
        std::vector<std::thread> threads;
        threads.reserve(kThreads);
        for (int index = 0; index < kThreads; ++index) {
            threads.emplace_back([&, index] {
                std::vector<std::uint8_t> pixels(query.size);
                chronon_frame_info info{};
                while (!start.load(std::memory_order_acquire))
                    std::this_thread::yield();
                const auto status = chronon_render_frame_into(
                    fixture.engine, fixture.plan, index % 2,
                    pixels.data(), pixels.size(), &info);
                if (status == CHRONON_ERROR_BUSY) {
                    busy_count.fetch_add(1, std::memory_order_relaxed);
                } else if (status == CHRONON_OK) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        start.store(true, std::memory_order_release);
        for (auto& thread : threads) thread.join();
    }

    CHECK(success_count.load() >= 1);
    CHECK(busy_count.load() >= 1);
}
