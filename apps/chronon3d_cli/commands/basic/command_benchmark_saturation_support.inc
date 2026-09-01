namespace {

struct ProcStatusMetrics {
    uint64_t voluntary_cs{0};
    uint64_t involuntary_cs{0};
    uint64_t peak_rss_kb{0};
};

ProcStatusMetrics read_proc_status() {
    ProcStatusMetrics m;
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto key = line.substr(0, colon);
        const auto val_start = line.find_first_of("0123456789", colon);
        if (val_start == std::string::npos) continue;
        if (key == "voluntary_ctxt_switches") {
            m.voluntary_cs = std::stoull(line.substr(val_start));
        } else if (key == "nonvoluntary_ctxt_switches") {
            m.involuntary_cs = std::stoull(line.substr(val_start));
        } else if (key == "VmPeak") {
            std::string digits;
            for (auto i = val_start; i < line.size(); ++i) {
                if (std::isdigit(static_cast<unsigned char>(line[i]))) digits += line[i];
            }
            if (!digits.empty()) m.peak_rss_kb = std::stoull(digits);
        }
    }
    return m;
}

double read_process_cpu_seconds() {
#if defined(__linux__)
    std::ifstream in("/proc/self/stat");
    std::string line;
    if (!std::getline(in, line)) return 0.0;
    const auto close_comm = line.rfind(')');
    if (close_comm == std::string::npos) return 0.0;
    std::istringstream fields(line.substr(close_comm + 2));
    std::string field;
    std::uint64_t utime = 0;
    std::uint64_t stime = 0;
    for (int index = 3; fields >> field; ++index) {
        if (index == 14) utime = std::strtoull(field.c_str(), nullptr, 10);
        if (index == 15) {
            stime = std::strtoull(field.c_str(), nullptr, 10);
            break;
        }
    }
    const long ticks = sysconf(_SC_CLK_TCK);
    return ticks > 0 ? static_cast<double>(utime + stime) / static_cast<double>(ticks) : 0.0;
#else
    return 0.0;
#endif
}

std::string read_cpu_model() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto key = line.substr(0, colon);
        const auto key_end = key.find_last_not_of(" \t");
        if (key_end == std::string::npos) continue;
        if (key.substr(0, key_end + 1) == "model name") {
            std::string val = line.substr(colon + 1);
            const auto first = val.find_first_not_of(" \t");
            return first == std::string::npos ? "unknown" : val.substr(first);
        }
    }
    return "unknown";
}

int count_numa_nodes() {
    std::ifstream in("/sys/devices/system/node/possible");
    std::string text;
    if (!(in >> text) || text.empty()) return 1;
    int nodes = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto comma = text.find(',', pos);
        const auto part = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (part.empty()) {
            if (comma == std::string::npos) break;
            pos = comma + 1;
            continue;
        }
        const auto dash = part.find('-');
        if (dash == std::string::npos) {
            ++nodes;
        } else {
            const int lo = std::atoi(part.substr(0, dash).c_str());
            const int hi = std::atoi(part.substr(dash + 1).c_str());
            if (hi >= lo) nodes += (hi - lo + 1);
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return nodes > 0 ? nodes : 1;
}

struct PerfMetrics {
    std::string cycles_per_frame{"N/A"};
    std::string instructions_per_frame{"N/A"};
    std::string ipc{"N/A"};
    std::string branch_miss_pct{"N/A"};
    std::string cache_miss_pct{"N/A"};
    std::string available{"false"};
};

PerfMetrics try_perf_stat(const std::string& cli_path, const std::string& scene,
                           int frames, int warmup) {
    PerfMetrics pm;
    if (std::system("perf stat -e cycles -r 1 /bin/true >/dev/null 2>&1") != 0) {
        return pm;
    }
    std::string perf_log = "/tmp/chronon_saturation_perf_XXXXXX.stat";
    std::string mkstemp = "mktemp " + perf_log;
    perf_log = "/tmp/chronon_sat_perf.stat";
    std::string cmd = fmt::format(
        "perf stat -r 3 -o {} -e cycles,instructions,branches,branch-misses,"
        "cache-references,cache-misses {} bench {} --frames {} --warmup {} --quiet "
        ">/dev/null 2>&1",
        perf_log, cli_path, scene, frames, warmup);

    int rc = std::system(cmd.c_str());
    if (rc != 0) return pm;

    auto parse_event = [&](const std::string& name) -> uint64_t {
        std::ifstream in(perf_log);
        std::string line;
        while (std::getline(in, line)) {
            auto pos = line.rfind(name);
            if (pos == std::string::npos) continue;
            if (pos > 0 && std::isalnum(line[pos - 1])) continue;
            if (pos + name.size() < line.size() && line[pos + name.size()] != ' '
                && line[pos + name.size()] != '#') continue;
            auto num_start = line.find_first_of("0123456789");
            if (num_start == std::string::npos) continue;
            auto num_end = line.find_first_not_of("0123456789,", num_start);
            std::string num_str = line.substr(num_start, num_end - num_start);
            num_str.erase(std::remove(num_str.begin(), num_str.end(), ','), num_str.end());
            return std::stoull(num_str);
        }
        return 0;
    };

    auto cycles = parse_event("cycles");
    auto instrs = parse_event("instructions");
    auto branches = parse_event("branches");
    auto branch_miss = parse_event("branch-misses");
    auto cache_ref = parse_event("cache-references");
    auto cache_miss = parse_event("cache-misses");

    if (cycles == 0) return pm;

    pm.available = "true";
    pm.cycles_per_frame = fmt::format("{}", cycles / frames);
    pm.instructions_per_frame = fmt::format("{}", instrs / frames);
    pm.ipc = fmt::format("{:.2f}", static_cast<double>(instrs) / static_cast<double>(cycles));
    if (branches > 0) {
        pm.branch_miss_pct = fmt::format("{:.2f}%", 100.0 * branch_miss / branches);
    }
    if (cache_ref > 0) {
        pm.cache_miss_pct = fmt::format("{:.2f}%", 100.0 * cache_miss / cache_ref);
    }

    std::remove(perf_log.c_str());
    return pm;
}

double percentile(const std::vector<double>& sorted, double pct) {
    if (sorted.empty()) return 0.0;
    const double idx = pct * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(idx));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(idx));
    if (lo == hi) return sorted[lo];
    const double frac = idx - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

} // namespace
