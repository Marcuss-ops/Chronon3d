#include <doctest/doctest.h>

#include <chronon3d/cache/compiled_artifact_cache.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

chronon3d::cache::ProgramArtifactKey test_key() {
    return {
        0x123456789abcdef0ULL,
        "nvrtc-ptx",
        "cuda-driver",
        "compute_86",
        "NVRTC",
        "12.8",
        "--gpu-architecture=compute_86",
        1};
}

} // namespace

TEST_CASE("ProgramArtifactKey is canonical across equivalent values") {
    const auto first = test_key();
    auto second = test_key();
    CHECK(first.stable_name() == second.stable_name());

    second.compiler_version = "12.9";
    CHECK(first.stable_name() != second.stable_name());
}

TEST_CASE("CompiledArtifactCache round-trips a GPU program artifact") {
    const auto directory = std::filesystem::temp_directory_path() /
        "chronon3d-program-artifact-cache-test";
    chronon3d::cache::CompiledArtifactCache cache(directory);
    const std::vector<std::uint8_t> payload{
        0x50, 0x54, 0x58, 0x00, 0x86, 0x01, 0xff};

    cache.save_program_artifact(test_key(), payload);
    const auto loaded = cache.load_program_artifact(test_key());
    REQUIRE(loaded.has_value());
    CHECK(*loaded == payload);
    CHECK(cache.clear() == 1);
    CHECK_FALSE(cache.load_program_artifact(test_key()).has_value());

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}
