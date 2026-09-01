namespace {

constexpr const char* kRenderPlanSchema = "chronon.render-plan";
constexpr const char* kFontRegular = "assets/fonts/Inter-Regular.ttf";
constexpr const char* kFontBold = "assets/fonts/Inter-Bold.ttf";

const char* doctor_status_name_impl(DoctorStatus status) noexcept {
    switch (status) {
        case DoctorStatus::Pass: return "pass";
        case DoctorStatus::Warn: return "warn";
        case DoctorStatus::Fail: return "fail";
        case DoctorStatus::Skip: return "skip";
    }
    return "skip";
}

const char* doctor_status_label_impl(DoctorStatus status) noexcept {
    switch (status) {
        case DoctorStatus::Pass: return "PASS";
        case DoctorStatus::Warn: return "WARN";
        case DoctorStatus::Fail: return "FAIL";
        case DoctorStatus::Skip: return "SKIP";
    }
    return "SKIP";
}

void add_check(DoctorReport& report, std::string id, DoctorStatus status,
               std::string message) {
    report.checks.push_back(DoctorCheck{std::move(id), status, std::move(message)});
}

class LogSilencer {
public:
    LogSilencer() : previous_(spdlog::get_level()) {
        spdlog::set_level(spdlog::level::off);
    }
    ~LogSilencer() { spdlog::set_level(previous_); }
    LogSilencer(const LogSilencer&) = delete;
    LogSilencer& operator=(const LogSilencer&) = delete;

private:
    spdlog::level::level_enum previous_;
};

bool command_succeeds(const char* command) {
    return std::system(command) == 0;
}

bool encoder_available(const char* name) {
    const std::string command =
        "ffmpeg -hide_banner -h encoder=" + std::string(name) +
        " > /dev/null 2>&1";
    return command_succeeds(command.c_str());
}

std::string host_arch() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#else
    return "unknown";
#endif
}

std::string read_cpu_model_name() {
    std::ifstream in("/proc/cpuinfo");
    if (!in.is_open()) return "unknown";
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, colon);
        const std::size_t key_end = key.find_last_not_of(" \t");
        if (key_end == std::string::npos) continue;
        if (key.substr(0, key_end + 1) != "model name" &&
            key.substr(0, key_end + 1) != "Model name") {
            continue;
        }
        std::string value = line.substr(colon + 1);
        const std::size_t first = value.find_first_not_of(" \t");
        return first == std::string::npos ? "unknown" : value.substr(first);
    }
    return "unknown";
}

std::uint64_t host_ram_bytes() noexcept {
#if defined(__linux__)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
    }
#endif
    return 0;
}

std::string human_bytes(std::uint64_t bytes) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    std::ostringstream out;
    out.precision(1);
    out << std::fixed << static_cast<double>(bytes) / kGiB << " GiB";
    return out.str();
}

bool directory_writable(const std::filesystem::path& dir) {
    std::error_code ec;
    const auto probe = dir / ".chronon-doctor-write-probe";
    {
        std::ofstream out(probe, std::ios::binary);
        if (!out) return false;
        out << "probe";
    }
    std::filesystem::remove(probe, ec);
    return true;
}
