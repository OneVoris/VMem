#pragma once

#include <voris/mem/buffer_chain.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <type_traits>

namespace voris::mem {

template <typename UInt>
concept unsigned_integer = std::is_integral_v<UInt> && std::is_unsigned_v<UInt>;

template <unsigned_integer UInt>
[[nodiscard]] std::expected<UInt, errc>
peek_uint_be(const buffer_chain& chain, std::size_t offset = 0U) noexcept {
    auto end_offset = checked_add(offset, sizeof(UInt));
    if (!end_offset || *end_offset > chain.size()) {
        return std::unexpected(errc::size_overflow);
    }
    UInt value{};
    std::size_t global{};
    std::size_t copied{};
    for (std::size_t index = 0U; index < chain.segment_count() && copied < sizeof(UInt); ++index) {
        auto segment = chain.segment(index);
        auto segment_end = checked_add(global, segment.size);
        if (!segment_end) {
            return std::unexpected(segment_end.error());
        }
        if (*segment_end <= offset) {
            global = *segment_end;
            continue;
        }
        std::size_t local{};
        if (offset > global) {
            auto checked_local = checked_sub(offset, global);
            if (!checked_local) {
                return std::unexpected(checked_local.error());
            }
            local = *checked_local;
        }
        for (std::size_t i = local; i < segment.size && copied < sizeof(UInt); ++i) {
            value = static_cast<UInt>((value << 8U) | static_cast<UInt>(segment.data[i]));
            ++copied;
        }
        global = *segment_end;
    }
    return value;
}

template <unsigned_integer UInt>
[[nodiscard]] std::expected<UInt, errc>
peek_uint_le(const buffer_chain& chain, std::size_t offset = 0U) noexcept {
    auto end_offset = checked_add(offset, sizeof(UInt));
    if (!end_offset || *end_offset > chain.size()) {
        return std::unexpected(errc::size_overflow);
    }
    UInt value{};
    std::size_t global{};
    std::size_t copied{};
    for (std::size_t index = 0U; index < chain.segment_count() && copied < sizeof(UInt); ++index) {
        auto segment = chain.segment(index);
        auto segment_end = checked_add(global, segment.size);
        if (!segment_end) {
            return std::unexpected(segment_end.error());
        }
        if (*segment_end <= offset) {
            global = *segment_end;
            continue;
        }
        std::size_t local{};
        if (offset > global) {
            auto checked_local = checked_sub(offset, global);
            if (!checked_local) {
                return std::unexpected(checked_local.error());
            }
            local = *checked_local;
        }
        for (std::size_t i = local; i < segment.size && copied < sizeof(UInt); ++i) {
            auto shift = checked_mul(copied, 8U);
            if (!shift) {
                return std::unexpected(shift.error());
            }
            value |= static_cast<UInt>(static_cast<UInt>(segment.data[i]) << *shift);
            ++copied;
        }
        global = *segment_end;
    }
    return value;
}

[[nodiscard]] inline std::optional<std::size_t>
find_delimiter(const buffer_chain& chain,
               std::byte delimiter,
               std::size_t offset = 0U) noexcept {
    if (offset > chain.size()) {
        return std::nullopt;
    }
    std::size_t global{};
    for (std::size_t index = 0U; index < chain.segment_count(); ++index) {
        auto segment = chain.segment(index);
        auto segment_end = checked_add(global, segment.size);
        if (!segment_end) {
            return std::nullopt;
        }
        if (*segment_end <= offset) {
            global = *segment_end;
            continue;
        }
        std::size_t local{};
        if (offset > global) {
            auto checked_local = checked_sub(offset, global);
            if (!checked_local) {
                return std::nullopt;
            }
            local = *checked_local;
        }
        for (std::size_t i = local; i < segment.size; ++i) {
            if (segment.data[i] == delimiter) {
                auto found = checked_add(global, i);
                if (!found) {
                    return std::nullopt;
                }
                return *found;
            }
        }
        global = *segment_end;
    }
    return std::nullopt;
}

[[nodiscard]] inline std::expected<std::size_t, errc>
copy_prefix(const buffer_chain& chain, mutable_buffer destination, std::size_t count) noexcept {
    if (count > 0U && !destination.valid()) {
        return std::unexpected(errc::wrong_owner);
    }
    if (count > chain.size() || count > destination.size) {
        return std::unexpected(errc::size_overflow);
    }
    std::size_t copied{};
    for (std::size_t index = 0U; index < chain.segment_count() && copied < count; ++index) {
        auto segment = chain.segment(index);
        auto needed = checked_sub(count, copied);
        if (!needed) {
            return std::unexpected(needed.error());
        }
        const auto take = segment.size < *needed ? segment.size : *needed;
        for (std::size_t i = 0U; i < take; ++i) {
            auto destination_index = checked_add(copied, i);
            if (!destination_index) {
                return std::unexpected(destination_index.error());
            }
            destination.data[*destination_index] = segment.data[i];
        }
        auto next_copied = checked_add(copied, take);
        if (!next_copied) {
            return std::unexpected(next_copied.error());
        }
        copied = *next_copied;
    }
    return copied;
}

} // namespace voris::mem
