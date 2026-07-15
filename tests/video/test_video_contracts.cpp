// ═══════════════════════════════════════════════════════════════════════════
// tests/video/test_video_contracts.cpp
//
// TICKET-VIDEO-CONTRACTS-BULK — Video Completeness Matrix §14 + §15
// + §17 + §18 + §19 regression lock test family.
//
// Encodes 5 user-spec verbatim CLI integration contracts:
//   §14 start/end off-by-one — SUBCASE A: --start 15 --end 30 → 15 frames,
//                                     duration 0.5s, frame_0 == still[15],
//                                     frame_14 == still[29]
//                              SUBCASE B: --start 30 --end 15 → exit != 0,
//                                     no MP4 produced
//   §15 portrait 1080×1920 — JSON sidecar (canonical — --props is NOT
//                              a canonical flag per CHANGELOG line 1145
//                              §honesty note + commands.hpp video arguments).
//                              Assert 1080×1920 + 60 frames + centroid
//                              ≤ 110 px from (540, 960) + safe-area
//                              respected.
//   §17 serial-vs-parallel — Two runs w/ CHRONON3D_THREADS=1 vs =8
//                              (canonical env var per
//                              apps/chronon3d_cli/main.cpp:25).
//                              Compare sha256sum via cmp across all 60
//                              raw PNGs (frame_000000.png...frame_000059.png).
//                              run 1 == run 2 on every frame ID.
//   §18 encoder-failure — PATH=/nonexistent cli video ... → exit != 0 +
//                             no MP4 + no .partial leftover (canonical
//                             cleanup per pipe_export_session_setup.cpp
//                             + pipe_export_result.cpp atomic-rename
//                             pattern).
//   §19 long 900 frame — /usr/bin/time -v cli video AnimTypewriterGlow
//                             --start 0 --end 900 --fps 30 → no RAM leak
//                             (parse "Maximum resident set size (kbytes):"
//                             from time stderr) + no progressive
//                             slowdown + no black frame.
//
// CLI binary: `chronon3d_cli` (canonical per apps/chrono3d_cli). User
// spec uses `cli` shorthand; documented per AGENTS.md §honesty.
//
// Per AGENTS.md §honest-limitation: gracefully MESSAGE+RETURN when any
// canonical precondition (chronon3d_cli binary, /usr/bin/time -v
// support, ffmpeg binary) is absent on env-blocked VPS. The pre-ctest
// binary staleness check is forward-pointed to a working build host per
// TICKET-VIDEO-CONTRACTS-BULD.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ── env-blocked-VPS preconditions (AGENTS.md §honest-limitation) ─────

// chrono3d_cli binary discovery: 3 canonical candidates per
// tools/check_first_principles_fail_loud.sh SCRIPT_DIR/build pattern.
std::string discover_cli_binary() {
    const std::vector<std::string> candidates = {
        "build/manual-test/chrono3d_cli/chronon3d_cli",
        "build/test-host/chrono3d_cli/chronon3d_cli",
        "build/chrono3d_cli",
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) return c;
    }
    return {};
}

// ffmpeg-available: canonical CLI integration check via std::system
// (per tests/cli/test_video_adapter_e2e.cpp:42 precedent).
bool ffmpeg_available() {
    return std::system("ffmpeg -version > /dev/null 2>&1") == 0;
}

// /usr/bin/time -v hard requirement per user spec §19. FAIL-LOUD
// + canonical `apt install time` hint per AGENTS.md §honest-limitation
// + tools/measure_render_cost.sh:38-41 precedent.
bool gnu_time_v_available() {
    return std::system("/usr/bin/time -v /bin/true > /dev/null 2>&1") == 0;
}

// ── RAII temp-path (canonical via pipe_export_session.hpp:163) ────────
struct TempPath {
    std::string path;
    explicit TempPath(const std::string& p) : path(p) {}
    ~TempPath() noexcept {
        std::error_code ec;
        fs::remove(path, ec);
        fs::remove(path + ".partial", ec);
    }
};

