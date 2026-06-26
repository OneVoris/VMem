#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/detail/accounting.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <atomic>
#include <expected>
#include <limits>
#include <optional>

namespace voris::mem
{

struct fault_injection_options
{
    std::size_t fail_on_allocation_call{};
    std::size_t fail_after_requested_bytes{std::numeric_limits<std::size_t>::max()};
    std::optional<memory_tag> fail_tag{};
};

class fault_injection_resource
{
  public:
    explicit fault_injection_resource(resource_ref upstream, fault_injection_options options = {}) noexcept
        : upstream_(upstream), options_(options)
    {
    }

    [[nodiscard]] std::expected<allocation, errc> allocate(const allocation_request &request) noexcept
    {
        const auto call = allocation_calls_.fetch_add(1, std::memory_order_relaxed) + 1;
        auto failure = should_fail(call, request);
        if (!failure)
        {
            failed_allocations_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(failure.error());
        }
        if (*failure)
        {
            failed_allocations_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(errc::out_of_memory);
        }

        auto accounted = account_bytes(request.size);
        if (!accounted)
        {
            failed_allocations_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(accounted.error());
        }

        auto block = upstream_.allocate(request);
        if (!block)
        {
            unaccount_bytes(request.size);
            failed_allocations_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(block.error());
        }

        if (block->size != 0 || block->data != nullptr)
        {
            active_allocations_.fetch_add(1, std::memory_order_relaxed);
            total_allocations_.fetch_add(1, std::memory_order_relaxed);
        }
        return block;
    }

    [[nodiscard]] std::expected<void, errc> deallocate(allocation block) noexcept
    {
        if (detail::has_invalid_non_empty_shape(block))
        {
            return std::unexpected(errc::wrong_owner);
        }

        auto unaccounted = unaccount_allocation(block);
        if (!unaccounted)
        {
            return std::unexpected(unaccounted.error());
        }

        auto released = upstream_.deallocate(block);
        if (!released)
        {
            restore_allocation(block);
            return std::unexpected(released.error());
        }

        if (block.size != 0 || block.data != nullptr)
        {
            total_deallocations_.fetch_add(1, std::memory_order_relaxed);
        }
        return {};
    }

    [[nodiscard]] resource_traits traits() const noexcept
    {
        auto base = upstream_.traits();
        return resource_traits{
            .name = "fault_injection_resource",
            .ownership = resource_ownership::caller_owned,
            .thread_safety = base.thread_safety,
            .supports_remote_deallocate = base.supports_remote_deallocate,
        };
    }

    [[nodiscard]] usage_snapshot usage() const noexcept
    {
        return usage_snapshot{
            .active_bytes = active_bytes_.load(std::memory_order_relaxed),
            .reserved_bytes = reserved_bytes_.load(std::memory_order_relaxed),
            .active_allocations = active_allocations_.load(std::memory_order_relaxed),
            .total_allocations = total_allocations_.load(std::memory_order_relaxed),
            .total_deallocations = total_deallocations_.load(std::memory_order_relaxed),
            .failed_allocations = failed_allocations_.load(std::memory_order_relaxed),
        };
    }

  private:
    [[nodiscard]] std::expected<void, errc> account_bytes(std::size_t size) noexcept
    {
        if (size == 0)
        {
            return {};
        }

        auto active = detail::checked_atomic_add(active_bytes_, size);
        if (!active)
        {
            return std::unexpected(active.error());
        }

        auto reserved = detail::checked_atomic_add(reserved_bytes_, size);
        if (!reserved)
        {
            static_cast<void>(detail::checked_atomic_sub(active_bytes_, size));
            return std::unexpected(reserved.error());
        }
        return {};
    }

    void unaccount_bytes(std::size_t size) noexcept
    {
        if (size == 0)
        {
            return;
        }
        static_cast<void>(detail::checked_atomic_sub(reserved_bytes_, size));
        static_cast<void>(detail::checked_atomic_sub(active_bytes_, size));
    }

    [[nodiscard]] std::expected<void, errc> unaccount_allocation(allocation block) noexcept
    {
        if (detail::is_empty_allocation(block))
        {
            return {};
        }

        auto active = detail::checked_atomic_sub(active_bytes_, block.size);
        if (!active)
        {
            return std::unexpected(active.error());
        }

        auto reserved = detail::checked_atomic_sub(reserved_bytes_, block.size);
        if (!reserved)
        {
            static_cast<void>(detail::checked_atomic_add(active_bytes_, block.size));
            return std::unexpected(reserved.error());
        }

        auto allocations = detail::checked_atomic_sub(active_allocations_, 1);
        if (!allocations)
        {
            static_cast<void>(detail::checked_atomic_add(reserved_bytes_, block.size));
            static_cast<void>(detail::checked_atomic_add(active_bytes_, block.size));
            return std::unexpected(allocations.error());
        }
        return {};
    }

    void restore_allocation(allocation block) noexcept
    {
        if (detail::is_empty_allocation(block))
        {
            return;
        }
        static_cast<void>(detail::checked_atomic_add(active_bytes_, block.size));
        static_cast<void>(detail::checked_atomic_add(reserved_bytes_, block.size));
        static_cast<void>(detail::checked_atomic_add(active_allocations_, 1));
    }

    [[nodiscard]] std::expected<bool, errc> should_fail(std::size_t call, const allocation_request &request) noexcept
    {
        if (options_.fail_on_allocation_call != 0 && call == options_.fail_on_allocation_call)
        {
            return true;
        }

        auto prior_bytes = requested_bytes_.load(std::memory_order_relaxed);
        for (;;)
        {
            auto next_bytes = checked_add(prior_bytes, request.size);
            if (!next_bytes)
            {
                requested_bytes_.store(std::numeric_limits<std::size_t>::max(), std::memory_order_relaxed);
                return std::unexpected(next_bytes.error());
            }
            if (requested_bytes_.compare_exchange_weak(prior_bytes, *next_bytes, std::memory_order_relaxed,
                                                       std::memory_order_relaxed))
            {
                if (options_.fail_after_requested_bytes != std::numeric_limits<std::size_t>::max())
                {
                    return *next_bytes > options_.fail_after_requested_bytes;
                }
                break;
            }
        }

        return options_.fail_tag.has_value() && request.tag == *options_.fail_tag;
    }

    resource_ref upstream_;
    fault_injection_options options_{};
    std::atomic<std::size_t> allocation_calls_{};
    std::atomic<std::size_t> requested_bytes_{};
    std::atomic<std::size_t> active_bytes_{};
    std::atomic<std::size_t> reserved_bytes_{};
    std::atomic<std::size_t> active_allocations_{};
    std::atomic<std::size_t> total_allocations_{};
    std::atomic<std::size_t> total_deallocations_{};
    std::atomic<std::size_t> failed_allocations_{};
};

} // namespace voris::mem
