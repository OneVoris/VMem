#include <voris/mem/system_resource.hpp>

#include <voris/mem/checked_math.hpp>
#include <voris/mem/detail/accounting.hpp>

#include <cstdlib>
#include <mutex>
#include <new>
#include <utility>

#if defined(_MSC_VER)
#    include <malloc.h>
#endif

namespace voris::mem {

namespace {

void* allocate_aligned(std::size_t size, std::size_t alignment) noexcept {
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#else
    if (alignment <= alignof(std::max_align_t)) {
        return std::malloc(size);
    }

    void* pointer{};
    if (posix_memalign(&pointer, alignment, size) != 0) {
        return nullptr;
    }
    return pointer;
#endif
}

void deallocate_aligned(void* pointer) noexcept {
#if defined(_MSC_VER)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

} // namespace

std::expected<allocation, errc>
system_resource::allocate(const allocation_request& request) noexcept {
    if (!is_power_of_two(request.alignment)) {
        failed_allocations_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(errc::invalid_alignment);
    }

    if (request.size == 0) {
        return allocation{nullptr, 0, request.alignment};
    }

    const auto checked_size = align_up(request.size, request.alignment);
    if (!checked_size) {
        failed_allocations_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(checked_size.error());
    }

    const auto effective_alignment =
        request.alignment < alignof(void*) ? alignof(void*) : request.alignment;
    void* pointer = allocate_aligned(request.size, effective_alignment);
    if (pointer == nullptr) {
        failed_allocations_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(errc::out_of_memory);
    }

    try {
        std::lock_guard lock{active_allocations_mutex_};
        auto [_, inserted] =
            active_allocations_.emplace(pointer, allocation{pointer, request.size, request.alignment});
        if (!inserted) {
            deallocate_aligned(pointer);
            failed_allocations_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(errc::out_of_memory);
        }
    } catch (const std::bad_alloc&) {
        deallocate_aligned(pointer);
        failed_allocations_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(errc::out_of_memory);
    }

    auto accounted = detail::checked_atomic_add(active_bytes_, request.size);
    if (!accounted) {
        std::lock_guard lock{active_allocations_mutex_};
        active_allocations_.erase(pointer);
        deallocate_aligned(pointer);
        failed_allocations_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(accounted.error());
    }

    auto active_count = detail::checked_atomic_add(active_allocation_count_, 1);
    if (!active_count) {
        static_cast<void>(detail::checked_atomic_sub(active_bytes_, request.size));
        std::lock_guard lock{active_allocations_mutex_};
        active_allocations_.erase(pointer);
        deallocate_aligned(pointer);
        failed_allocations_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(active_count.error());
    }

    auto total_count = detail::checked_atomic_add(total_allocations_, 1);
    if (!total_count) {
        static_cast<void>(detail::checked_atomic_sub(active_allocation_count_, 1));
        static_cast<void>(detail::checked_atomic_sub(active_bytes_, request.size));
        std::lock_guard lock{active_allocations_mutex_};
        active_allocations_.erase(pointer);
        deallocate_aligned(pointer);
        failed_allocations_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(total_count.error());
    }
    return allocation{pointer, request.size, request.alignment};
}

std::expected<void, errc> system_resource::deallocate(allocation block) noexcept {
    if (detail::is_empty_allocation(block)) {
        return {};
    }
    if (detail::has_invalid_non_empty_shape(block)) {
        return std::unexpected(errc::wrong_owner);
    }

    {
        std::lock_guard lock{active_allocations_mutex_};
        auto found = active_allocations_.find(block.data);
        if (found == active_allocations_.end()) {
            return std::unexpected(errc::wrong_owner);
        }
        if (found->second.size != block.size || found->second.alignment != block.alignment) {
            return std::unexpected(errc::wrong_owner);
        }

        auto active = detail::checked_atomic_sub(active_bytes_, block.size);
        if (!active) {
            return std::unexpected(active.error());
        }

        auto allocations = detail::checked_atomic_sub(active_allocation_count_, 1);
        if (!allocations) {
            static_cast<void>(detail::checked_atomic_add(active_bytes_, block.size));
            return std::unexpected(allocations.error());
        }

        active_allocations_.erase(found);
    }

    deallocate_aligned(block.data);
    static_cast<void>(detail::checked_atomic_add(total_deallocations_, 1));
    return {};
}

resource_traits system_resource::traits() const noexcept {
    return resource_traits{
        .name = "system_resource",
        .ownership = resource_ownership::caller_owned,
        .thread_safety = resource_thread_safety::thread_safe,
        .supports_remote_deallocate = true,
    };
}

usage_snapshot system_resource::usage() const noexcept {
    const auto active_bytes = active_bytes_.load(std::memory_order_relaxed);
    return usage_snapshot{
        .active_bytes = active_bytes,
        .reserved_bytes = active_bytes,
        .active_allocations = active_allocation_count_.load(std::memory_order_relaxed),
        .total_allocations = total_allocations_.load(std::memory_order_relaxed),
        .total_deallocations = total_deallocations_.load(std::memory_order_relaxed),
        .failed_allocations = failed_allocations_.load(std::memory_order_relaxed),
    };
}

} // namespace voris::mem
