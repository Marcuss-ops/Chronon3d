// =============================================================================
// src/cache/compiled_artifact_cache.cpp
//
// Save/load compiled compositions to/from disk via:
//   CompiledComposition → FlatBuffers → zstd → atomic file write
//   File read → zstd decompress → FlatBuffers verifier → version check → hydrate
//
// The FlatBuffers schema lives in schema/chronon_artifact.fbs.
// Generated header: chronon_artifact_generated.h
// =============================================================================

#include <chronon3d/cache/compiled_artifact_cache.hpp>
#include <chronon3d/cache/compression/compressor.hpp>

#include "chronon_artifact_generated.h"

#include <flatbuffers/flatbuffers.h>
#include <spdlog/spdlog.h>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_definition.hpp>

#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace chronon3d::cache {
namespace {

// ── Constants ────────────────────────────────────────────────────────────

/// Current artifact format version.  Increment on any breaking change to
/// the FlatBuffers schema or the serialization logic.
constexpr std::uint32_t kArtifactFormatVersion = 1;

/// zstd compression level for artifacts (offline/batch-friendly value).
constexpr int kArtifactCompressionLevel = 6;

/// File extension on disk.
constexpr std::string_view kExtension = ".cart";

// ── Internal helpers ─────────────────────────────────────────────────────

void write_binary_atomic(const std::filesystem::path& path,
                         const std::vector<std::uint8_t>& data) {
    const auto tmp = std::filesystem::path(path.string() + ".tmp");
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
    if (size <= 0) {
        return {};
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(size));
    if (!in) {
        throw std::runtime_error(
            "Failed to read artifact data: " + path.string());
    }
    return data;
}

std::string digest_hex(const std::vector<std::uint8_t>& data) {
    // Simple hex digest (not cryptographic — fingerprint only).
    // We use a fast FNV-1a-like hash for the content identity.
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (auto b : data) {
        h ^= b;
        h *= 0x100000001b3ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string(buf);
}

// ── Serialization: CompiledComposition → FlatBuffer bytes ────────────────

std::vector<std::uint8_t> serialize_to_flatbuffer(
    const CompiledComposition& compiled) {
    using namespace chronon3d::artifact;
    flatbuffers::FlatBufferBuilder builder(4096);

    // ── Composition identity ──────────────────────────────────────────

    auto comp_def = compiled.definition;
    std::int32_t width  = comp_def ? comp_def->composition.width : 0;
    std::int32_t height = comp_def ? comp_def->composition.height : 0;
    std::int32_t fps_num_int = comp_def ? comp_def->composition.frame_rate.num() : 30;
    std::int32_t fps_den_int = comp_def ? comp_def->composition.frame_rate.den() : 1;
    std::uint32_t fps_num = static_cast<std::uint32_t>(fps_num_int);
    std::uint32_t fps_den = static_cast<std::uint32_t>(fps_den_int);
    std::int64_t duration = comp_def ? comp_def->composition.duration.integral()
                                     : 0;
    std::uint8_t exec_mode = static_cast<std::uint8_t>(
        compiled.execution_mode);

    auto composition_id = CreateCompositionId(
        builder,
        compiled.fingerprint,
        width, height,
        fps_num, fps_den,
        duration,
        exec_mode);

    // ── Graph identity (use fingerprint as instance_id if no graph) ────

    auto graph_id = CreateGraphIdentity(
        builder,
        compiled.fingerprint,   // instance_id
        0,                       // structure_hash (not populated in v0.1)
        0, 0,                    // registry gen / processor snap
        0);                      // authored structure fingerprint

    // ── Nodes (empty for v0.1 — compiled compositions don't expose
    //     per-node metadata yet; this is a forward-compat slot) ─────────

    std::vector<flatbuffers::Offset<CompiledNodeMeta>> nodes;

    // ── Program metadata — frame_program ──────────────────────────────

    std::vector<flatbuffers::Offset<CompiledOpMeta>> ops;
    std::vector<flatbuffers::Offset<LayerBatchMeta>> batches;
    std::vector<flatbuffers::Offset<DynamicSlotMeta>> slots;
    std::vector<flatbuffers::Offset<NodeIndexList>> levels;

    auto fp = compiled.frame_program;
    std::uint32_t slot_count = fp ? static_cast<std::uint32_t>(fp->slots.size()) : 0;

    for (const auto& slot_desc : (fp ? fp->slots : std::vector<DynamicSlotDesc>{})) {
        auto slot_meta = CreateDynamicSlotMeta(
            builder,
            slot_desc.slot_id,
            static_cast<std::uint8_t>(slot_desc.kind),
            slot_desc.offset,
            slot_desc.size,
            slot_desc.owner_instance);
        slots.push_back(slot_meta);
    }
    auto slots_vec = builder.CreateVector(slots);

    auto levels_vec = builder.CreateVector(levels);
    auto ops_vec    = builder.CreateVector(ops);
    auto batches_vec = builder.CreateVector(batches);

    auto program = CreateFrameProgramMeta(
        builder,
        levels_vec, ops_vec, batches_vec, slots_vec,
        fp ? fp->command_plan.pass_count() > 0 : false,  // fully_recorded
        false,  // has_fused_passes
        false,  // require_native_gpu
        builder.CreateVector(std::vector<bool>{}),  // interior_skip
        0);     // static_bake_count

    // ── Lifetimes, ownership, physical plan, release levels ───────────

    auto lifetimes_vec = builder.CreateVector(
        std::vector<flatbuffers::Offset<ResourceLifetimeMeta>>{});
    auto ownership_vec = builder.CreateVector(
        std::vector<flatbuffers::Offset<OwnershipTransferMeta>>{});
    auto physical_plan = builder.CreateVector(std::vector<std::uint8_t>{});
    auto release_vec = builder.CreateVector(
        std::vector<flatbuffers::Offset<ReleaseLevel>>{});
    auto consumer_counts = builder.CreateVector(std::vector<std::uint32_t>{});

    // ── Parameter payload ─────────────────────────────────────────────
    auto param_data = builder.CreateVector(std::vector<std::uint8_t>{});
    auto param_payload = CreateFrameDataPayload(builder, param_data, 0);

    // ── Nodes vector ──────────────────────────────────────────────────
    auto nodes_vec = builder.CreateVector(nodes);

    // ── Root descriptor ───────────────────────────────────────────────
    auto descriptor = CreateCompiledArtifactDescriptor(
        builder,
        0,              // header (populated later, see below)
        composition_id,
        graph_id,
        nodes_vec,
        program,
        lifetimes_vec,
        ownership_vec,
        physical_plan,
        release_vec,
        consumer_counts,
        param_payload,
        false);  // skip_initial_clear

    // ── Compute content SHA-256 (hash of the descriptor body) ─────────
    builder.Finish(descriptor);
    auto body_data = std::vector<std::uint8_t>(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());

    // Rebuild with header containing the content digest.
    flatbuffers::FlatBufferBuilder final_builder(builder.GetSize() + 256);
    auto digest = digest_hex(body_data);
    auto git_sha = final_builder.CreateString("unknown");

    auto header = CreateArtifactHeader(
        final_builder,
        kArtifactFormatVersion,
        final_builder.CreateVector(
            std::vector<std::uint8_t>(digest.begin(), digest.end())),
        git_sha,
        0);  // created_at_ms

    // Re-create the full descriptor with header.
    auto composition_id2 = CreateCompositionId(
        final_builder,
        compiled.fingerprint,
        width, height,
        fps_num, fps_den,
        duration,
        exec_mode);

    auto graph_id2 = CreateGraphIdentity(
        final_builder,
        compiled.fingerprint, 0, 0, 0, 0);

    auto nodes2_vec = final_builder.CreateVector(nodes);
    auto program2 = CreateFrameProgramMeta(
        final_builder,
        final_builder.CreateVector(levels),
        final_builder.CreateVector(ops),
        final_builder.CreateVector(batches),
        final_builder.CreateVector(slots),
        fp ? fp->command_plan.pass_count() > 0 : false,
        false, false,
        final_builder.CreateVector(std::vector<bool>{}),
        0);

    auto descriptor2 = CreateCompiledArtifactDescriptor(
        final_builder,
        header,
        composition_id2,
        graph_id2,
        nodes2_vec,
        program2,
        final_builder.CreateVector(
            std::vector<flatbuffers::Offset<ResourceLifetimeMeta>>{}),
        final_builder.CreateVector(
            std::vector<flatbuffers::Offset<OwnershipTransferMeta>>{}),
        final_builder.CreateVector(std::vector<std::uint8_t>{}),
        final_builder.CreateVector(
            std::vector<flatbuffers::Offset<ReleaseLevel>>{}),
        final_builder.CreateVector(std::vector<std::uint32_t>{}),
        CreateFrameDataPayload(
            final_builder,
            final_builder.CreateVector(std::vector<std::uint8_t>{}),
            0),
        false);

    final_builder.Finish(descriptor2);

    return std::vector<std::uint8_t>(
        final_builder.GetBufferPointer(),
        final_builder.GetBufferPointer() + final_builder.GetSize());
}

// ── Deserialization: FlatBuffer bytes → LoadedArtifact ──────────────────

std::optional<LoadedArtifact> hydrate_from_flatbuffer(
    const std::vector<std::uint8_t>& flatbuffer) {
    using namespace chronon3d::artifact;

    // ── Verifier ────────────────────────────────────────────────────
    flatbuffers::Verifier verifier(flatbuffer.data(), flatbuffer.size());
    if (!VerifyCompiledArtifactDescriptorBuffer(verifier)) {
        spdlog::warn("[CompiledArtifactCache] FlatBuffer verification "
                      "failed — artifact is corrupt or tampered");
        return std::nullopt;
    }

    const auto* descriptor = flatbuffers::GetRoot<CompiledArtifactDescriptor>(
        flatbuffer.data());
    if (!descriptor) {
        return std::nullopt;
    }

    // ── Version check ───────────────────────────────────────────────
    const auto* header = descriptor->header();
    if (!header) {
        spdlog::warn("[CompiledArtifactCache] Missing header — rejecting");
        return std::nullopt;
    }
    if (header->format_version() != kArtifactFormatVersion) {
        spdlog::warn("[CompiledArtifactCache] Unsupported format version {} "
                      "(expected {}) — rejecting",
                      header->format_version(), kArtifactFormatVersion);
        return std::nullopt;
    }

    // ── Composition metadata ────────────────────────────────────────
    const auto* comp = descriptor->composition();
    if (!comp) {
        spdlog::warn("[CompiledArtifactCache] Missing composition block");
        return std::nullopt;
    }

    LoadedArtifact result;
    if (header->git_commit() && header->git_commit()->size() > 0) {
        result.provenance_git_commit = header->git_commit()->str();
    }

    // ── Reconstruct CompiledComposition ─────────────────────────────
    auto reconstituted = std::make_shared<CompiledComposition>();
    reconstituted->fingerprint = comp->fingerprint();

    // Build a minimal CompositionDefinition from the stored metadata.
    CompositionSpec spec;
    spec.width  = comp->width();
    spec.height = comp->height();
    spec.frame_rate = FrameRate{
        static_cast<chronon3d::i32>(comp->fps_num()),
        static_cast<chronon3d::i32>(comp->fps_den())};
    spec.duration = Frame{comp->duration()};

    auto def = std::make_shared<CompositionDefinition>();
    def->composition = spec;
    reconstituted->definition = def;
    reconstituted->execution_mode = static_cast<SceneExecutionMode>(
        comp->execution_mode());

    // ── Frame program ───────────────────────────────────────────────
    const auto* prog = descriptor->program();
    if (prog) {
        auto frame_prog = std::make_shared<CompiledFrameProgram>();
        if (prog->slots()) {
            for (const auto* sm : *prog->slots()) {
                if (!sm) continue;
                DynamicSlotDesc slot;
                slot.slot_id        = sm->slot_id();
                slot.kind           = static_cast<DynamicSlotKind>(sm->kind());
                slot.offset         = sm->offset();
                slot.size           = sm->size();
                slot.owner_instance = sm->owner_instance();
                frame_prog->slots.push_back(slot);
            }
        }
        reconstituted->frame_program = frame_prog;
    }

    result.composition = reconstituted;
    return result;
}

}  // anonymous namespace

// ── CompiledArtifactCache ─────────────────────────────────────────────────

CompiledArtifactCache::CompiledArtifactCache(std::filesystem::path directory)
    : m_directory(std::move(directory)) {}

std::filesystem::path CompiledArtifactCache::artifact_path(
    std::string_view name) const {
    std::string filename(name);
    filename += kExtension;
    return m_directory / filename;
}

bool CompiledArtifactCache::is_safe_name(std::string_view name) noexcept {
    if (name.empty()) return false;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.') {
            continue;
        }
        return false;
    }
    // Reject path traversal
    if (name.find("..") != std::string_view::npos) return false;
    if (name.find('/') != std::string_view::npos) return false;
    return true;
}