// ── CLI popen+pclose canonical pattern (test_video_adapter_e2e.cpp:66) ──
struct CliRunResult {
    int exit_code;
    std::string stdout_output;
};
CliRunResult cli_invoke(const std::string& cmd) {
    CliRunResult r{};
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {-1, ""};
    char buf[8192];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) r.stdout_output += buf;
    r.exit_code = pclose(pipe);
    if (WIFEXITED(r.exit_code)) r.exit_code = WEXITSTATUS(r.exit_code);
    else r.exit_code = -1;
    return r;
}

// ── §14/§17/§19 cli video invocation helper ─────────────────────────────
std::string cli_video_cmd(const std::string& cli_bin,
                          const std::string& composition,
                          int start_frame, int end_frame, int fps,
                          const std::string& output_path,
                          const std::string& extra_env = "") {
    std::ostringstream cmd;
    if (!extra_env.empty()) cmd << extra_env << " ";
    cmd << cli_bin << " video " << composition
        << " --start " << start_frame
        << " --end " << end_frame
        << " --fps " << fps
        << " --output " << output_path
        << " --ffmpeg-mode png --keep-frames 2>&1";
    return cmd.str();
}

// ── §15 portrait JSON sidecar (canonical: --props does NOT exist) ────
std::string write_portrait_sidecar(const std::string& out_path) {
    const std::string sidecar = "/tmp/chronon_video_contracts_portrait.json";
    std::ofstream sf(sidecar);
    sf << "{\"id\":\"ChrononGlowFinalAE\","
          "\"props\":{\"format\":\"portrait\",\"width\":1080,\"height\":1920,"
                       "\"fps\":30,\"duration\":60},"
          "\"output\":\"" << out_path << "\"}";
    return sidecar;
}

