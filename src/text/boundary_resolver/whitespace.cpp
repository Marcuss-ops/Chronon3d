// Unicode whitespace policy adapter. ICU owns the Unicode property table;
// Chronon adds U+FEFF because its text-unit contract treats a BOM as a
// whitespace-only unit even though modern Unicode White_Space excludes it.

#include "../unicode/whitespace.hpp"

#include <unicode/uchar.h>

namespace chronon3d {
namespace text {
namespace unicode {

bool is_unicode_whitespace(char32_t cp) noexcept {
    return cp == 0xFEFFu || u_isUWhiteSpace(static_cast<UChar32>(cp));
}

}  // namespace unicode
}  // namespace text
}  // namespace chronon3d
