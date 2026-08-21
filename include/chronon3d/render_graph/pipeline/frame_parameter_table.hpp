#pragma once

// Generic, domain-neutral per-frame parameter storage.
//
// The table deliberately stores opaque bytes.  Video, image, text and effect
// processors own the meaning of their parameter payload; the compiled graph
// only owns lifetime, indexing and reuse of the storage.

#include <chronon3d/core/types/frame.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace chronon3d::graph {

struct FrameParameterSlice {
    std::uint32_t offset{0};
    std::uint32_t size{0};
};

class FrameParameterWriter {
public:
    explicit FrameParameterWriter(std::vector<std::byte>& storage) noexcept
        : m_storage(storage) {}

    template <typename T>
    void write(const T& value) {
        const auto* begin = reinterpret_cast<const std::byte*>(&value);
        m_storage.insert(m_storage.end(), begin, begin + sizeof(T));
    }

    void write_bytes(std::span<const std::byte> bytes) {
        m_storage.insert(m_storage.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_storage.size(); }

private:
    std::vector<std::byte>& m_storage;
};

/// Immutable-after-prepare table containing one opaque parameter block per
/// frame.  The table is intentionally independent of any processor family.
class FrameParameterTable {
public:
    void warm_up(std::size_t frame_count, std::size_t bytes_per_frame = 0,
                 Frame first_frame = Frame{0}) {
        m_slices.resize(frame_count);
        m_storage.reserve(frame_count * bytes_per_frame);
        m_frame_count = frame_count;
        m_first_frame = first_frame;
        m_storage.clear();
        for (auto& slice : m_slices) slice = FrameParameterSlice{};
    }

    void clear() noexcept {
        m_storage.clear();
        m_slices.clear();
        m_frame_count = 0;
        m_first_frame = Frame{0};
    }

    template <typename Writer>
    void sample(Frame frame, Writer&& writer) {
        const auto relative = frame.integral() - m_first_frame.integral();
        if (relative < 0 || static_cast<std::size_t>(relative) >= m_frame_count) {
            throw std::out_of_range("FrameParameterTable::sample frame out of range");
        }
        const auto index = static_cast<std::size_t>(relative);
        const auto begin = m_storage.size();
        FrameParameterWriter output(m_storage);
        writer(output);
        m_slices[index] = FrameParameterSlice{
            static_cast<std::uint32_t>(begin),
            static_cast<std::uint32_t>(m_storage.size() - begin)};
    }

    [[nodiscard]] std::span<const std::byte> view(Frame frame) const {
        const auto relative = frame.integral() - m_first_frame.integral();
        if (relative < 0 || static_cast<std::size_t>(relative) >= m_frame_count) {
            throw std::out_of_range("FrameParameterTable::view frame out of range");
        }
        const auto slice = m_slices[static_cast<std::size_t>(relative)];
        return std::span<const std::byte>(m_storage).subspan(slice.offset, slice.size);
    }

    [[nodiscard]] std::span<const std::byte> bytes(
        std::uint32_t offset, std::uint32_t size) const {
        if (static_cast<std::size_t>(offset) + static_cast<std::size_t>(size) >
            m_storage.size()) {
            throw std::out_of_range("FrameParameterTable::bytes range out of bounds");
        }
        return std::span<const std::byte>(m_storage).subspan(offset, size);
    }

    [[nodiscard]] std::size_t frame_count() const noexcept { return m_frame_count; }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return m_storage.size(); }
    [[nodiscard]] std::size_t capacity_bytes() const noexcept { return m_storage.capacity(); }

private:
    std::vector<std::byte> m_storage;
    std::vector<FrameParameterSlice> m_slices;
    std::size_t m_frame_count{0};
    Frame m_first_frame{0};
};

/// Small common sampler facade used by prepare stages.  It gives every
/// processor the same frame-indexed preparation contract while leaving the
/// payload schema to the processor callback.
class FrameParameterSampler {
public:
    using SampleWriter = std::function<void(Frame, FrameParameterWriter&)>;

    static void prepare(FrameParameterTable& table, Frame first, std::size_t count,
                         const SampleWriter& writer) {
        table.warm_up(count, 0, first);
        for (std::size_t i = 0; i < count; ++i) {
            const Frame frame = first + static_cast<std::int64_t>(i);
            table.sample(frame, [&](FrameParameterWriter& output) { writer(frame, output); });
        }
    }
};

} // namespace chronon3d::graph
