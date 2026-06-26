#pragma once

#include <voris/mem/error.hpp>

#include <cstddef>
#include <expected>
#include <limits>

namespace voris::mem
{

[[nodiscard]] constexpr bool is_power_of_two(std::size_t value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] constexpr std::expected<std::size_t, errc> checked_add(std::size_t lhs, std::size_t rhs) noexcept
{
    if (std::numeric_limits<std::size_t>::max() - lhs < rhs)
    {
        return std::unexpected(errc::size_overflow);
    }
    return lhs + rhs;
}

[[nodiscard]] constexpr std::expected<std::size_t, errc> checked_mul(std::size_t lhs, std::size_t rhs) noexcept
{
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs)
    {
        return std::unexpected(errc::size_overflow);
    }
    return lhs * rhs;
}

[[nodiscard]] constexpr std::expected<std::size_t, errc> checked_sub(std::size_t lhs, std::size_t rhs) noexcept
{
    if (lhs < rhs)
    {
        return std::unexpected(errc::wrong_owner);
    }
    return lhs - rhs;
}

[[nodiscard]] constexpr std::expected<std::size_t, errc> align_up(std::size_t value, std::size_t alignment) noexcept
{
    if (!is_power_of_two(alignment))
    {
        return std::unexpected(errc::invalid_alignment);
    }

    const std::size_t mask = alignment - 1;
    auto padded = checked_add(value, mask);
    if (!padded)
    {
        return std::unexpected(padded.error());
    }
    return *padded & ~mask;
}

} // namespace voris::mem
