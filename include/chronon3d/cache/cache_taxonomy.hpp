#pragma once

// =============================================================================
// cache_taxonomy.hpp — canonical cache-family classification
//
// Every cache in chronon3d MUST belong to exactly ONE of the following three
// families.  Infrastructure (LruCache, CacheDiagnostics, cache_policy) is
// cross-cutting plumbing, not a cache itself — but every cache instance must
// self-identify with one of these families.
//
// Generic in-memory key/value caches MUST delegate storage/eviction to LruCache.
// Specialized residency managers (for example FramebufferPool) and persistent
// artifact stores may own domain-specific allocation/I/O machinery, but they do
// not define a fourth cache family or a second generic cache primitive.
//
// The taxonomy is canonical: no new cache family is allowed.  If a new cache
// concept does not fit one of these three, the concept itself must be
// redesigned to fit — or the existing family extended.
// =============================================================================

namespace chronon3d::cache {

// ── CacheFamily enum ────────────────────────────────────────────────────────

enum class CacheFamily : unsigned char {
    /// ContentCache — immutable content byte-identical keys.
    ///
    /// Input:  immutable content identity (digest, hash, or content-derived key).
    /// Output: prepared/dependent representation (rendered framebuffer,
    ///         converted frame, GPU-resident asset, styled glyph).
    ///
    /// Semantics:  same key ⇒ same output, always.  Cache hits are
    ///             deterministic by content, not by memory pressure.
    ///
    /// Members:
    ///   • NodeCache                     — rendered node outputs
    ///   • FrameCache                    — fully rendered frames by scene/render hash
    ///   • VideoFrameCache               — converted video frames
    ///   • ConvertedFrameCache           — output conversion results
    ///   • GpuAssetCache                 — decoded assets → GPU device-local surfaces
    ///   • PreparedAssetDigestCache      — preflight file digest memoization
    ///   • GpuGlyphAtlas styled entries  — cached styled glyph representations
    ContentCache = 1,

    /// ResidencyCache — bounded memory residence governed by an external plan.
    ///
    /// Input:  resource identity (surface handle, slot index, size class).
    /// Output: a backing GPU/CPU allocation that fits within a residency budget.
    ///
    /// Semantics: graph::CompiledResourceTable is the sole persisted compiled
    ///             hot-path placement authority. runtime::ResourcePlanner and
    ///             runtime::ResourcePlan are ephemeral placement machinery, not
    ///             caches. Residency caches materialize/reuse backing storage for
    ///             cold paths, reference execution and persistent residency.
    ///
    /// Members:
    ///   • FramebufferPool                    — cold-path / reference / extension pool
    ///   • GpuGlyphAtlas plain glyph pages    — glyph page residency
    ResidencyCache = 2,

    /// ProgramCache — program identity → compiled executable.
    ///
    /// Input:  program fingerprint (topology hash, renderer ABI, quality profile).
    /// Output: a compiled, ready-to-execute program (CompiledFrameGraph,
    ///         CompiledTemplateProgram, command plan).
    ///
    /// Semantics:  same fingerprint ⇒ same compiled program, always.
    ///             Bindings (textures, scene data) are NOT part of the key;
    ///             they are job bindings applied at dispatch time.
    ///
    /// Members:
    ///   • TemplateProgramCache    — compiled template programs by fingerprint
    ///   • CompiledGraphCache      — compiled frame graphs (dimension-dependent)
    ///   • SceneProgramCache       — scene-specific compiled programs
    ///   • PixelProgram            — canonical semantic IR (graph::pixel::PixelProgram v1)
    ///   • DeviceProgramKey        — lowered device program identity feeding ProgramArtifactKey
    ///   • CompiledArtifactCache   — persistent compiled-program artifact store
    ///                              (I/O adapter, not a second in-memory primitive)
    ///
    /// Every compiled GPU program artifact belongs HERE, whoever produced it:
    ///
    ///   • GLSL → SPIR-V binary
    ///   • Slang → SPIR-V / PTX output
    ///   • CUDA → PTX module, CUBIN
    ///   • SASS specialization for a GPU architecture
    ///   • CUDA graph executable
    ///   • PixelProgram → DeviceProgram lowering (all DeviceTarget values)
    ///     share THIS key: DeviceProgramKey is the pre-persistent ProgramCache
    ///     identity; cache::ProgramArtifactKey is the on-disk stable name.
    ///     There is never a SlangCache / PtxCache / ShaderBinaryRegistry.
    ///
    /// A lowering is NOT a renderer, a backend, a cache family or a registry.
    /// Slang/PTX/SASS are compiler technologies that lower the SAME semantic
    /// program; adding one adds an artifact KIND to this family, never a
    /// `SlangCache` / `PtxCache` / `SassCache` / `ShaderBinaryRegistry`, and
    /// never a second cache engine. The artifact key is the canonical program
    /// fingerprint (render_plan::compute_render_job_fingerprint for job
    /// identity) plus the target/compiler attributes — never a filename, an
    /// object address or an ad-hoc hash. Enforced by the
    /// `no_gpu_binary_cache_family` / `single_cache_primitive` rules in
    /// tools/architecture_rules.toml.
    ProgramCache = 3,
};

// ── Compile-time annotation helper ──────────────────────────────────────────
//
/// Marker used by cache implementations to lock their family assignment in
/// static assertions without introducing another registry or runtime service.
template <CacheFamily Family>
inline constexpr bool cache_family_annotation = true;

} // namespace chronon3d::cache
