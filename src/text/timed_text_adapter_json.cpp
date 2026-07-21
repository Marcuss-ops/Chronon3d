#include <chronon3d/text/timed_text_document.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string_view>

namespace chronon3d {
namespace {

// ── Minimal JSON parser sufficient for the subtitle schema ───────────────
// We avoid nlohmann/json to keep the core text module dependency-free.
// Supports: objects, strings, numbers, arrays, null, booleans.

class JsonCursor {
    std::string_view raw_;
    size_t pos_{0};
public:
    explicit JsonCursor(std::string_view raw) : raw_(raw) {}

    [[nodiscard]] bool done() const { return pos_ >= raw_.size(); }

    void skip_whitespace() {
        while (pos_ < raw_.size() && (raw_[pos_] == ' ' || raw_[pos_] == '\t' ||
               raw_[pos_] == '\n' || raw_[pos_] == '\r')) ++pos_;
    }

    [[nodiscard]] char peek() const {
        if (pos_ >= raw_.size()) return '\0';
        return raw_[pos_];
    }

    char next() {
        if (pos_ >= raw_.size()) return '\0';
        return raw_[pos_++];
    }

    void expect(char c) {
        skip_whitespace();
        if (peek() != c) return;
        ++pos_;
    }

    // Parse a JSON string (quoted). Returns unescaped content.
    // Supports basic escapes: \\, \", \n, \t, \r.
    std::string parse_string() {
        skip_whitespace();
        if (peek() != '"') return {};
        ++pos_; // skip opening quote
        std::string result;
        while (pos_ < raw_.size() && raw_[pos_] != '"') {
            if (raw_[pos_] == '\\') {
                ++pos_;
                if (pos_ >= raw_.size()) break;
                switch (raw_[pos_]) {
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/';  break;
                    case 'n':  result += '\n'; break;
                    case 't':  result += '\t'; break;
                    case 'r':  result += '\r'; break;
                    default:   result += raw_[pos_]; break;
                }
            } else {
                result += raw_[pos_];
            }
            ++pos_;
        }
        if (peek() == '"') ++pos_; // skip closing quote
        return result;
    }

    // Parse a JSON number (integer or float).
    f32 parse_number() {
        skip_whitespace();
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < raw_.size() && std::isdigit(static_cast<unsigned char>(raw_[pos_]))) ++pos_;
        if (pos_ < raw_.size() && raw_[pos_] == '.') {
            ++pos_;
            while (pos_ < raw_.size() && std::isdigit(static_cast<unsigned char>(raw_[pos_]))) ++pos_;
        }
        if (pos_ < raw_.size() && (raw_[pos_] == 'e' || raw_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < raw_.size() && (raw_[pos_] == '+' || raw_[pos_] == '-')) ++pos_;
            while (pos_ < raw_.size() && std::isdigit(static_cast<unsigned char>(raw_[pos_]))) ++pos_;
        }
        std::string num_str(raw_.substr(start, pos_ - start));
        return std::strtof(num_str.c_str(), nullptr);
    }

    // Parse a JSON integer (for font_weight etc.)
    int parse_int() {
        skip_whitespace();
        size_t start = pos_;
        while (pos_ < raw_.size() && std::isdigit(static_cast<unsigned char>(raw_[pos_]))) ++pos_;
        return std::atoi(std::string(raw_.substr(start, pos_ - start)).c_str());
    }

    // Skip past any JSON value (used for unknown/unwanted fields)
    void skip_value() {
        skip_whitespace();
        if (done()) return;
        char c = raw_[pos_];
        if (c == '"') {
            parse_string();
        } else if (c == '{') {
            next();
            int depth = 1;
            while (pos_ < raw_.size() && depth > 0) {
                c = raw_[pos_];
                if (c == '{') ++depth;
                else if (c == '}') --depth;
                else if (c == '"') {
                    next();
                    while (pos_ < raw_.size() && raw_[pos_] != '"') {
                        if (raw_[pos_] == '\\') { ++pos_; if (pos_ >= raw_.size()) break; }
                        ++pos_;
                    }
                }
                ++pos_;
            }
        } else if (c == '[') {
            next();
            int depth = 1;
            while (pos_ < raw_.size() && depth > 0) {
                if (raw_[pos_] == '[') ++depth;
                else if (raw_[pos_] == ']') --depth;
                else if (raw_[pos_] == '"') {
                    ++pos_;
                    while (pos_ < raw_.size() && raw_[pos_] != '"') {
                        if (raw_[pos_] == '\\') { ++pos_; if (pos_ >= raw_.size()) break; }
                        ++pos_;
                    }
                }
                ++pos_;
            }
        } else if (c == 't' || c == 'f' || c == 'n') {
            // true / false / null — skip 4-5 chars
            while (pos_ < raw_.size() && !std::isspace(static_cast<unsigned char>(raw_[pos_])) &&
                   raw_[pos_] != ',' && raw_[pos_] != '}' && raw_[pos_] != ']') ++pos_;
        } else if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            parse_number();
        } else {
            ++pos_; // unknown
        }
    }
};

