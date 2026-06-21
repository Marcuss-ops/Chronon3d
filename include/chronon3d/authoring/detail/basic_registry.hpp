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
//
//   5. **Merge proxy nullopt return.** `merge()` proxies resolve via
//      `source.resolve(id)` on every call. If the source entry vanishes
//      after merge (concurrent unregister / clear), the proxy returns
//      `Value{}` — current generators (`TextStyle`, `TextAnimatorSpec`)
//      are default-constructible, but if a future `BasicRegistry<X>`
//      takes a non-default-constructible `X`, the proxy lambda will
//      fail to compile.  Acceptable failure mode.
//
// ── PR 5.1 additions (this PR) ─────────────────────────────────────────
//
//   A. **`register_range(InputIt, InputIt)`** — bulk registration of
//      `(string, Value)` or `(string, Factory)` pairs from any iterator
//      range.  Type dispatch at compile time via `std::is_invocable_r_v`:
//      if the pair's `second` field is invocable returning `Value`, it's
//      treated as a factory (preserved verbatim); otherwise wrapped in a
//      stateful lambda that returns the captured value (mirrors
//      `register_value`).  Empty ids silently skipped (consistent with
//      the existing `register_*` no-ops).
//
//   B. **`merge(const BasicRegistry& source)`** — layer-cake preset
//      composition: copy every entry from `source` into this registry,
//      silently overriding on duplicate `id`.  Implements proxy-lambda
//      semantics: each merged entry is a thin wrapper that calls
//      `source.resolve(id)` on every lookup.  This preserves source's
//      parametric factories (which might capture by-reference) without
//      copying.  **LIFETIME INVARIANT**: source must outlive target.
//      See `docs/migrations/2026-06-authoring-registry.md` for the full
//      lifetime contract.
//
//      Locking: `std::lock(target_mutex_, source.mutex_)` acquires both
//      in a deadlock-free order; held for the duration of the merge.
//      Self-merge (`this == &source`) is a no-op (would deadlock under
//      the std::lock pessimisation).
//
//      *Concurrent source mutation safety*: if `source` is mutated
//      (clear / unregister / register) AFTER the merge, the proxy can
//      observe a missing entry. The proxy returns `Value{}` instead of
//      dereferencing a nullopt — `assert()` would be a release-build
//      no-op causing UB. **REQUIRES `Value` to be default-constructible**;
//      `TextStyle` and `TextAnimatorSpec` both satisfy this today. If a
//      future `BasicRegistry<X>` takes a non-default-constructible `X`,
//      the proxy lambda fails to compile (good failure mode).
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <type_traits>
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

    // ── PR 5.1 — bulk registration ────────────────────────────────────
    //
    // Walks the iterator range and registers each `(string, Value|Factory)`
    // pair.  Empty `id` fields are silently skipped (consistent with the
    // existing single-call `register_*` family).
    //
    // Type dispatch (compile-time):
    //   - If `pair.second` is invocable returning `Value` (e.g. a captured
    //     lambda, a `factory_type` itself, or any compatible callable),
    //     it is forwarded verbatim as a factory entry.
    //   - Otherwise, it is captured as a stateful lambda that returns a
    //     moved-out copy on every invocation — mirroring `register_value`.
    //
    // This lets the caller mix value pairs and factory pairs in the same
    // range without explicit dispatch at the call site.
    template <class InputIt>
    BasicRegistry& register_range(InputIt first, InputIt last) & {
        std::unique_lock lock(mutex_);
        for (auto it = first; it != last; ++it) {
            auto id = std::string{it->first};
            if (id.empty()) continue;
            using Second = std::decay_t<decltype(it->second)>;
            if constexpr (std::is_invocable_r_v<Value, Second>) {
                // Callable matching `Value()` → forward as a factory.
                entries_[std::move(id)] = it->second;
            } else {
                // Plain value → wrap in a stateful returning lambda.
                entries_[std::move(id)] = [s = it->second]() mutable {
                    return std::move(s);
                };
            }
        }
        return *this;
    }

    // ── PR 5.1 — layer-cake preset composition ────────────────────────
    //
    // Copies every entry from `source` into this registry. Silent override
    // on duplicate `id` (matches single-call `register_*` semantics).
    //
    // Implementation note: each merged entry is a proxy lambda that calls
    // `source.resolve(id)` on every lookup.  This preserves source's
    // parametric factories (which might capture by-reference) without
    // copying — copying a lambda that captures by-reference into the
    // target's std::function would dangle if the captured context goes
    // out of scope.  Source controls its own captured state.
    //
    // **LIFETIME INVARIANT**: source must outlive target. If target is
    // resolved after source is destroyed, the proxy std::function will
    // dereference dead captured state → UB.  Document this in call sites
    // (typically: builtin packs, host-owned singleton-like instances, or
    // long-lived static registries satisfy this).
    //
    // Locking: `std::lock(target_mutex_, source.mutex_)` acquires both in
    // a deadlock-free order (the std::lock algorithm picks one of the
    // two safe orderings).  Both held for the duration of the merge.
    // Self-merge is a no-op; pinning `this == &source` upfront avoids a
    // std::lock deadlock against a recursive mutex upgrade that this
    // shared_mutex does NOT support.
    BasicRegistry& merge(const BasicRegistry& source) & {
        if (this == &source) return *this;
        std::unique_lock target_lock(mutex_,     std::defer_lock);
        std::shared_lock source_lock(source.mutex_, std::defer_lock);
        std::lock(target_lock, source_lock);
        for (const auto& [id, _] : source.entries_) {
            // Proxy: each lookup re-invokes `source.resolve(id)`.
            // `id_owned` is a string copy so the lambda outlives any
            // container of `source.entries_` updates.
            //
            // Concurrent source mutation safety: if `source` is mutated
            // (clear / unregister / register) AFTER the merge, the proxy
            // can observe a missing entry. We return `Value{}` (Type
            // must be default-constructible) rather than dereferencing
            // a nullopt — `assert()` would be a release-build no-op
            // causing UB. Documented in the merge() docstring above.
            entries_[id] = [&source, id_owned = id]() -> Value {
                if (auto res = source.resolve(id_owned); res.has_value()) {
                    return std::move(*res);
                }
                return Value{};
            };
        }
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
