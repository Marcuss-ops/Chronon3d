#pragma once

#include <chronon3d/core/tracing/tracing.hpp>

#include <cstdint>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace chronon3d::cli::telemetry {

struct NvmlStats {
    double gpu_utilization_avg{0.0};
    double gpu_utilization_peak{0.0};
    double nvdec_utilization_avg{0.0};
    double nvdec_utilization_peak{0.0};
    double nvenc_utilization_avg{0.0};
    double nvenc_utilization_peak{0.0};
    double memory_utilization_avg{0.0};
    std::uint64_t vram_used_peak_mb{0};
    std::uint64_t vram_total_mb{0};
    std::uint64_t sample_count{0};
};

class NvmlSampler {
public:
    NvmlSampler() {
        init_nvml();
    }

    ~NvmlSampler() {
        stop();
        shutdown_nvml();
    }

    bool is_available() const {
        return m_initialized;
    }

    void start(std::chrono::milliseconds interval = std::chrono::milliseconds(250)) {
        if (!m_initialized || m_running.exchange(true)) return;
        m_worker = std::thread([this, interval]() {
            while (m_running.load(std::memory_order_relaxed)) {
                sample_once();
                std::this_thread::sleep_for(interval);
            }
        });
    }

    void stop() {
        if (!m_running.exchange(false)) return;
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    NvmlStats stats() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        NvmlStats s;
        if (m_samples.empty()) return s;
        s.sample_count = m_samples.size();
        s.vram_total_mb = m_vram_total_mb;
        double sum_gpu = 0, sum_dec = 0, sum_enc = 0, sum_mem = 0;
        for (const auto& sample : m_samples) {
            sum_gpu += sample.gpu;
            sum_dec += sample.dec;
            sum_enc += sample.enc;
            sum_mem += sample.mem;
            if (sample.gpu > s.gpu_utilization_peak) s.gpu_utilization_peak = sample.gpu;
            if (sample.dec > s.nvdec_utilization_peak) s.nvdec_utilization_peak = sample.dec;
            if (sample.enc > s.nvenc_utilization_peak) s.nvenc_utilization_peak = sample.enc;
            if (sample.vram_used_mb > s.vram_used_peak_mb) s.vram_used_peak_mb = sample.vram_used_mb;
        }
        s.gpu_utilization_avg = sum_gpu / m_samples.size();
        s.nvdec_utilization_avg = sum_dec / m_samples.size();
        s.nvenc_utilization_avg = sum_enc / m_samples.size();
        s.memory_utilization_avg = sum_mem / m_samples.size();
        return s;
    }

private:
    struct Sample {
        double gpu{0};
        double dec{0};
        double enc{0};
        double mem{0};
        std::uint64_t vram_used_mb{0};
    };

    struct nvmlUtilization_t {
        unsigned int gpu;
        unsigned int memory;
    };

    struct nvmlMemory_t {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    };

    using nvmlReturn_t = int;
    using nvmlDevice_t = void*;

    void* m_lib{nullptr};
    nvmlDevice_t m_device{nullptr};
    bool m_initialized{false};
    std::atomic<bool> m_running{false};
    std::thread m_worker;
    mutable std::mutex m_mutex;
    std::vector<Sample> m_samples;
    std::uint64_t m_vram_total_mb{0};

    nvmlReturn_t (*p_nvmlInit)(){nullptr};
    nvmlReturn_t (*p_nvmlShutdown)(){nullptr};
    nvmlReturn_t (*p_nvmlDeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*){nullptr};
    nvmlReturn_t (*p_nvmlDeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_t*){nullptr};
    nvmlReturn_t (*p_nvmlDeviceGetDecoderUtilization)(nvmlDevice_t, unsigned int*, unsigned int*){nullptr};
    nvmlReturn_t (*p_nvmlDeviceGetEncoderUtilization)(nvmlDevice_t, unsigned int*, unsigned int*){nullptr};
    nvmlReturn_t (*p_nvmlDeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_t*){nullptr};

