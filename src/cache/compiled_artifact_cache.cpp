// =============================================================================
// src/cache/compiled_artifact_cache.cpp
//
// Persistent storage for typed GPU-program artifacts.
// =============================================================================

#include <chronon3d/cache/compiled_artifact_cache.hpp>
#include <chronon3d/cache/compression/compressor.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>

#include <spdlog/spdlog.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <thread>
#include <system_error>
#include <string_view>

namespace chronon3d::cache {
namespace {

constexpr int kArtifactCompressionLevel = 6;
constexpr std::string_view kProgramArtifactExtension = ".pbin";
constexpr std::string_view kRetiredCompositionArtifactExtension = ".cart";

void write_binary_atomic(const std::filesystem::path& path,
                         const std::vector<std::uint8_t>& data) {
    const auto tmp = std::filesystem::path(
        path.string() + ".tmp." +
        std::to_string(std::hash<std::thread::id>{}(
            std::this_thread::get_id())));
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error(
                "Failed to open artifact for writing: " + tmp.string());
        }
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        if (!out) {
            out.close();
            std::filesystem::remove(tmp);
            throw std::runtime_error(
                "Failed to write artifact data: " + tmp.string());
        }
    }
    std::filesystem::rename(tmp, path);
}

std::vector<std::uint8_t> read_binary(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error(
            "Failed to open artifact for reading: " + path.string());
    }
    const auto size = in.tellg();
    if (size <= 0) return {};

    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    if (!in) {
        throw std::runtime_error(
            "Failed to read artifact data: " + path.string());
    }
    return data;
}

}  // namespace

std::string ProgramArtifactKey::stable_name() const {
    const auto digest = chronon3d::core::hash::HashBuilder{}
        .add("chronon3d.program-artifact-key.v1")
        .add(semantic_program_fingerprint)
        .add(lowering_kind)
        .add(target_backend)
        .add(gpu_architecture)
        .add(compiler_identity)
        .add(compiler_version)
        .add(compile_flags)
        .add(abi_version)
        .finish();

    char buffer[17]{};
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(digest));
    return "program-v1-" + std::string(buffer);
}

CompiledArtifactCache::CompiledArtifactCache(std::filesystem::path directory)
    : m_directory(std::move(directory)) {}

void CompiledArtifactCache::save_program_artifact(
    const ProgramArtifactKey& key,
    std::span<const std::uint8_t> payload) const {
    if (payload.empty()) {
        throw std::runtime_error(
            "CompiledArtifactCache: cannot save an empty program artifact");
    }

    std::filesystem::create_directories(m_directory);
    const auto compressed = cache_compressor().compress(
        payload.data(), payload.size(), kArtifactCompressionLevel);
    write_binary_atomic(program_artifact_path(key), compressed);
    spdlog::debug("[CompiledArtifactCache] Saved program artifact '{}' ({} bytes)",
                  key.stable_name(), compressed.size());
}

std::optional<std::vector<std::uint8_t>>
CompiledArtifactCache::load_program_artifact(
    const ProgramArtifactKey& key) const {
    const auto path = program_artifact_path(key);
    if (!std::filesystem::is_regular_file(path)) return std::nullopt;

    try {
        const auto compressed = read_binary(path);
        if (compressed.empty()) return std::nullopt;
        auto payload = cache_compressor().decompress(
            compressed.data(), compressed.size());
        if (payload.empty()) return std::nullopt;
        return payload;
    } catch (const std::exception& error) {
        spdlog::warn("[CompiledArtifactCache] Program artifact load failed for '{}': {}",
                     key.stable_name(), error.what());
        return std::nullopt;
    }
}

std::size_t CompiledArtifactCache::clear() const {
    std::size_t count = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(m_directory, error)) {
        if (error) break;
        const auto extension = entry.path().extension();
        if (entry.is_regular_file() &&
            (extension == kProgramArtifactExtension ||
             extension == kRetiredCompositionArtifactExtension)) {
            ++count;
            std::filesystem::remove(entry.path(), error);
            if (error) error.clear();
        }
    }
    return count;
}

std::filesystem::path CompiledArtifactCache::program_artifact_path(
    const ProgramArtifactKey& key) const {
    return m_directory / (key.stable_name() +
                          std::string(kProgramArtifactExtension));
}

}  // namespace chronon3d::cache
