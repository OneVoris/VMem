#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/error.hpp>

#include <atomic>
#include <cstddef>
#include <expected>

namespace voris::mem::detail
{

[[nodiscard]] constexpr bool is_empty_allocation(allocation block) noexcept
{
    return block.data == nullptr && block.size == 0;
}

[[nodiscard]] constexpr bool has_invalid_non_empty_shape(allocation block) noexcept
{
    return (block.data == nullptr) != (block.size == 0);
}

[[nodiscard]] inline std::expected<std::size_t, errc> checked_atomic_add(std::atomic<std::size_t> &counter,
                                                                         std::size_t delta) noexcept
{
    auto current = counter.load(std::memory_order_relaxed);
    for (;;)
    {
        auto next = checked_add(current, delta);
        if (!next)
        {
            return std::unexpected(next.error());
        }
        if (counter.compare_exchange_weak(current, *next, std::memory_order_relaxed, std::memory_order_relaxed))
        {
            return *next;
        }
    }
}

[[nodiscard]] inline std::expected<std::size_t, errc> checked_atomic_sub(std::atomic<std::size_t> &counter,
                                                                         std::size_t delta) noexcept
{
    auto current = counter.load(std::memory_order_relaxed);
    for (;;)
    {
        auto next = checked_sub(current, delta);
        if (!next)
        {
            return std::unexpected(next.error());
        }
        if (counter.compare_exchange_weak(current, *next, std::memory_order_relaxed, std::memory_order_relaxed))
        {
            return *next;
        }
    }
}

} // namespace voris::mem::detail
