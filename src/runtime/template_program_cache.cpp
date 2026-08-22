// ──────────────────────────────────────────────────────────────────────────────
// src/runtime/template_program_cache.cpp — Fase H (TICKET-VIDEO-COMPILER-ARCH-V1)
// TemplateProgramCache: bounded LRU (canonical cache::LruCache) + pinned
// active-job residency overlay.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/runtime/template_program_cache.hpp>

#include <utility>

namespace chronon3d::runtime {

// ── TemplatePin ──────────────────────────────────────────────────────────────

void TemplatePin::release() noexcept {
    if (m_cache) {
        m_cache->unpin(m_key);
        m_cache = nullptr;
    }
}

// ── TemplateProgramCache ─────────────────────────────────────────────────────

TemplateProgramCache::TemplateProgramCache(std::size_t capacity_entries)
    : m_lru(capacity_entries == 0 ? std::size_t{1} : capacity_entries,
            /*num_shards=*/1,  // single LRU: compiled templates are few + read-mostly
            cache::CapacityMode::Count)
    , m_capacity(capacity_entries == 0 ? ResidencyBudget::kDefaultCompiledPrograms
                                       : capacity_entries) {}

TemplateProgramCache::TemplateProgramCache(const ResidencyBudget& budget)
    : TemplateProgramCache(budget.compiled_programs) {}

std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram>
TemplateProgramCache::compile(
    const TemplateProgramKey& key,
    const std::function<
        std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram>()>& builder) {
    // ── Pinned tier takes priority ─────────────────────────────────────
    {
        std::lock_guard lock(m_pinned_mutex);
        auto it = m_pinned.find(key);
        if (it != m_pinned.end()) {
            return it->second;
        }
    }

    // ── LRU tier (compute-on-miss) ─────────────────────────────────────
    return m_lru.compute_if_absent(key, [&]() {
        return std::make_pair(builder(), std::size_t{1});
    });
}

TemplatePin TemplateProgramCache::pin(
    const TemplateProgramKey& key,
    const std::function<
        std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram>()>& builder) {
    // Already pinned?  Return a fresh lease.
    {
        std::lock_guard lock(m_pinned_mutex);
        if (m_pinned.find(key) != m_pinned.end()) {
            return TemplatePin(this, key);
        }
    }

    // Promote from LRU if present, else compile directly into the pinned tier.
    std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram> program;
    if (auto opt = m_lru.get(key)) {
        program = *opt;
        m_lru.erase(key, /*notify=*/false);
    } else {
        program = builder();
    }

    {
        std::lock_guard lock(m_pinned_mutex);
        // Double-check: another thread may have pinned concurrently.
        auto it = m_pinned.find(key);
        if (it == m_pinned.end()) {
            m_pinned[key] = program;
        }
    }

    return TemplatePin(this, key);
}

std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram>
TemplateProgramCache::find(const TemplateProgramKey& key) {
    {
        std::lock_guard lock(m_pinned_mutex);
        auto it = m_pinned.find(key);
        if (it != m_pinned.end()) {
            return it->second;
        }
    }
    auto opt = m_lru.get(key);
    return opt ? *opt : nullptr;
}

void TemplateProgramCache::unpin(const TemplateProgramKey& key) {
    std::shared_ptr<const chronon3d::graph::CompiledTemplateProgram> program;
    {
        std::lock_guard lock(m_pinned_mutex);
        auto it = m_pinned.find(key);
        if (it == m_pinned.end()) {
            return;  // nothing pinned (or already released)
        }
        program = it->second;
        m_pinned.erase(it);
    }
    if (program) {
        m_lru.put(key, program, std::size_t{1});
    }
}

void TemplateProgramCache::clear() {
    m_lru.clear();
    std::lock_guard lock(m_pinned_mutex);
    m_pinned.clear();
}

std::size_t TemplateProgramCache::capacity() const noexcept {
    return m_capacity;
}

TemplateProgramCache::Stats TemplateProgramCache::stats() {
    const auto s = m_lru.stats();
    std::lock_guard lock(m_pinned_mutex);
    return Stats{
        .hits          = s.hits,
        .misses        = s.misses,
        .evictions     = s.evictions,
        .lru_entries   = s.current_size,
        .pinned_entries = m_pinned.size(),
        .total_entries = s.current_size + m_pinned.size(),
    };
}

} // namespace chronon3d::runtime
