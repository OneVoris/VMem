#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/detail/accounting.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <atomic>
#include <expected>

namespace voris::mem
{

class counting_resource
{
  public:
    explicit counting_resource(resource_ref upstream) noexcept : upstream_(upstream)
    {
    }

    [[nodiscard]] std::expected<allocation, errc> allocate(const allocation_request &request) noexcept
    {
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
            .name = "counting_resource",
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

    resource_ref upstream_;
    std::atomic<std::size_t> active_bytes_{};
    std::atomic<std::size_t> reserved_bytes_{};
    std::atomic<std::size_t> active_allocations_{};
    std::atomic<std::size_t> total_allocations_{};
    std::atomic<std::size_t> total_deallocations_{};
    std::atomic<std::size_t> failed_allocations_{};
};

} // namespace voris::mem
