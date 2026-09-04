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
    ///   • PersistentFramebufferStore         — named persistent FB storage
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
    ///   • OverlayTemplateCache    — overlay template GPU command plans
    ///   • CompiledArtifactCache   — persistent compiled-program artifact store
    ///                              (I/O adapter, not a second in-memory primitive)
    ProgramCache = 3,
};

// ── Compile-time annotation helper ──────────────────────────────────────────
//
/// Marker used by cache implementations to lock their family assignment in
/// static assertions without introducing another registry or runtime service.
template <CacheFamily Family>
inline constexpr bool cache_family_annotation = true;

} // namespace chronon3d::cache