void CompiledArtifactCache::save(std::string_view name,
                                  const CompiledComposition& compiled) const {
    if (!is_safe_name(name)) {
        throw std::runtime_error(
            std::string("CompiledArtifactCache: unsafe artifact name: ")
            + std::string(name));
    }

    std::filesystem::create_directories(m_directory);

    // 1. Serialize to FlatBuffer bytes.
    const auto flatbuffer = serialize_to_flatbuffer(compiled);

    // 2. Compress with zstd via the canonical CacheCompressor.
    auto& compressor = cache_compressor();
    const auto compressed = compressor.compress(flatbuffer.data(),
                                                 flatbuffer.size(),
                                                 kArtifactCompressionLevel);

    spdlog::debug("[CompiledArtifactCache] Serialized {} → {} bytes ({:.1f}%)",
                  flatbuffer.size(), compressed.size(),
                  100.0 * compressed.size() / flatbuffer.size());

    // 3. Write atomically.
    const auto path = artifact_path(name);
    write_binary_atomic(path, compressed);

    spdlog::info("[CompiledArtifactCache] Saved artifact '{}' ({} bytes)",
                 name, compressed.size());
}

std::optional<LoadedArtifact> CompiledArtifactCache::load(
    std::string_view name) const {
    if (!is_safe_name(name)) {
        return std::nullopt;
    }

    const auto path = artifact_path(name);
    if (!std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }

    try {
        // 1. Read compressed blob from disk.
        const auto compressed = read_binary(path);
        if (compressed.empty()) {
            spdlog::warn("[CompiledArtifactCache] Empty artifact file: {}",
                          path.string());
            return std::nullopt;
        }

        // 2. Decompress with zstd.
        auto& compressor = cache_compressor();
        const auto flatbuffer = compressor.decompress(
            compressed.data(), compressed.size());

        spdlog::debug("[CompiledArtifactCache] Decompressed {} → {} bytes",
                      compressed.size(), flatbuffer.size());

        // 3. Verify + hydrate.
        return hydrate_from_flatbuffer(flatbuffer);

    } catch (const std::exception& e) {
        spdlog::error("[CompiledArtifactCache] Load failed for '{}': {}",
                       name, e.what());
        return std::nullopt;
    }
}

bool CompiledArtifactCache::contains(std::string_view name) const noexcept {
    if (!is_safe_name(name)) return false;
    return std::filesystem::is_regular_file(artifact_path(name));
}

void CompiledArtifactCache::remove(std::string_view name) const {
    if (!is_safe_name(name)) return;
    std::error_code ec;
    std::filesystem::remove(artifact_path(name), ec);
}

std::size_t CompiledArtifactCache::clear() const {
    std::size_t count = 0;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(m_directory, ec)) {
        if (ec) break;
        if (entry.is_regular_file() &&
            entry.path().extension() == kExtension) {
            ++count;
            std::filesystem::remove(entry.path(), ec);
        }
    }
    return count;
}

}  // namespace chronon3d::cache