#include "fingerprint_detail.hpp"

#include <cmath>
#include <cstring>
#include <xxhash.h>

namespace chronon3d::render_plan::fingerprint_detail {

CanonicalPlanWriter::CanonicalPlanWriter(std::pmr::memory_resource* resource)
    : bytes_(resource) {
    bytes_.reserve(64U * 1024U);
}
void CanonicalPlanWriter::add_bool(bool value) { add_u8(value ? 1U : 0U); }
void CanonicalPlanWriter::add_frame(Frame value) { add_i64(value.integral()); }
void CanonicalPlanWriter::add_float(float value) {
    if (value == 0.0F) value = 0.0F;
    std::uint32_t bits = 0;
    if (std::isnan(value)) bits = 0x7fc00000U;
    else { static_assert(sizeof(bits) == sizeof(value)); std::memcpy(&bits, &value, sizeof(bits)); }
    add_u32(bits);
}
void CanonicalPlanWriter::add_double(double value) {
    if (value == 0.0) value = 0.0;
    std::uint64_t bits = 0;
    if (std::isnan(value)) bits = 0x7ff8000000000000ULL;
    else { static_assert(sizeof(bits) == sizeof(value)); std::memcpy(&bits, &value, sizeof(bits)); }
    add_u64(bits);
}
void CanonicalPlanWriter::add_i64(std::int64_t value) { add_u64(static_cast<std::uint64_t>(value)); }
void CanonicalPlanWriter::add_uint64(std::uint64_t value) { add_u64(value); }
void CanonicalPlanWriter::add_uint32(std::uint32_t value) { add_u32(value); }
void CanonicalPlanWriter::add_size(std::size_t value) { add_u64(static_cast<std::uint64_t>(value)); }
void CanonicalPlanWriter::add_string(std::string_view value) {
    add_size(value.size());
    if (!value.empty()) {
        const auto* first = reinterpret_cast<const std::byte*>(value.data());
        bytes_.insert(bytes_.end(), first, first + value.size());
    }
}
std::uint64_t CanonicalPlanWriter::finish() const noexcept {
    return XXH3_64bits(bytes_.data(), bytes_.size());
}
void CanonicalPlanWriter::add_u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
void CanonicalPlanWriter::add_u32(std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        add_u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
}
void CanonicalPlanWriter::add_u64(std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8)
        add_u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
}

} // namespace chronon3d::render_plan::fingerprint_detail
