#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// template_program_cache.hpp — ProgramCache: compiled program residency
//
// Cache family: ProgramCache (see cache/cache_taxonomy.hpp).
//
// Fase H: template program cache with residency.  Keys by ProgramFingerprint
// (topology_hash + renderer_abi + quality_profile).  Two tiers: LRU eviction
// + Pinned (active-job) residency via TemplatePin RAII handles.
//
// A bounded, thread-safe cache keyed by ProgramFingerprint (the Fase A
// template key: topology_hash + renderer_abi + quality_profile — this IS the
// ticket's TemplateProgramKey).  Two tiers:
//
//   • LRU tier — canonical cache::LruCache (Count mode), evicts cold
//     templates first.
//   • Pinned tier — active-job residency.  While a TemplatePin RAII handle
//     is alive, its template cannot be evicted; on release it returns to the
//     LRU pool.
//
// The cache composes the canonical primitives (LruCache) — no second cache
// primitive.  This is the compiled-template analogue of the existing
// OverlayTemplateCache (which keys coarse OverlayTemplateDesc GPU command
// plans); this cache keys compiled programs by fingerprint.
//
// Ticket: TICKET-VIDEO-COMPILER-ARCH-V1 §Fase H
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/render_graph/compiler/compiled_template_program.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace chronon3d::runtime {

// ── ResidencyBudget ──────────────────────────────────────────────────────────
//
/// Per-category residency budget (Fase H).  `compiled_programs` is the budget
/// consumed by this cache; the other categories are the daemon-level budgets
/// for textures / glyph atlas / baked surfaces (resolved in later phases).
struct ResidencyBudget {
    std::size_t textures{0};          // GPU texture surfaces
    std::size_t glyph_atlas{0};       // glyph atlas slots
    std::size_t baked_templates{0};   // baked static-island surfaces
    std::size_t compiled_programs{0}; // CompiledTemplateProgram entries

    /// Canonical default budget (entry counts).
    static constexpr std::size_t kDefaultCompiledPrograms = 16;

    [[nodiscard]] bool operator==(const ResidencyBudget&) const noexcept = default;
};

// ── TemplateProgramKey alias ─────────────────────────────────────────────────
//
/// The ticket's TemplateProgramKey is the Fase A ProgramFingerprint:
/// { topology_hash, renderer_abi, quality_profile }.  Bindings (text strings,
/// image contents, video URLs) are intentionally excluded — they are job
/// bindings, not program identity.
using TemplateProgramKey = chronon3d::graph::ProgramFingerprint;

// ── TemplatePin (RAII pinned residency) ──────────────────────────────────────
//
/// While alive, the referenced template is immune to LRU eviction (active-job
/// pinned residency).  Destruction returns the template to the LRU pool.
class TemplatePin {
public:
    TemplatePin() noexcept = default;

    TemplatePin(TemplatePin&& other) noexcept
        : m_cache(other.m_cache), m_key(other.m_key) {
        other.m_cache = nullptr;
    }

    TemplatePin& operator=(TemplatePin&& other) noexcept {
        if (this != &other) {
            release();
            m_cache = other.m_cache;
            m_key   = other.m_key;
            other.m_cache = nullptr;
        }
        return *this;
    }

    TemplatePin(const TemplatePin&) = delete;
    TemplatePin& operator=(const TemplatePin&) = delete;

    ~TemplatePin() { release(); }

    [[nodiscard]] bool valid() const noexcept { return m_cache != nullptr; }

private:
    friend class TemplateProgramCache;
    TemplatePin(class TemplateProgramCache* cache, TemplateProgramKey key) noexcept
        : m_cache(cache), m_key(key) {}

    void release() noexcept;

    class TemplateProgramCache* m_cache{nullptr};
    TemplateProgramKey          m_key{};
};

// ── TemplateProgramCache ─────────────────────────────────────────────────────
//
/// Bounded LRU cache of compiled template programs with active-job pinning.
/// `compile()` returns the cached program for a fingerprint, invoking the
/// builder only on a miss (and after eviction).  `pin()` leases the program
/// for an active job (cannot be evicted); the lease ends when the returned
/// TemplatePin is destroyed.
class TemplateProgramCache {
public:
    struct Stats {
        std::size_t hits{0};
        std::size_t misses{0};
        std::size_t evictions{0};
        std::size_t lru_entries{0};
        std::size_t pinned_entries{0};
        std::size_t total_entries{0};
    };

    explicit TemplateProgramCache(std::size_t capacity_entries =
        ResidencyBudget::kDefaultCompiledPrograms);

    /// Construct from a full residency budget; `compiled_programs` is the
    /// capacity of this cache (0 → canonical default).
    explicit TemplateProgramCache(const ResidencyBudget& budget);

    /// Resolve (and on miss, compile) the template program for `key`.
    /// Pinned entries take priority over the LRU.
    std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram> compile(
        const TemplateProgramKey& key,
        const std::function<
            std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram>()>& builder);

    /// Lease `key` for an active job: the template cannot be evicted until
    /// the returned TemplatePin is destroyed.  If absent, compiles via the
    /// builder into the pinned tier.
    TemplatePin pin(
        const TemplateProgramKey& key,
        const std::function<
            std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram>()>& builder);

    /// Look up a template without compiling (nullptr when absent).
    [[nodiscard]] std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram>
    find(const TemplateProgramKey& key);

    void clear();
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] Stats stats();

private:
    friend class TemplatePin;
    void unpin(const TemplateProgramKey& key);

    cache::LruCache<
        TemplateProgramKey,
        std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram>>
        m_lru;

    // Pinned tier: active-job residency, excluded from eviction.
    mutable std::mutex m_pinned_mutex;
    std::unordered_map<
        TemplateProgramKey,
        std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram>>
        m_pinned;

    std::size_t m_capacity;
};

} // namespace chronon3d::runtime
