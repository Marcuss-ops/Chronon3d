#pragma once
#include <chronon3d/core/types/time.hpp>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <stdexcept>
#include <utility>
#include <string>
#include <vector>
namespace chronon3d::cli {
class TemporaryMaskSpool final {
public:
    TemporaryMaskSpool() {
        static std::atomic<std::uint64_t> sequence{0};
        const auto root = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = root / ("chronon-mask-spool-" + std::to_string(stamp) + "-" +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
            if (error && error != std::errc::file_exists)
                throw std::runtime_error("could not create temporary mask spool: " + error.message());
        }
        throw std::runtime_error("could not allocate a unique temporary mask spool directory");
    }
    ~TemporaryMaskSpool() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TemporaryMaskSpool(const TemporaryMaskSpool&) = delete;
    TemporaryMaskSpool& operator=(const TemporaryMaskSpool&) = delete;

    [[nodiscard]] std::string frame_path(std::size_t index, bool confidence) const {
        return (path_ / (std::string(confidence ? "confidence-" : "mask-") +
            std::to_string(index) + ".png")).string();
    }
    [[nodiscard]] std::string source_frame_path(std::size_t index) const {
        return (path_ / ("source-" + std::to_string(index) + ".chrmaskframe")).string();
    }
    [[nodiscard]] std::string staged_output_path(std::size_t index,
                                                bool confidence) const {
        return (path_ / (std::string(confidence ? "output-confidence-" :
                                                   "output-mask-") +
                         std::to_string(index) + ".png")).string();
    }

private:
    std::filesystem::path path_;
};

struct MaskPropagationState {
    std::vector<std::string> frames;
    std::string source_video;
    std::string source_video_transfer;
    std::string source_video_sha256;
    std::vector<RationalTime> source_presentation_times;
    std::string seed_mask;
    std::string output_pattern{"mask_####.png"};
    std::string input_transfer{"srgb"};
    std::size_t seed_frame{0};
    float minimum_confidence{0.12f};
    float minimum_mean_confidence{0.0f};
    float minimum_mean_foreground_confidence{0.0f};
    float maximum_foreground_loss{1.0f};
    std::uint64_t maximum_total_work_units{100'000'000'000ULL};
    std::uint64_t maximum_video_spool_bytes{8ULL * 1024ULL * 1024ULL * 1024ULL};
    std::uint32_t refinement_radius{0};
    float refinement_epsilon{1.0e-3f};
    std::string plan_input;
    std::string plan_output;
    std::string target_layer;
    std::string matte_video;
    bool encode_matte_video{false};
    std::string matte_layer_id;
    std::string matte_type{"luma"};
    std::vector<std::string> correction_masks;
};
}
