#pragma once
// ==============================================================================
// chronon3d/core/types/result.hpp
//
// Result<T, E> — a minimal std::expected equivalent for C++20.
//
// Provides:
//   - has_value() / explicit operator bool()
//   - value() / error() — undefined behaviour if accessed in wrong state
//   - operator-> / operator*
//   - Factory: Result<T,E>::ok(value) / Result<T,E>::err(error)
//   - [[nodiscard]] — compiler warns on discarded results
//
// TRY(expr) macro — early-return on error (GNU statement-expression).
//
// Usage:
//   auto r = compile_camera(descriptor, &catalog);
//   if (!r) { return r.error(); }
//   auto program = std::move(r).value();
//
//   // Or with TRY:
//   auto program = CHRONON_TRY(compile_camera(descriptor, &catalog));
// ==============================================================================

#include <cassert>
#include <new>
#include <type_traits>
#include <utility>

namespace chronon3d {

// ═════════════════════════════════════════════════════════════════════════════
//  Result<T, E>
// ═════════════════════════════════════════════════════════════════════════════

template <typename T, typename E>
class [[nodiscard]] Result {
    static_assert(!std::is_reference_v<T>, "Result<T,E>: T must not be a reference");
    static_assert(!std::is_reference_v<E>, "Result<T,E>: E must not be a reference");
    static_assert(!std::is_same_v<std::decay_t<T>, std::decay_t<E>>,
                  "Result<T,E>: T and E must be distinct types");

public:
    using value_type = T;
    using error_type = E;

    // ── Construction from value ──────────────────────────────────────────

    /// Implicit construction from T — the success path.
    /// NOLINTNEXTLINE(google-explicit-constructor)
    Result(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : has_value_(true) {
        ::new (value_ptr()) T(std::move(value));
    }

    /// Implicit construction from T (copy).
    /// NOLINTNEXTLINE(google-explicit-constructor)
    Result(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
        : has_value_(true) {
        ::new (value_ptr()) T(value);
    }

    // ── Construction from error ──────────────────────────────────────────

    /// Implicit construction from E — the error path.
    /// NOLINTNEXTLINE(google-explicit-constructor)
    Result(E&& error) noexcept(std::is_nothrow_move_constructible_v<E>)
        : has_value_(false) {
        ::new (error_ptr()) E(std::move(error));
    }

    /// Implicit construction from E (copy).
    /// NOLINTNEXTLINE(google-explicit-constructor)
    Result(const E& error) noexcept(std::is_nothrow_copy_constructible_v<E>)
        : has_value_(false) {
        ::new (error_ptr()) E(error);
    }

    // ── Move / copy ──────────────────────────────────────────────────────

    Result(Result&& other) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<E>)
        : has_value_(other.has_value_) {
        if (has_value_) {
            ::new (value_ptr()) T(std::move(*other.value_ptr()));
        } else {
            ::new (error_ptr()) E(std::move(*other.error_ptr()));
        }
    }

    Result& operator=(Result&& other) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<E> &&
        std::is_nothrow_destructible_v<T> &&
        std::is_nothrow_destructible_v<E>) {
        if (this != &other) {
            destroy();
            has_value_ = other.has_value_;
            if (has_value_) {
                ::new (value_ptr()) T(std::move(*other.value_ptr()));
            } else {
                ::new (error_ptr()) E(std::move(*other.error_ptr()));
            }
        }
        return *this;
    }

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    ~Result() { destroy(); }

    // ── Observers ────────────────────────────────────────────────────────

    [[nodiscard]] bool has_value() const noexcept { return has_value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value_; }

    /// Return the contained value.  Precondition: has_value() == true.
    [[nodiscard]] T& value() & noexcept {
        assert(has_value_);
        return *value_ptr();
    }
    [[nodiscard]] const T& value() const& noexcept {
        assert(has_value_);
        return *value_ptr();
    }
    [[nodiscard]] T&& value() && noexcept {
        assert(has_value_);
        return std::move(*value_ptr());
    }
    [[nodiscard]] const T&& value() const&& noexcept {
        assert(has_value_);
        return std::move(*value_ptr());
    }

    /// Return the contained error.  Precondition: has_value() == false.
    [[nodiscard]] E& error() & noexcept {
        assert(!has_value_);
        return *error_ptr();
    }
    [[nodiscard]] const E& error() const& noexcept {
        assert(!has_value_);
        return *error_ptr();
    }
    [[nodiscard]] E&& error() && noexcept {
        assert(!has_value_);
        return std::move(*error_ptr());
    }
    [[nodiscard]] const E&& error() const&& noexcept {
        assert(!has_value_);
        return std::move(*error_ptr());
    }

    // ── Pointer access (only valid when has_value) ───────────────────────

    [[nodiscard]] T* operator->() noexcept { return value_ptr(); }
    [[nodiscard]] const T* operator->() const noexcept { return value_ptr(); }
    [[nodiscard]] T& operator*() & noexcept { return value(); }
    [[nodiscard]] const T& operator*() const& noexcept { return value(); }
    [[nodiscard]] T&& operator*() && noexcept { return std::move(*this).value(); }

private:
    void destroy() noexcept {
        if (has_value_) {
            value_ptr()->~T();
        } else {
            error_ptr()->~E();
        }
    }

    [[nodiscard]] T* value_ptr() noexcept {
        return std::launder(reinterpret_cast<T*>(&storage_));
    }
    [[nodiscard]] const T* value_ptr() const noexcept {
        return std::launder(reinterpret_cast<const T*>(&storage_));
    }
    [[nodiscard]] E* error_ptr() noexcept {
        return std::launder(reinterpret_cast<E*>(&storage_));
    }
    [[nodiscard]] const E* error_ptr() const noexcept {
        return std::launder(reinterpret_cast<const E*>(&storage_));
    }

    // Storage: large enough for the larger type, suitably aligned.
    static constexpr size_t kSize  = sizeof(T) > sizeof(E) ? sizeof(T) : sizeof(E);
    static constexpr size_t kAlign = alignof(T) > alignof(E) ? alignof(T) : alignof(E);

    alignas(kAlign) unsigned char storage_[kSize];
    bool has_value_;
};

// ═════════════════════════════════════════════════════════════════════════════
//  TRY macro — propagate error upward
//
//  CHRONON_TRY(expr) evaluates expr, which must return Result<U, E>.
//  On error, returns the error from the enclosing function.
//  On success, evaluates to the unwrapped U&&.
//
//  Requires: the enclosing function returns a type constructible from E,
//  e.g. Result<Something, E> or std::optional<E>.
//
//  Uses GNU statement-expression ({ ... }) — supported by GCC, Clang, ICC.
// ═════════════════════════════════════════════════════════════════════════════

#define CHRONON_TRY(expr)                                                    \
    ({                                                                       \
        auto _chronon_try_result = (expr);                                   \
        if (!_chronon_try_result)                                            \
            return std::move(_chronon_try_result).error();                   \
        std::move(_chronon_try_result).value();                              \
    })

} // namespace chronon3d
