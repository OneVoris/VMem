#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/detail/accounting.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <cstddef>
#include <expected>
#include <exception>
#include <memory_resource>
#include <mutex>
#include <new>
#include <unordered_map>

namespace voris::mem {

class pmr_memory_resource final : public std::pmr::memory_resource {
public:
    explicit pmr_memory_resource(resource_ref upstream) noexcept : upstream_(upstream) {}

private:
    [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        auto block = upstream_.allocate(bytes, alignment);
        if (!block) {
            throw std::bad_alloc{};
        }
        return block->data;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
        auto released = upstream_.deallocate(allocation{pointer, bytes, alignment});
        if (!released) {
            std::terminate();
        }
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    resource_ref upstream_;
};

class pmr_resource_adapter {
public:
    explicit pmr_resource_adapter(std::pmr::memory_resource* upstream) noexcept : upstream_(upstream) {}

    [[nodiscard]] std::expected<allocation, errc>
    allocate(const allocation_request& request) noexcept {
        std::lock_guard lock{mutex_};
        if (!is_power_of_two(request.alignment)) {
            failed_allocations_ += 1;
            return std::unexpected(errc::invalid_alignment);
        }
        if (request.size == 0) {
            return allocation{nullptr, 0, request.alignment};
        }

        void* pointer{};
        try {
            pointer = upstream_->allocate(request.size, request.alignment);
            auto active = checked_add(active_bytes_, request.size);
            if (!active) {
                static_cast<void>(safe_upstream_deallocate(pointer, request.size, request.alignment));
                failed_allocations_ += 1;
                return std::unexpected(active.error());
            }
            auto allocations = checked_add(active_allocations_, 1);
            if (!allocations) {
                static_cast<void>(safe_upstream_deallocate(pointer, request.size, request.alignment));
                failed_allocations_ += 1;
                return std::unexpected(allocations.error());
            }
            auto total_allocations = checked_add(total_allocations_, 1);
            if (!total_allocations) {
                static_cast<void>(safe_upstream_deallocate(pointer, request.size, request.alignment));
                failed_allocations_ += 1;
                return std::unexpected(total_allocations.error());
            }
            active_.emplace(pointer, allocation{pointer, request.size, request.alignment});
            active_bytes_ = *active;
            active_allocations_ = *allocations;
            total_allocations_ = *total_allocations;
        } catch (...) {
            if (pointer != nullptr) {
                static_cast<void>(safe_upstream_deallocate(pointer, request.size, request.alignment));
            }
            failed_allocations_ += 1;
            return std::unexpected(errc::out_of_memory);
        }
        return allocation{pointer, request.size, request.alignment};
    }

    [[nodiscard]] std::expected<void, errc> deallocate(allocation block) noexcept {
        std::lock_guard lock{mutex_};
        if (detail::is_empty_allocation(block)) {
            return {};
        }
        if (detail::has_invalid_non_empty_shape(block)) {
            return std::unexpected(errc::wrong_owner);
        }
        auto found = active_.find(block.data);
        if (found == active_.end() || found->second.size != block.size ||
            found->second.alignment != block.alignment) {
            return std::unexpected(errc::wrong_owner);
        }
        auto active = checked_sub(active_bytes_, block.size);
        if (!active) {
            return std::unexpected(active.error());
        }
        auto allocations = checked_sub(active_allocations_, 1);
        if (!allocations) {
            return std::unexpected(allocations.error());
        }
        auto total_deallocations = checked_add(total_deallocations_, 1);
        if (!total_deallocations) {
            return std::unexpected(total_deallocations.error());
        }
        if (!safe_upstream_deallocate(block.data, block.size, block.alignment)) {
            return std::unexpected(errc::wrong_owner);
        }
        active_bytes_ = *active;
        active_allocations_ = *allocations;
        total_deallocations_ = *total_deallocations;
        active_.erase(found);
        return {};
    }

    [[nodiscard]] resource_traits traits() const noexcept {
        return resource_traits{
            .name = "pmr_resource_adapter",
            .ownership = resource_ownership::caller_owned,
            .thread_safety = resource_thread_safety::thread_safe,
            .supports_remote_deallocate = true,
        };
    }

    [[nodiscard]] usage_snapshot usage() const noexcept {
        std::lock_guard lock{mutex_};
        return usage_snapshot{
            .active_bytes = active_bytes_,
            .reserved_bytes = active_bytes_,
            .active_allocations = active_allocations_,
            .total_allocations = total_allocations_,
            .total_deallocations = total_deallocations_,
            .failed_allocations = failed_allocations_,
        };
    }

private:
    [[nodiscard]] bool
    safe_upstream_deallocate(void* pointer, std::size_t size, std::size_t alignment) noexcept {
        try {
            upstream_->deallocate(pointer, size, alignment);
            return true;
        } catch (...) {
            return false;
        }
    }

    std::pmr::memory_resource* upstream_{};
    mutable std::mutex mutex_{};
    std::unordered_map<void*, allocation> active_{};
    std::size_t active_bytes_{};
    std::size_t active_allocations_{};
    std::size_t total_allocations_{};
    std::size_t total_deallocations_{};
    std::size_t failed_allocations_{};
};

} // namespace voris::mem
