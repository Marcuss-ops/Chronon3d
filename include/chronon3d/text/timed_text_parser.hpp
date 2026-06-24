#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// chronon3d/text/timed_text_parser.hpp — TXT-11 Timed Text Parsers
//
// Parsers for subtitle and word-timing formats:
//   - SRT (SubRip Text) — the most common subtitle format
//   - WebVTT — W3C standard for web subtitles
//   - JSON — custom word-timing JSON for karaoke / active-word highlighting
//
// All parsers produce the canonical model defined in
// include/chronon3d/presets/text/subtitle.hpp:
//   WordTiming → SubtitleCue → SubtitleTrack
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/presets/text/subtitle.hpp>

#include <string>
#include <string_view>

namespace chronon3d::text {

// ═══════════════════════════════════════════════════════════════════════════
// ParseResult — structured result from all parse_* functions
// ═══════════════════════════════════════════════════════════════════════════

struct ParseResult {
    presets::text::SubtitleTrack  track;
    std::string                   error;   // empty on success
    bool                          ok() const noexcept { return error.empty(); }
};

// ═══════════════════════════════════════════════════════════════════════════
// SRT (SubRip Text) parser
//
// Format:
//   1
//   00:00:01,000 --> 00:00:04,000
//   Hello world
//
//   2
//   00:00:05,000 --> 00:00:08,000
//   Second subtitle
//
// Word timing is NOT part of the SRT spec.  If the SRT contains inline
// word-timing markers (<t=...>), they are parsed; otherwise words are
// left empty and the entire cue text is used.
// ═══════════════════════════════════════════════════════════════════════════

/// Parse SRT content into a SubtitleTrack.
/// @param content  The raw SRT file content (UTF-8).
/// @param fps      Frames per second for timecode → Frame conversion.  Default 30.
/// @return ParseResult with track and optional error string.
[[nodiscard]] ParseResult parse_srt(std::string_view content, int fps = 30);

// ═══════════════════════════════════════════════════════════════════════════
// WebVTT parser
//
// Format:
//   WEBVTT
//
//   00:00:01.000 --> 00:00:04.000
//   Hello world
//
//   00:00:05.000 --> 00:00:08.000
//   Second subtitle
//
// Supports:
//   - WEBVTT header (optional but common)
//   - NOTE blocks (skipped)
//   - STYLE blocks (skipped — no CSS parsing yet)
//   - Timestamp with optional hours (HH:MM:SS.mmm or MM:SS.mmm)
//   - Cue identifiers (optional, before timestamp)
//   - <c.class> markup in cue text (stripped for plain text; TODO CSS class)
// ═══════════════════════════════════════════════════════════════════════════

/// Parse WebVTT content into a SubtitleTrack.
/// @param content  The raw WebVTT file content (UTF-8).
/// @param fps      Frames per second for timecode → Frame conversion.  Default 30.
/// @return ParseResult with track and optional error string.
[[nodiscard]] ParseResult parse_vtt(std::string_view content, int fps = 30);

// ═══════════════════════════════════════════════════════════════════════════
// JSON word-timing parser
//
// Format:
//   {
//     "cues": [
//       {
//         "start": 0.0,        // seconds
//         "end": 4.0,
//         "text": "Hello world",
//         "words": [
//           {"word": "Hello", "start": 0.0, "end": 1.5},
//           {"word": "world", "start": 1.5, "end": 4.0}
//         ]
//       }
//     ]
//   }
//
// All times in seconds (float).  Converted to Frame internally.
// ═══════════════════════════════════════════════════════════════════════════

/// Parse JSON word-timing content into a SubtitleTrack.
/// @param content  The raw JSON content (UTF-8).
/// @param fps      Frames per second for seconds → Frame conversion.  Default 30.
/// @return ParseResult with track and optional error string.
[[nodiscard]] ParseResult parse_json_word_timing(std::string_view content, int fps = 30);

} // namespace chronon3d::text