    void init_nvml() {
        m_lib = dlopen("libnvidia-ml.so.1", RTLD_NOW);
        if (!m_lib) m_lib = dlopen("libnvidia-ml.so", RTLD_NOW);
        if (!m_lib) return;

        p_nvmlInit = reinterpret_cast<nvmlReturn_t(*)()>(dlsym(m_lib, "nvmlInit_v2"));
        if (!p_nvmlInit) p_nvmlInit = reinterpret_cast<nvmlReturn_t(*)()>(dlsym(m_lib, "nvmlInit"));
        p_nvmlShutdown = reinterpret_cast<nvmlReturn_t(*)()>(dlsym(m_lib, "nvmlShutdown"));
        p_nvmlDeviceGetHandleByIndex = reinterpret_cast<nvmlReturn_t(*)(unsigned int, nvmlDevice_t*)>(
            dlsym(m_lib, "nvmlDeviceGetHandleByIndex_v2"));
        if (!p_nvmlDeviceGetHandleByIndex) {
            p_nvmlDeviceGetHandleByIndex = reinterpret_cast<nvmlReturn_t(*)(unsigned int, nvmlDevice_t*)>(
                dlsym(m_lib, "nvmlDeviceGetHandleByIndex"));
        }
        p_nvmlDeviceGetUtilizationRates = reinterpret_cast<nvmlReturn_t(*)(nvmlDevice_t, nvmlUtilization_t*)>(
            dlsym(m_lib, "nvmlDeviceGetUtilizationRates"));
        p_nvmlDeviceGetDecoderUtilization = reinterpret_cast<nvmlReturn_t(*)(nvmlDevice_t, unsigned int*, unsigned int*)>(
            dlsym(m_lib, "nvmlDeviceGetDecoderUtilization"));
        p_nvmlDeviceGetEncoderUtilization = reinterpret_cast<nvmlReturn_t(*)(nvmlDevice_t, unsigned int*, unsigned int*)>(
            dlsym(m_lib, "nvmlDeviceGetEncoderUtilization"));
        p_nvmlDeviceGetMemoryInfo = reinterpret_cast<nvmlReturn_t(*)(nvmlDevice_t, nvmlMemory_t*)>(
            dlsym(m_lib, "nvmlDeviceGetMemoryInfo"));

        if (!p_nvmlInit || !p_nvmlShutdown || !p_nvmlDeviceGetHandleByIndex ||
            !p_nvmlDeviceGetUtilizationRates || !p_nvmlDeviceGetMemoryInfo) {
            dlclose(m_lib);
            m_lib = nullptr;
            return;
        }

        if (p_nvmlInit() != 0) {
            dlclose(m_lib);
            m_lib = nullptr;
            return;
        }

        if (p_nvmlDeviceGetHandleByIndex(0, &m_device) != 0) {
            p_nvmlShutdown();
            dlclose(m_lib);
            m_lib = nullptr;
            return;
        }

        nvmlMemory_t mem{};
        if (p_nvmlDeviceGetMemoryInfo(m_device, &mem) == 0) {
            m_vram_total_mb = mem.total / (1024 * 1024);
        }
        m_initialized = true;
    }

    void shutdown_nvml() {
        if (m_initialized && p_nvmlShutdown) {
            p_nvmlShutdown();
        }
        if (m_lib) {
            dlclose(m_lib);
            m_lib = nullptr;
        }
        m_initialized = false;
    }

    void sample_once() {
        if (!m_initialized || !m_device) return;
        Sample sample;
        nvmlUtilization_t util{};
        if (p_nvmlDeviceGetUtilizationRates(m_device, &util) == 0) {
            sample.gpu = util.gpu;
            sample.mem = util.memory;
        }
        if (p_nvmlDeviceGetDecoderUtilization) {
            unsigned int dec_val = 0, sampling_us = 0;
            if (p_nvmlDeviceGetDecoderUtilization(m_device, &dec_val, &sampling_us) == 0) {
                sample.dec = dec_val;
            }
        }
        if (p_nvmlDeviceGetEncoderUtilization) {
            unsigned int enc_val = 0, sampling_us = 0;
            if (p_nvmlDeviceGetEncoderUtilization(m_device, &enc_val, &sampling_us) == 0) {
                sample.enc = enc_val;
            }
        }
        nvmlMemory_t mem{};
        if (p_nvmlDeviceGetMemoryInfo(m_device, &mem) == 0) {
            sample.vram_used_mb = mem.used / (1024 * 1024);
            if (m_vram_total_mb == 0) m_vram_total_mb = mem.total / (1024 * 1024);
        }
        // Perfetto counter tracks (chronon.gpu): raw NVML samples emitted on
        // the sampler thread at the configured cadence.  No-op when tracing
        // is compiled out or no trace session is running.
        CHRONON_TRACE_COUNTER("chronon.gpu", "vram_used_mb",
            static_cast<int64_t>(sample.vram_used_mb));
        CHRONON_TRACE_COUNTER("chronon.gpu", "gpu_utilization", sample.gpu);
        CHRONON_TRACE_COUNTER("chronon.gpu", "nvdec_utilization", sample.dec);
        CHRONON_TRACE_COUNTER("chronon.gpu", "nvenc_utilization", sample.enc);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_samples.push_back(sample);
    }
};

} // namespace chronon3d::cli::telemetry
