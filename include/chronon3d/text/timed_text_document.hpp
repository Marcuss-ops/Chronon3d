#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// timed_text_document.hpp — Canonical timed text model (FASE 4b)
//
// TimedTextDocument is the single typed model for all subtitle/caption
// formats (SRT, VTT, JSON, ASS, TTML, etc.).  Adapters convert FROM
// their source format INTO this model — never the reverse.
//
// Design principles:
//   1. One canonical type — no per-format variants
//   2. All timing in seconds (float) — adapters normalise from SRT ms /
//      VTT ms / JSON seconds
//   3. Words carry semantic IDs for analytics, audio sync, and
//      word-level kinetic effects (karaoke, per-word pop, etc.)
//   4. Speaker + style metadata live on cues — enough to drive
//      speaker-dependent styling without a full CSS cascade
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/types.hpp>  // f32, u64
#include <optional>
#include <string>
#include <vector>

namespace chronon3d {

// ── TimedWord — word-level timing + semantic identity ───────────────────

/// A single word with its timing window and a stable semantic identifier.
/// The semantic_id is used for analytics, audio sync, and per-word kinetic
/// effects (karaoke highlighting, pop animations, etc.).  It survives
/// across adapter runs and is hashable for cache keys.
struct TimedWord {
    std::string text;           ///< the word itself (UTF-8), no surrounding whitespace
    f32         start_s{0.0f};  ///< word start in seconds
    f32         end_s{0.0f};    ///< word end in seconds
    std::string semantic_id;    ///< stable id, e.g. "cue3-word4" or "keyword-1"
};

// ── WordTimingQuality — provenance classification of per-word timing ────
//
// TICKET-WORD-TIMING-QUALITY.  Per-cue discriminator that lets the
// renderer decide whether pop/highlight animations are trustable
// animation primitives or only uniform-split approximations.
//
//   None          — cue has no per-word breakdown (text-only cues)
//   Estimated     — per-word timing derived by uniform split across the
//                   cue duration (SRT/VTT always; JSON when the
//                   "words" array is absent and an auto-fallback fires)
//   Authoritative — per-word timing comes verbatim from the source
//                   format (JSON/Whisper-only when the source actually
//                   provides per-word start/end)
//
// The enum lives at per-cue granularity (NOT per-document) because a
// JSON document can mix cues with and without word arrays; a per-doc
// flag would either lie about the worst case or pessimise the best
// case.  See docs/tickets/TICKET-WORD-TIMING-QUALITY.md for rationale.
enum class WordTimingQuality {
    None,
    Estimated,
    Authoritative,
};

// ── TimedCueStyle — per-cue style metadata ──────────────────────────────

/// Speaker-dependent styling hints carried on each cue.  All fields are
/// optional — empty means "inherit from document defaults".  Colours are
/// STRING keys (e.g. "#FFCC00", "gold", "rgba(255,200,0,0.9)") that get
/// resolved to RGBA by the rendering layer; we keep them as strings here
/// to avoid coupling the model to Color/TextMaterial.
struct TimedCueStyle {
    std::optional<std::string> color;         ///< foreground colour key
    std::optional<std::string> background;    ///< background highlight
    std::optional<f32>         font_size;     ///< multiplier vs doc default
    std::optional<u32>         font_weight;   ///< 100..900
};

// ── TimedCue — a single subtitle/caption cue ────────────────────────────

/// One subtitle event with absolute timing (seconds), speaker identity,
/// and optional per-word breakdown.
struct TimedCue {
    std::string              speaker;         ///< speaker label, e.g. "John" / "Narrator"
    f32                      start_s{0.0f};   ///< cue start in seconds
    f32                      end_s{0.0f};     ///< cue end in seconds
    std::string              text;            ///< full cue text (includes newlines for multi-line)
    std::vector<TimedWord>   words;           ///< per-word timing (empty = no word-level data)
    std::optional<TimedCueStyle> style;       ///< cue-level style overrides

    /// Unique identifier derived from the source format.  For SRT this is
    /// the index (1-based string); for VTT the cue id if present; for
    /// JSON the id field.  Used for cache-key hashing and debugging.
    std::string source_id;

    /// Provenance classification of the per-word timing in `words`.
    /// TICKET-WORD-TIMING-QUALITY: per-cue field.  Adapters MUST set
    /// this so the renderer can distinguish trustworthy (Authoritative)
    /// word-level animation primitives from uniform-split estimates.
    /// Defaults to `None` so a default-constructed cue advertises
    /// "no per-word data" — never silently False-positive as Estimated.
    WordTimingQuality word_timing_quality{WordTimingQuality::None};

    [[nodiscard]] bool operator==(const TimedCue&) const noexcept = default;
};

// ── TimedTextDocument — canonical timed text container ──────────────────

/// The single typed model that every subtitle/caption adapter produces.
/// Consumed by TextRunBuilder::from_timed_text_document() (future wiring)
/// and by the render-graph timed-text node.
struct TimedTextDocument {
    std::vector<TimedCue> cues;

    // Document-level metadata.
    std::string language;       ///< BCP-47, e.g. "en", "it", "ja"
    std::string title;          ///< optional title (VTT header, JSON metadata)
    std::string source_format;  ///< "srt" / "vtt" / "json" / "ass" — informational

    [[nodiscard]] bool operator==(const TimedTextDocument&) const noexcept = default;
};

// ── Free functions ──────────────────────────────────────────────────────

/// Hash a TimedTextDocument for cache keys.
[[nodiscard]] u64 hash_timed_text_document(const TimedTextDocument& doc);

/// Hash a single TimedCue.
[[nodiscard]] u64 hash_timed_cue(const TimedCue& cue);

// ── Adapters: source format → TimedTextDocument ──────────────────────
//
// Each adapter parses a raw string in a specific format and returns a
// fully materialised TimedTextDocument.  The adapter does NOT convert
// back; the canonical model is the single destination.
//
// Error handling: adapters return an empty TimedTextDocument on parse
// failure.  Callers should check `cues.empty()` to detect errors.

/// Parse SRT (SubRip) format into TimedTextDocument.
/// Supports: index lines, HH:MM:SS,mmm --> HH:MM:SS,mmm timestamps,
/// single- and multi-line cue text, blank-line cue separator.
[[nodiscard]] TimedTextDocument timed_text_from_srt(const std::string& raw);

/// Parse WebVTT format into TimedTextDocument.
/// Supports: WEBVTT header, optional cue ids, HH:MM:SS.mmm timestamps,
/// optional style blocks (ignored — styling is metadata-only), blank-line
/// cue separator.
[[nodiscard]] TimedTextDocument timed_text_from_vtt(const std::string& raw);

/// Parse JSON subtitle format into TimedTextDocument.
/// Expected schema:
///   {
///     "language": "en",
///     "title": "...",
///     "cues": [{
///       "id": "c1",
///       "speaker": "John",
///       "start": 1.0, "end": 4.0,
///       "text": "Hello world",
///       "words": [{"word": "Hello", "start": 1.0, "end": 1.5, "id": "w1"}],
///       "style": {"color": "#FFF", "font_size": 1.2, "font_weight": 600}
///     }]
///   }
///
/// All fields except "text", "start", "end" are optional.
[[nodiscard]] TimedTextDocument timed_text_from_json(const std::string& raw);

} // namespace chronon3d
