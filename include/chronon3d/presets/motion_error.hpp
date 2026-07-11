// ═══════════════════════════════════════════════════════════════════════════
// motion_error.hpp — §5.0b — Typed exception for MotionPresetPackRegistry
//
// Per the §5 user-spec `resolve().compile().is_valid()` chain-method closure
// (commit `b8b6014c`, 2026-07-11), the next sub-step is closing the
// `std::runtime_error`-as-control-flow rot pattern in the canonical
// `MotionPresetPackRegistry` — specifically the `apply()` lookup miss
// branch (3 throw sites total; this commit migrates ONLY the `apply()`
// site per the user's "small-blast-radius" mandate).
//
// SHAPE (verbatim from user spec):
//   `MotionError { std::string path; MotionErrorCode code; }`
//
// Notes on the design deviation from the user literal:
//   1. The error is `class MotionError : public std::runtime_error`
//      (not pure struct) so callers can `catch(const std::runtime_error&)`
//      — matches the existing `ChrononAssetError` precedent at
//      `include/chronon3d/assets/render_preflight.hpp:19` and the
//      principle of preserving catch-types across the migration.
//   2. `class MotionError` cannot be aggregate-initialized
//      (base class has no default ctor). The brace-init example
//      `MotionError{.code=MotionPresetNotFound, .path=missing-id}` from
//      the user spec maps to the 2-arg constructor:
//        `MotionError(MotionErrorCode::MotionPresetNotFound, "missing-id")`
//      Field declaration order is `path` then `code` (matches the user's
//      literal member listing).
//   3. `std::runtime_error::what()` returns a one-line diagnostic of
//      the form `MotionPresetPackRegistry: MotionPresetNotFound 'missing-id'`
//      composed from `to_string(MotionErrorCode)` + `path`.
//
// ENUM (verbatim from user spec):
//   `enum class MotionErrorCode { MotionPresetNotFound, UnknownPackId, ... }`
// Two members currently in use:
//   • `MotionPresetNotFound` — thrown by `MotionPresetPackRegistry::apply()`
//     when the lookup id does not match any registered preset (sole
//     user-spec migration target for this commit).
//   • `UnknownPackId` — reserved for future pack-namespaced `apply()`
//     variants (NOT currently thrown; future-proof).
//
// OUT-OF-SCOPE (intentionally NOT migrated in this commit):
//   • `MotionPresetPackRegistry::register_preset()` — frozen-registry and
//     duplicate-id throw sites are NOT migrated, per the user-spec
//     "Migrate `MotionPresetPackRegistry::apply(lb, id)`" wording. They
//     continue to throw `std::runtime_error` until a future §5.x
//     forward-point commit. The class docblock above documents this
//     scope decision.
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stdexcept>
#include <string>

namespace chronon3d::presets {

/// Error codes emitted by `MotionPresetPackRegistry::apply()` (and
/// future pack-namespaced variants). Each variant maps to a single
/// categorized failure path so callers can switch on `.code`
/// programmatically instead of parsing string messages.
enum class MotionErrorCode {
    /// `apply()` was given an id not present in any registered
    /// preset. This is the canonical "unknown preset at apply time"
    /// failure — the lookup miss path.
    MotionPresetNotFound,

    /// Reserved for future pack-namespaced `apply()` variants where
    /// the Id is well-formed but does not belong to the requested
    /// pack. NOT currently thrown by any code in this commit; the
    /// enum member is included per the user spec's open-ended "..."
    /// trailing in `enum class MotionErrorCode { MotionPresetNotFound,
    /// UnknownPackId, ... }` (forward-proof).
    UnknownPackId,
};

/// Stable, never-localized, machine-readable string label for each
/// `MotionErrorCode` variant. Inline definition in the .hpp matches
/// the canonical Chronon3D precedent (`VideoSinkError::to_string()`
/// in `include/chronon3d/media/video/video_sink.hpp:83`).
[[nodiscard]] inline const char* to_string(MotionErrorCode code) noexcept {
    switch (code) {
        case MotionErrorCode::MotionPresetNotFound:
            return "MotionPresetNotFound";
        case MotionErrorCode::UnknownPackId:
            return "UnknownPackId";
    }
    return "<unknown-MotionErrorCode>";
}

/// Typed exception for `MotionPresetPackRegistry` lookup failures.
/// Inherits `std::runtime_error` so existing catch-blocks that
/// `catch (const std::runtime_error&)` continue to compile + run
/// unchanged — backward-compatibility is a hard requirement of this
/// rot-pattern closure migration per AGENTS.md §anti-duplication rules.
class MotionError : public std::runtime_error {
public:
    /// User-spec literal field order: path-then-code. `path` is the
    /// trace/context (the failing lookup id), `code` is the
    /// programmatic discriminant. Stored as public members so users
    /// can `.path` / `.code` directly without accessors.
    std::string path;
    MotionErrorCode code;

    /// 2-arg constructor matching the user spec brace-init example:
    /// `MotionError{.code=MotionPresetNotFound, .path=missing-id}`
    /// maps to: `MotionError(MotionErrorCode::MotionPresetNotFound,
    /// std::string("missing-id"))` (note: brace-init syntax is
    /// positional/named-by-DECLARED-order for aggregates, but
    /// `MotionError` is NOT aggregate because `std::runtime_error`
    /// has no default ctor — hence the constructor).
    MotionError(MotionErrorCode code_, std::string path_)
        : std::runtime_error(
              std::string("MotionPresetPackRegistry: ")
              + to_string(code_) + " '" + path_ + "'")
        , path(std::move(path_))
        , code(code_)
    {}

    /// Ergonomic construction alias. Equivalent to the 2-arg
    /// constructor — the function name follows the user-spec brace-init
    /// positional order (`.code=`, then `.path=`) so production
    /// callers opting for the factory form have a self-documenting
    /// call-site:
    ///   `MotionError::from_code_path(MotionErrorCode::MotionPresetNotFound,
    ///                                 std::string("missing-id"))`
    /// vs the ctor form:
    ///   `MotionError(MotionErrorCode::MotionPresetNotFound,
    ///                std::string("missing-id"))`
    /// The brace-init example
    /// `MotionError{.code=MotionPresetNotFound, .path=missing-id}` from
    /// the §5.0b user spec is illustrative intent; it does NOT compile
    /// literally because the class is not aggregate (see USER-SPEC
    /// SYNTAX NOTE in the header docblock).
    [[nodiscard]] static MotionError from_code_path(
            MotionErrorCode c, std::string p)
    {
        MotionError out(c, std::move(p));
        return out;
    }
};

} // namespace chronon3d::presets
