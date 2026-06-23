#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/detail/accounting.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace voris::mem {

struct fixed_block_pool_options {
    std::size_t block_size{64};
    std::size_t block_alignment{alignof(std::max_align_t)};
    std::size_t capacity{1024};
};

struct fixed_block_allocation {
    allocation block{};
    std::size_t generation{};
};

class fixed_block_pool {
public:
    explicit fixed_block_pool(resource_ref upstream,
                              fixed_block_pool_options options = {}) noexcept
        : upstream_(upstream), options_(options) {}

    fixed_block_pool(const fixed_block_pool&) = delete;
    fixed_block_pool& operator=(const fixed_block_pool&) = delete;

    ~fixed_block_pool() {
        if (backing_.data != nullptr) {
            static_cast<void>(upstream_.deallocate(backing_));
        }
    }

    [[nodiscard]] std::expected<allocation, errc>
    allocate(const allocation_request& request) noexcept {
        auto block = allocate_block(request);
        if (!block) {
            return std::unexpected(block.error());
        }
        return block->block;
    }

    [[nodiscard]] std::expected<fixed_block_allocation, errc>
    allocate_block(const allocation_request& request) noexcept {
        if (!is_power_of_two(request.alignment) || !is_power_of_two(options_.block_alignment)) {
            failed_allocations_ += 1;
            return std::unexpected(errc::invalid_alignment);
        }
        if (request.size == 0) {
            return fixed_block_allocation{.block = allocation{nullptr, 0, request.alignment},
                                          .generation = 0};
        }
        if (request.size > options_.block_size || request.alignment > options_.block_alignment) {
            failed_allocations_ += 1;
            return std::unexpected(errc::out_of_memory);
        }
        auto ready = ensure_initialized();
        if (!ready) {
            failed_allocations_ += 1;
            return std::unexpected(ready.error());
        }
        if (free_indices_.empty()) {
            failed_allocations_ += 1;
            return std::unexpected(errc::out_of_memory);
        }
        auto active = checked_add(active_bytes_, options_.block_size);
        if (!active) {
            failed_allocations_ += 1;
            return std::unexpected(active.error());
        }
        auto allocations = checked_add(active_allocations_, 1);
        if (!allocations) {
            failed_allocations_ += 1;
            return std::unexpected(allocations.error());
        }
        auto total_allocations = checked_add(total_allocations_, 1);
        if (!total_allocations) {
            failed_allocations_ += 1;
            return std::unexpected(total_allocations.error());
        }

        const auto index = free_indices_.back();
        free_indices_.pop_back();
        auto generation = checked_add(states_[index].generation, 1);
        if (!generation) {
            free_indices_.push_back(index);
            failed_allocations_ += 1;
            return std::unexpected(generation.error());
        }
        states_[index].allocated = true;
        states_[index].generation = *generation;
        active_bytes_ = *active;
        active_allocations_ = *allocations;
        total_allocations_ = *total_allocations;
        auto offset = checked_mul(index, stride_);
        if (!offset) {
            return std::unexpected(offset.error());
        }
        auto address = checked_add(reinterpret_cast<std::uintptr_t>(backing_.data), *offset);
        if (!address) {
            return std::unexpected(address.error());
        }
        return fixed_block_allocation{
            .block = allocation{reinterpret_cast<void*>(*address),
                                options_.block_size,
                                options_.block_alignment},
            .generation = *generation,
        };
    }

    [[nodiscard]] std::expected<void, errc> deallocate(allocation block) noexcept {
        if (detail::is_empty_allocation(block)) {
            return {};
        }
        auto descriptor = descriptor_for_current(block);
        if (!descriptor) {
            return std::unexpected(descriptor.error());
        }
        return deallocate_block(*descriptor);
    }

