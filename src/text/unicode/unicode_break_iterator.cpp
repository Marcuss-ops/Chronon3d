#include "../boundary_resolver/text_boundary_resolver.hpp"
#include "utf8_decoder.hpp"

#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/stringpiece.h>
#include <unicode/unistr.h>
#include <unicode/ubrk.h>
#include <unicode/utypes.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::text::boundary {
namespace {

[[nodiscard]] std::vector<std::size_t> utf16_to_utf8_offsets(
    std::string_view utf8,
    const icu::UnicodeString& utf16
) {
    const auto utf16_length = utf16.length();
    std::vector<std::size_t> offsets(
        static_cast<std::size_t>(utf16_length) + 1,
        utf8.size());

    std::size_t byte_offset = 0;
    int32_t utf16_offset = 0;
    offsets[0] = 0;

    while (byte_offset < utf8.size() && utf16_offset < utf16_length) {
        const std::size_t byte_start = byte_offset;
        const char32_t cp = unicode::decode_codepoint(utf8, byte_offset);
        const int32_t utf16_units = cp > 0xFFFF ? 2 : 1;

        offsets[static_cast<std::size_t>(utf16_offset)] = byte_start;
        if (utf16_units == 2 && utf16_offset + 1 < utf16_length) {
            offsets[static_cast<std::size_t>(utf16_offset + 1)] = byte_start;
        }
        utf16_offset += utf16_units;
        if (utf16_offset <= utf16_length) {
            offsets[static_cast<std::size_t>(utf16_offset)] = byte_offset;
        }
    }

    return offsets;
}

void append_iterator_boundaries(
    icu::BreakIterator& iterator,
    const std::vector<std::size_t>& utf16_to_utf8,
    std::vector<std::size_t>& output
) {
    for (int32_t boundary = iterator.first();
         boundary != icu::BreakIterator::DONE;
         boundary = iterator.next()) {
        if (boundary < 0 ||
            static_cast<std::size_t>(boundary) >= utf16_to_utf8.size()) {
            continue;
        }
        output.push_back(utf16_to_utf8[static_cast<std::size_t>(boundary)]);
    }

    std::sort(output.begin(), output.end());
    output.erase(std::unique(output.begin(), output.end()), output.end());
}

void append_word_boundaries_and_segments(
    icu::BreakIterator& iterator,
    const std::vector<std::size_t>& utf16_to_utf8,
    std::vector<std::size_t>& boundaries,
    std::vector<WordSegment>& segments
) {
    int32_t start = iterator.first();
    if (start == icu::BreakIterator::DONE) return;
    if (start >= 0 && static_cast<std::size_t>(start) < utf16_to_utf8.size()) {
        boundaries.push_back(utf16_to_utf8[static_cast<std::size_t>(start)]);
    }

    for (int32_t end = iterator.next();
         end != icu::BreakIterator::DONE;
         end = iterator.next()) {
        if (start >= 0 && end >= start &&
            static_cast<std::size_t>(end) < utf16_to_utf8.size()) {
            segments.push_back(WordSegment{
                utf16_to_utf8[static_cast<std::size_t>(start)],
                utf16_to_utf8[static_cast<std::size_t>(end)],
                iterator.getRuleStatus() >= UBRK_WORD_NONE_LIMIT});
            boundaries.push_back(utf16_to_utf8[static_cast<std::size_t>(end)]);
        }
        start = end;
    }

    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
}

[[nodiscard]] icu::Locale make_locale(TextBoundaryOptions options) {
    if (options.language.empty()) return icu::Locale::getRoot();
    const std::string language(options.language);
    return icu::Locale::createFromName(language.c_str());
}

} // namespace

BoundaryMap IcuBoundaryResolver::resolve(
    std::string_view utf8,
    const TextBoundaryOptions& options
) const {
    BoundaryMap result;

    icu::UnicodeString utf16 = icu::UnicodeString::fromUTF8(
        icu::StringPiece(utf8.data(), static_cast<int32_t>(utf8.size())));
    const auto utf16_to_utf8 = utf16_to_utf8_offsets(utf8, utf16);
    const auto locale = make_locale(options);

    auto collect = [&](std::unique_ptr<icu::BreakIterator> iterator,
                       std::vector<std::size_t>& output) {
        if (!iterator) return;
        iterator->setText(utf16);
        append_iterator_boundaries(*iterator, utf16_to_utf8, output);
    };

    UErrorCode status = U_ZERO_ERROR;
    collect(std::unique_ptr<icu::BreakIterator>(
                icu::BreakIterator::createCharacterInstance(locale, status)),
            result.grapheme_boundaries);
    if (U_FAILURE(status)) {
        status = U_ZERO_ERROR;
        result.grapheme_boundaries.clear();
    }

    status = U_ZERO_ERROR;
    auto word_iterator = std::unique_ptr<icu::BreakIterator>(
        icu::BreakIterator::createWordInstance(locale, status));
    if (word_iterator) {
        word_iterator->setText(utf16);
        append_word_boundaries_and_segments(
            *word_iterator,
            utf16_to_utf8,
            result.word_boundaries,
            result.word_segments);
    }
    if (U_FAILURE(status)) {
        status = U_ZERO_ERROR;
        result.word_boundaries.clear();
        result.word_segments.clear();
    }

    status = U_ZERO_ERROR;
    collect(std::unique_ptr<icu::BreakIterator>(
                icu::BreakIterator::createLineInstance(locale, status)),
            result.line_boundaries);
    if (U_FAILURE(status)) result.line_boundaries.clear();

    for (auto* boundaries : {&result.grapheme_boundaries,
                             &result.word_boundaries,
                             &result.line_boundaries}) {
        boundaries->push_back(0);
        boundaries->push_back(utf8.size());
        std::sort(boundaries->begin(), boundaries->end());
        boundaries->erase(
            std::unique(boundaries->begin(), boundaries->end()),
            boundaries->end());
    }

    return result;
}

} // namespace chronon3d::text::boundary
