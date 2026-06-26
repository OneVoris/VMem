#pragma once

#include <voris/mem/checked_math.hpp>
#include <voris/mem/error.hpp>

#include <cstddef>
#include <expected>
#include <span>

namespace voris::mem
{

struct const_buffer
{
    const std::byte *data{};
    std::size_t size{};

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return size == 0U;
    }

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return data != nullptr || size == 0U;
    }

    [[nodiscard]] constexpr const std::byte *begin() const noexcept
    {
        return data;
    }

    [[nodiscard]] constexpr const std::byte *end() const noexcept
    {
        return data == nullptr ? nullptr : data + size;
    }

    [[nodiscard]] constexpr std::span<const std::byte> span() const noexcept
    {
        return {data, size};
    }

    [[nodiscard]] constexpr std::expected<const_buffer, errc> slice(std::size_t offset,
                                                                    std::size_t count) const noexcept
    {
        if (!valid())
        {
            return std::unexpected(errc::wrong_owner);
        }
        auto end_offset = checked_add(offset, count);
        if (!end_offset || *end_offset > size)
        {
            return std::unexpected(errc::size_overflow);
        }
        return const_buffer{data == nullptr ? nullptr : data + offset, count};
    }
};

struct mutable_buffer
{
    std::byte *data{};
    std::size_t size{};

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return size == 0U;
    }

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return data != nullptr || size == 0U;
    }

    [[nodiscard]] constexpr std::byte *begin() const noexcept
    {
        return data;
    }

    [[nodiscard]] constexpr std::byte *end() const noexcept
    {
        return data == nullptr ? nullptr : data + size;
    }

    [[nodiscard]] constexpr std::span<std::byte> span() const noexcept
    {
        return {data, size};
    }

    [[nodiscard]] constexpr operator const_buffer() const noexcept
    {
        return const_buffer{data, size};
    }

    [[nodiscard]] constexpr std::expected<mutable_buffer, errc> slice(std::size_t offset,
                                                                      std::size_t count) const noexcept
    {
        if (!valid())
        {
            return std::unexpected(errc::wrong_owner);
        }
        auto end_offset = checked_add(offset, count);
        if (!end_offset || *end_offset > size)
        {
            return std::unexpected(errc::size_overflow);
        }
        return mutable_buffer{data == nullptr ? nullptr : data + offset, count};
    }
};

} // namespace voris::mem