std::optional<std::string> try_parse_string(JsonCursor& j, const char* key) {
    j.skip_whitespace();
    auto k = j.parse_string();
    if (k == key) {
        j.expect(':');
        return j.parse_string();
    }
    j.skip_value();
    return std::nullopt;
}

std::optional<f32> try_parse_float(JsonCursor& j, const char* key) {
    j.skip_whitespace();
    auto k = j.parse_string();
    if (k == key) {
        j.expect(':');
        return j.parse_number();
    }
    j.skip_value();
    return std::nullopt;
}

std::optional<int> try_parse_int(JsonCursor& j, const char* key) {
    j.skip_whitespace();
    auto k = j.parse_string();
    if (k == key) {
        j.expect(':');
        return j.parse_int();
    }
    j.skip_value();
    return std::nullopt;
}

} // namespace

TimedTextDocument timed_text_from_json(const std::string& raw) {
    TimedTextDocument doc;
    doc.source_format = "json";

    if (raw.empty()) return doc;

    JsonCursor j(raw);

    // Expect root object
    j.skip_whitespace();
    if (j.peek() != '{') return doc;
    j.next();

    j.skip_whitespace();

    // Parse top-level fields
    while (!j.done() && j.peek() != '}') {
        j.skip_whitespace();
        if (j.peek() == '}') break;

        auto key = j.parse_string();
        if (key.empty()) break;

        j.expect(':');
        j.skip_whitespace();

        if (key == "language") {
            doc.language = j.parse_string();
        } else if (key == "title") {
            doc.title = j.parse_string();
        } else if (key == "cues") {
            // Parse array of cues
            if (j.peek() != '[') { j.skip_value(); continue; }
            j.next(); // skip '['

            j.skip_whitespace();
            while (!j.done() && j.peek() != ']') {
                if (j.peek() == '{') {
                    j.next(); // skip '{'
                    TimedCue cue;
                    // TICKET-WORD-TIMING-QUALITY: tracks whether ANY words
                    // for this cue came from the source `"words"` array.
                    // Drives the Authoritative vs Estimated classification
                    // (vs the auto-fallback path which leaves this false).
                    bool words_from_source = false;

                    j.skip_whitespace();
                    while (!j.done() && j.peek() != '}') {
                        auto cue_key = j.parse_string();
                        if (cue_key.empty()) break;
                        j.expect(':');
                        j.skip_whitespace();

                        if (cue_key == "id") {
                            cue.source_id = j.parse_string();
                        } else if (cue_key == "speaker") {
                            cue.speaker = j.parse_string();
                        } else if (cue_key == "text") {
                            cue.text = j.parse_string();
                        } else if (cue_key == "start") {
                            cue.start_s = j.parse_number();
                        } else if (cue_key == "end") {
                            cue.end_s = j.parse_number();
                        } else if (cue_key == "words") {
                            if (j.peek() == '[') {
                                j.next(); // skip '['
                                j.skip_whitespace();
                                while (!j.done() && j.peek() != ']') {
                                    if (j.peek() == '{') {
                                        j.next();
                                        TimedWord tw;
                                        j.skip_whitespace();
                                        while (!j.done() && j.peek() != '}') {
                                            auto wk = j.parse_string();
                                            if (wk.empty()) break;
                                            j.expect(':');
                                            if (wk == "word") tw.text = j.parse_string();
                                            else if (wk == "start") tw.start_s = j.parse_number();
                                            else if (wk == "end") tw.end_s = j.parse_number();
                                            else if (wk == "id") tw.semantic_id = j.parse_string();
                                            else j.skip_value();
                                            j.skip_whitespace();
                                            if (j.peek() == ',') j.next();
                                        }
                                        // Ensure we have a semantic_id
                                        if (tw.semantic_id.empty()) {
                                            tw.semantic_id = cue.source_id + "-word" + std::to_string(cue.words.size());
                                        }
                                        cue.words.push_back(std::move(tw));
                                        // TICKET-WORD-TIMING-QUALITY: the
                                        // source provided a `words` array —
                                        // even if individual word start/end
                                        // are sparse, this is the
                                        // Authoritative provenance (vs the
                                        // Estimated auto-fallback below).
                                        words_from_source = true;
                                        if (j.peek() == '}') j.next(); // skip '}'
                                    }
                                    j.skip_whitespace();
                                    if (j.peek() == ',') j.next();
                                    j.skip_whitespace();
                                }
                                if (j.peek() == ']') j.next(); // skip ']'
                            } else {
                                j.skip_value();
                            }
                        } else if (cue_key == "style") {
                            if (j.peek() == '{') {
                                j.next(); // skip '{'
                                TimedCueStyle style;
                                j.skip_whitespace();
                                while (!j.done() && j.peek() != '}') {
                                    auto sk = j.parse_string();
                                    if (sk.empty()) break;
                                    j.expect(':');
                                    if (sk == "color") style.color = j.parse_string();
                                    else if (sk == "background") style.background = j.parse_string();
                                    else if (sk == "font_size") style.font_size = j.parse_number();
                                    else if (sk == "font_weight") style.font_weight = static_cast<u32>(j.parse_int());
                                    else j.skip_value();
                                    j.skip_whitespace();
                                    if (j.peek() == ',') j.next();
                                }
                                if (j.peek() == '}') j.next();
                                cue.style = std::move(style);
                            } else {
                                j.skip_value();
                            }
                        } else {
                            j.skip_value();
                        }
                        j.skip_whitespace();
                        if (j.peek() == ',') j.next();
                        j.skip_whitespace();
                    }
                    if (j.peek() == '}') j.next();

                    // Auto-generate word breakdown if no words provided but text exists
                    if (cue.words.empty() && !cue.text.empty() && cue.start_s < cue.end_s) {
                        // Simple space-split
                        size_t i = 0;
                        int word_idx = 0;
                        while (i < cue.text.size()) {
                            while (i < cue.text.size() && (cue.text[i] == ' ' || cue.text[i] == '\t' || cue.text[i] == '\n')) ++i;
                            if (i >= cue.text.size()) break;
                            size_t start = i;
                            while (i < cue.text.size() && cue.text[i] != ' ' && cue.text[i] != '\t' && cue.text[i] != '\n') ++i;
                            std::string word_str = cue.text.substr(start, i - start);
                            auto& words = cue.words;
                            TimedWord tw;
                            tw.text = word_str;
                            f32 dur = cue.end_s - cue.start_s;
                            // We don't have per-word timings, so distribute evenly.
                            // This will be refined once word count is known.
                            tw.start_s = cue.start_s;
                            tw.end_s = cue.end_s;
                            tw.semantic_id = cue.source_id + "-word" + std::to_string(word_idx);
                            words.push_back(tw);
                            ++word_idx;
                        }
                        // Distribute evenly now that we know the count
                        if (!cue.words.empty()) {
                            f32 dur = cue.end_s - cue.start_s;
                            f32 per_word = dur / static_cast<f32>(cue.words.size());
                            for (size_t wi = 0; wi < cue.words.size(); ++wi) {
                                cue.words[wi].start_s = cue.start_s + static_cast<f32>(wi) * per_word;
                                cue.words[wi].end_s = cue.start_s + static_cast<f32>(wi + 1) * per_word;
                            }
                        }
                    }

                    // TICKET-WORD-TIMING-QUALITY: classify per-cue
                    // provenance.  Three-way decision driven by the two
                    // upstream paths (source `words` array OR auto-fallback
                    // uniform-split).  The queue guard below filters the
                    // `None` borderline out for callers.
                    if (words_from_source) {
                        cue.word_timing_quality = WordTimingQuality::Authoritative;
                    } else if (cue.words.empty()) {
                        cue.word_timing_quality = WordTimingQuality::None;
                    } else {
                        cue.word_timing_quality = WordTimingQuality::Estimated;
                    }

                    if (cue.start_s >= 0.0f && cue.end_s > cue.start_s && !cue.text.empty()) {
                        doc.cues.push_back(std::move(cue));
                    }
                }
                j.skip_whitespace();
                if (j.peek() == ',') j.next();
                j.skip_whitespace();
            }
            if (j.peek() == ']') j.next();
        } else {
            j.skip_value(); // unknown top-level key
        }

        j.skip_whitespace();
        if (j.peek() == ',') j.next();
        j.skip_whitespace();
    }

    return doc;
}

} // namespace chronon3d