    [[nodiscard]] std::expected<void, errc>
    deallocate_block(fixed_block_allocation descriptor) noexcept {
        const auto block = descriptor.block;
        if (detail::is_empty_allocation(block)) {
            return {};
        }
        if (detail::has_invalid_non_empty_shape(block) || block.size != options_.block_size ||
            block.alignment != options_.block_alignment || backing_.data == nullptr) {
            return std::unexpected(errc::wrong_owner);
        }

        auto index = index_for(block.data);
        if (!index) {
            return std::unexpected(errc::wrong_owner);
        }
        auto& state = states_[*index];
        if (!state.allocated) {
            return std::unexpected(errc::wrong_owner);
        }
        if (state.generation != descriptor.generation) {
            return std::unexpected(errc::wrong_owner);
        }

        auto active = checked_sub(active_bytes_, options_.block_size);
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
        auto generation = checked_add(state.generation, 1);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        state.allocated = false;
        state.generation = *generation;
        free_indices_.push_back(*index);
        active_bytes_ = *active;
        active_allocations_ = *allocations;
        total_deallocations_ = *total_deallocations;
        return {};
    }

    [[nodiscard]] resource_traits traits() const noexcept {
        return resource_traits{
            .name = "fixed_block_pool",
            .ownership = resource_ownership::caller_owned,
            .thread_safety = resource_thread_safety::shard_confined,
            .supports_remote_deallocate = false,
        };
    }

    [[nodiscard]] usage_snapshot usage() const noexcept {
        return usage_snapshot{
            .active_bytes = active_bytes_,
            .reserved_bytes = backing_.size,
            .active_allocations = active_allocations_,
            .total_allocations = total_allocations_,
            .total_deallocations = total_deallocations_,
            .failed_allocations = failed_allocations_,
        };
    }

private:
    struct block_state {
        bool allocated{};
        std::size_t generation{};
    };

    [[nodiscard]] std::expected<void, errc> ensure_initialized() noexcept {
        if (backing_.data != nullptr) {
            return {};
        }
        if (options_.capacity == 0 || options_.block_size == 0) {
            return std::unexpected(errc::out_of_memory);
        }
        auto stride = align_up(options_.block_size, options_.block_alignment);
        if (!stride) {
            return std::unexpected(stride.error());
        }
        auto total = checked_mul(*stride, options_.capacity);
        if (!total) {
            return std::unexpected(total.error());
        }

        auto block = upstream_.allocate(*total, options_.block_alignment);
        if (!block) {
            return std::unexpected(block.error());
        }
        try {
            states_.assign(options_.capacity, block_state{});
            free_indices_.reserve(options_.capacity);
            for (std::size_t index = 0; index < options_.capacity; ++index) {
                free_indices_.push_back(options_.capacity - index - 1);
            }
        } catch (...) {
            static_cast<void>(upstream_.deallocate(*block));
            states_.clear();
            free_indices_.clear();
            return std::unexpected(errc::out_of_memory);
        }
        backing_ = *block;
        stride_ = *stride;
        return {};
    }

    [[nodiscard]] std::expected<std::size_t, errc> index_for(void* pointer) const noexcept {
        const auto begin = reinterpret_cast<std::uintptr_t>(backing_.data);
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        auto bytes = checked_mul(stride_, options_.capacity);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        auto end = checked_add(begin, *bytes);
        if (!end || address < begin || address >= *end) {
            return std::unexpected(errc::wrong_owner);
        }
        auto offset = checked_sub(address, begin);
        if (!offset || (*offset % stride_) != 0) {
            return std::unexpected(errc::wrong_owner);
        }
        auto index = *offset / stride_;
        if (index >= options_.capacity) {
            return std::unexpected(errc::wrong_owner);
        }
        return index;
    }

    [[nodiscard]] std::expected<fixed_block_allocation, errc>
    descriptor_for_current(allocation block) const noexcept {
        if (detail::has_invalid_non_empty_shape(block) || block.size != options_.block_size ||
            block.alignment != options_.block_alignment || backing_.data == nullptr) {
            return std::unexpected(errc::wrong_owner);
        }
        auto index = index_for(block.data);
        if (!index) {
            return std::unexpected(index.error());
        }
        const auto& state = states_[*index];
        if (!state.allocated) {
            return std::unexpected(errc::wrong_owner);
        }
        return fixed_block_allocation{.block = block, .generation = state.generation};
    }

    resource_ref upstream_;
    fixed_block_pool_options options_{};
    allocation backing_{};
    std::size_t stride_{};
    std::vector<block_state> states_{};
    std::vector<std::size_t> free_indices_{};
    std::size_t active_bytes_{};
    std::size_t active_allocations_{};
    std::size_t total_allocations_{};
    std::size_t total_deallocations_{};
    std::size_t failed_allocations_{};
};

} // namespace voris::mem