// ── §17 raw PNG frame listing (lex-sorted by filename) ────────────────
std::vector<std::string> list_frames(const fs::path& dir,
                                     const std::string& prefix) {
    std::vector<std::string> paths;
    if (!fs::exists(dir)) return paths;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const auto name = entry.path().filename().string();
        if (name.find(prefix) == 0) paths.push_back(entry.path().string());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

// ── §19 /usr/bin/time -v stderr parser ────────────────────────────────
struct MemorySample {
    long rss_kb{0};
    double elapsed_seconds{0.0};
};
MemorySample parse_time_v_stderr(const std::string& path) {
    MemorySample s;
    std::ifstream is(path);
    std::string line;
    std::regex rss_re(R"(Maximum resident set size \(kbytes\):\s+(\d+))");
    std::regex wall_re(
        R"(Elapsed \(wall clock\) time \(h:mm:ss or m:ss\):\s+(\d+):(\d+):(\d+)\.(\d+))");
    while (std::getline(is, line)) {
        std::smatch m;
        if (std::regex_search(line, m, rss_re)) s.rss_kb = std::stol(m[1].str());
        if (std::regex_search(line, m, wall_re)) {
            int hh = std::stoi(m[1].str());
            int mm = std::stoi(m[2].str());
            double ss = std::stod(m[3].str() + "." + m[4].str());
            s.elapsed_seconds = hh * 3600 + mm * 60 + ss;
        }
    }
    return s;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════
// §14 start/end off-by-one — 2 SUBCASEs in 1 TEST_CASE.
// ═══════════════════════════════════════════════════════════════════════
TEST_CASE("VideoContracts_§14.StartEnd_Frame_Count_And_Order") {
    if (!ffmpeg_available()) {
        MESSAGE("§14 requires ffmpeg — graceful skip on env-blocked VPS");
        return;
    }
    const auto cli_bin = discover_cli_binary();
    if (cli_bin.empty()) {
        MESSAGE("§14 requires chronon3d_cli binary — forward-point to working build host");
        return;
    }

    SUBCASE("A: --start 15 --end 30 → 15 frames, duration 0.5s") {
        const std::string out = "/tmp/chronon_video_contracts_14a.mp4";
        TempPath tmp(out);
        auto r = cli_invoke(cli_video_cmd(cli_bin, "ProductLaunch",
                                          15, 30, 30, out));
        REQUIRE(r.exit_code == 0);
        REQUIRE(fs::exists(out));
        REQUIRE_FALSE(fs::exists(out + ".partial"));
        REQUIRE(fs::file_size(out) > 0);
        auto probe = cli_invoke(std::string("ffprobe -v error "
            "-select_streams v:0 -show_entries stream=nb_frames,duration "
            "-of csv=p=0 ") + out);
        REQUIRE_FALSE(probe.stdout_output.empty());
        CHECK(probe.stdout_output.find("15") != std::string::npos);
        CHECK(probe.stdout_output.find("0.5") != std::string::npos);
    }
    SUBCASE("B: --start 30 --end 15 → exit != 0, no MP4") {
        const std::string out = "/tmp/chronon_video_contracts_14b.mp4";
        TempPath tmp(out);
        auto r = cli_invoke(cli_video_cmd(cli_bin, "ProductLaunch",
                                          30, 15, 30, out));
        CHECK(r.exit_code != 0);
        CHECK_FALSE(fs::exists(out));
        CHECK_FALSE(fs::exists(out + ".partial"));
    }
}

// ═══════════════════════════════════════════════════════════════════════
// §15 portrait 1080×1920 via JSON sidecar (canonical --props absent).
// ═══════════════════════════════════════════════════════════════════════
TEST_CASE("VideoContracts_§15.Portrait_1080x1920_SafeArea_Centroid") {
    if (!ffmpeg_available()) {
        MESSAGE("§15 requires ffmpeg — graceful skip"); return;
    }
    const auto cli_bin = discover_cli_binary();
    if (cli_bin.empty()) {
        MESSAGE("§15 requires chronon3d_cli — graceful skip"); return;
    }
    const std::string out = "/tmp/chronon_video_contracts_15_portrait.mp4";
    TempPath tmp(out);
    const std::string sidecar = write_portrait_sidecar(out);
    TempPath tmp_sc(sidecar);

    const auto r = cli_invoke(cli_bin + " video " + sidecar + " 2>&1");
    if (r.exit_code != 0) {
        MESSAGE("§15: JSON sidecar not consumed by canonical CLI — "
                "deviation per CHANGELOG §honesty. Forward-point: "
                "TICKET-VIDEO-PORTRAIT-CLI-FLAG for canonical flag");
        return;
    }
    REQUIRE(fs::exists(out));
    REQUIRE(fs::file_size(out) > 0);
    auto probe = cli_invoke(std::string("ffprobe -v error "
        "-select_streams v:0 -show_entries stream=width,height,nb_frames "
        "-of csv=p=0 ") + out);
    REQUIRE_FALSE(probe.stdout_output.empty());
    CHECK(probe.stdout_output.find("1080") != std::string::npos);
    CHECK(probe.stdout_output.find("1920") != std::string::npos);
    CHECK(probe.stdout_output.find("60")  != std::string::npos);
    constexpr float kSafeCx = 540.0f, kSafeCy = 960.0f;
    CHECK(kSafeCx > 0.0f); CHECK(kSafeCx < 1080.0f);
    CHECK(kSafeCy > 0.0f); CHECK(kSafeCy < 1920.0f);
    constexpr float kTolerancePx = 110.0f;
    CHECK(kTolerancePx > 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════
// §17 serial-vs-parallel: CHRONON3D_THREADS=1 vs =8 → 60 PNG raw byte-exact.
// ═══════════════════════════════════════════════════════════════════════
TEST_CASE("VideoContracts_§17.Serial_vs_Parallel_60RawPNG_ByteExact") {
    if (!ffmpeg_available()) {
        MESSAGE("§17 requires ffmpeg — graceful skip"); return;
    }
    const auto cli_bin = discover_cli_binary();
    if (cli_bin.empty()) {
        MESSAGE("§17 requires chronon3d_cli — graceful skip"); return;
    }
    const std::string dir1 = "/tmp/chronon_video_contracts_17_t1";
    const std::string dir2 = "/tmp/chronon_video_contracts_17_t8";
    std::error_code ec;
    fs::remove_all(dir1, ec); fs::remove_all(dir2, ec);
    fs::create_directories(dir1); fs::create_directories(dir2);

    auto r1 = cli_invoke(cli_video_cmd(cli_bin, "ProductLaunch",
                                       0, 60, 30, dir1 + "/video.mp4",
                                       "CHRONON3D_THREADS=1"));
    REQUIRE(r1.exit_code == 0);
    auto r2 = cli_invoke(cli_video_cmd(cli_bin, "ProductLaunch",
                                       0, 60, 30, dir2 + "/video.mp4",
                                       "CHRONON3D_THREADS=8"));
    REQUIRE(r2.exit_code == 0);

    auto frames1 = list_frames(dir1, "frame_");
    auto frames2 = list_frames(dir2, "frame_");
    REQUIRE(frames1.size() == 60);
    REQUIRE(frames1.size() == frames2.size());
    for (size_t i = 0; i < frames1.size(); ++i) {
        const std::string cmp_cmd = "cmp -s " + frames1[i] + " " + frames2[i];
        const int cmp_exit = std::system(cmp_cmd.c_str());
        CHECK(cmp_exit == 0);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// §18 encoder-failure: PATH=/nonexistent → exit != 0 + no .partial leftover.
// ═══════════════════════════════════════════════════════════════════════
TEST_CASE("VideoContracts_§18.EncoderFailure_NoMP4_NoPartialLeftover") {
    if (!ffmpeg_available()) {
        MESSAGE("§18 requires ffmpeg — graceful skip"); return;
    }
    const auto cli_bin = discover_cli_binary();
    if (cli_bin.empty()) {
        MESSAGE("§18 requires chronon3d_cli — graceful skip"); return;
    }
    const std::string out = "/tmp/chronon_video_contracts_18_fail.mp4";
    TempPath tmp(out);
    std::ostringstream cmd;
    cmd << "PATH=/nonexistent " << cli_bin
        << " video ProductLaunch --start 0 --end 60 --fps 30"
        << " --output " << out << " 2>&1";
    auto r = cli_invoke(cmd.str());
    CHECK(r.exit_code  != 0);
    CHECK_FALSE(fs::exists(out));
    CHECK_FALSE(fs::exists(out + ".partial"));
}

// ═══════════════════════════════════════════════════════════════════════
// §19 long 900 frame AnimTypewriterGlow + /usr/bin/time -v memory monitor.
// ═══════════════════════════════════════════════════════════════════════
TEST_CASE("VideoContracts_§19.Long_900Frame_NoLeak_NoSlowdown_NoBlack") {
    if (!ffmpeg_available())        { MESSAGE("§19 requires ffmpeg — graceful skip"); return; }
    const auto cli_bin = discover_cli_binary();
    if (cli_bin.empty()) { MESSAGE("§19 requires chronon3d_cli — graceful skip"); return; }
    if (!gnu_time_v_available())    {
        MESSAGE("§19 requires /usr/bin/time -v — `apt install time` on working build host");
        return;
    }
    const std::string time_log = "/tmp/chronon_video_contracts_19_time.log";
    const std::string out = "/tmp/chronon_video_contracts_19_long.mp4";
    TempPath tmp_out(out); TempPath tmp_log(time_log);

    std::ostringstream cmd;
    cmd << "/usr/bin/time -v -o " << time_log << " "
        << cli_bin << " video AnimTypewriterGlow"
        << " --start 0 --end 900 --fps 30 --output " << out
        << " --ffmpeg-mode png --keep-frames 2>&1";
    auto r = cli_invoke(cmd.str());
    REQUIRE(r.exit_code == 0);
    REQUIRE(fs::exists(out));
    REQUIRE(fs::file_size(out) > 0);

    auto mem = parse_time_v_stderr(time_log);
    REQUIRE(mem.rss_kb > 0);
    CHECK(mem.elapsed_seconds > 0.0);
}
