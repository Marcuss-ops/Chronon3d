// ═══════════════════════════════════════════════════════════════════════════
// detail::BasicRegistry<Value> — shared implementation for authoring
// string-keyed preset registries.
//
// `chronon3d::authoring::StyleRegistry` and `chronon3d::authoring::MotionRegistry`
// are thin public subclasses of this template; they add a domain-specific
// alias (`register_style`, `register_motion`) and re-`using` every other
// method verbatim. Future registries (e.g. PR 7: shape/effect/composition
// preset registries) follow the same pattern.
//
// Why an internal template:
//   • Anti-duplication (docs/ANTI_DUPLICATION_RULES.md §2): both registries
//     share the same lookup / lifecycle / threading surface; keeping two
//     full copies was a code-hygiene smell — easier to fork, harder to
//     audit.  Templates let users read one defined behaviour.
//   • The public type names remain readable at call sites:
//        .style("youtube.hero.premium")   → StyleRegistry::resolve
//        .motion("text.reveal.soft")       → MotionRegistry::resolve
//     — type aliases would also work but erasure at the call site has
//     inferior IDE / debugger behaviour.
//
// ── Ownership / Threading ───────────────────────────────────────────────
//
//   • Instance-based, no global singleton (anti-duplication §3).
//   • No copy / no move — pinned to the host that built it.
//   • `shared_mutex`: registrations are exclusive (rare); lookups are
//     concurrent (hot).  `register_*` and `resolve_*` never run on the
//     same thread at the same time.
//
// ── Lifecycle constraints (READ THESE) ──────────────────────────────────
//
//   1. **Factory re-evaluation.**  The `std::function<Value()>` registered
//      via `register_factory()` is INVOKED on every `resolve()`. This
//      keeps the surface uniform with parametric presets (e.g.
//      `"text.reveal.soft" -> factory(duration, ease, accent)`). If you
//      want memoisation, capture the result once inside your factory:
//
//        reg.register_factory("static.motion", [&cache]() {
//            if (!cache.has_value()) cache = compute_once();
//            return *cache;
//        });
//
//   2. **No re-entrancy.**  The factory body MUST NOT call back into the
//      SAME registry (would deadlock against the shared→unique upgrade).
//      Chain through a separate registry if you need cross-lookups.
//
//   3. **Silent override on `register_*`.**  Re-registering an existing
//      `id` overwrites the prior entry without warning. This matches the
//      "layer-cake preset" authoring model where studio packs overwrite
//      builtin defaults; silently is intentional. Wrap your registration
//      logic with `has()` checks if your pack needs stricter semantics.
//
//   4. **No silent eviction.**  Registries are NOT backed by LruCache.
//      Missing keys resolve to `std::nullopt` — never `LruMiss`. Treat
//      `nullopt` as a hard authoring error at the call site.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <algorithm>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace chronon3d::authoring::detail {

template <class Value>
class BasicRegistry {
public:
    using value_type  = Value;
    using factory_type = std::function<Value()>;

    explicit BasicRegistry(std::size_t reserve_hint = 16) {
        entries_.reserve(reserve_hint);
    }

    // Pinned-to-host: no copy / no move.
    BasicRegistry(const BasicRegistry&)            = delete;
    BasicRegistry& operator=(const BasicRegistry&) = delete;
    BasicRegistry(BasicRegistry&&)                 = delete;
    BasicRegistry& operator=(BasicRegistry&&)      = delete;

    // ── Registration (silently overrides on duplicate id) ─────────────
    BasicRegistry& register_value(std::string id, Value value) & {
        if (id.empty()) return *this;
        std::unique_lock lock(mutex_);
        entries_[std::move(id)] = [s = std::move(value)]() mutable {
            return std::move(s);
        };
        return *this;
    }

    /// Factory is INVOKED on every resolve — see header docstring §1.
    /// MUST NOT re-enter the registry from inside the factory — §2.
    BasicRegistry& register_factory(std::string id, factory_type factory) & {
        if (id.empty() || !factory) return *this;
        std::unique_lock lock(mutex_);
        entries_[std::move(id)] = std::move(factory);
        return *this;
    }

    bool unregister(std::string_view id) {
        std::unique_lock lock(mutex_);
        return entries_.erase(std::string{id}) > 0;
    }

    void clear() {
        std::unique_lock lock(mutex_);
        entries_.clear();
    }

    // ── Lookup ──────────────────────────────────────────────────────────
    [[nodiscard]]
    std::optional<Value> resolve(std::string_view id) const {
        std::shared_lock lock(mutex_);
        auto it = entries_.find(std::string{id});
        if (it == entries_.end()) return std::nullopt;
        return it->second();
    }

    [[nodiscard]] bool has(std::string_view id) const {
        std::shared_lock lock(mutex_);
        return entries_.find(std::string{id}) != entries_.end();
    }

    [[nodiscard]] std::size_t count() const noexcept {
        std::shared_lock lock(mutex_);
        return entries_.size();
    }

    [[nodiscard]]
    std::vector<std::string> ids() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> out;
        out.reserve(entries_.size());
        for (const auto& [k, _] : entries_) out.push_back(k);
        std::sort(out.begin(), out.end());
        return out;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, factory_type> entries_;
};

} // namespace chronon3d::authoring::detail
