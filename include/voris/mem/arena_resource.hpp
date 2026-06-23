#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/detail/accounting.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <utility>
#include <vector>

namespace voris::mem {

struct arena_options {
    std::size_t initial_chunk_size{64 * 1024};
    std::size_t max_chunk_size{1024 * 1024};
    std::size_t retained_bytes_limit{0};
};

class arena_resource {
public:
    explicit arena_resource(resource_ref upstream, arena_options options = {}) noexcept
        : upstream_(upstream), options_(normalize(options)), next_chunk_size_(options_.initial_chunk_size) {}

    arena_resource(const arena_resource&) = delete;
    arena_resource& operator=(const arena_resource&) = delete;

    ~arena_resource() {
        release_all_chunks();
    }

    [[nodiscard]] std::expected<allocation, errc>
    allocate(const allocation_request& request) noexcept {
        if (!is_power_of_two(request.alignment)) {
            failed_allocations_ += 1;
            return std::unexpected(errc::invalid_alignment);
        }
        if (request.size == 0) {
            return allocation{nullptr, 0, request.alignment};
        }

        auto required = align_up(request.size, request.alignment);
        if (!required) {
            failed_allocations_ += 1;
            return std::unexpected(required.error());
        }
        auto accountable = check_success_accounting(request.size);
        if (!accountable) {
            failed_allocations_ += 1;
            return std::unexpected(accountable.error());
        }

        auto block = allocate_from_existing(request.size, request.alignment);
        if (block) {
            account_success(request.size);
            return *block;
        }

        auto grown = grow_for(*required, request.alignment);
        if (!grown) {
            failed_allocations_ += 1;
            return std::unexpected(grown.error());
        }

        block = allocate_from_existing(request.size, request.alignment);
        if (!block) {
            failed_allocations_ += 1;
            return std::unexpected(errc::out_of_memory);
        }
        account_success(request.size);
        return *block;
    }

    [[nodiscard]] std::expected<void, errc> deallocate(allocation block) noexcept {
        if (detail::is_empty_allocation(block)) {
            return {};
        }
        if (detail::has_invalid_non_empty_shape(block) || !is_power_of_two(block.alignment)) {
            return std::unexpected(errc::wrong_owner);
        }
        if (!contains_block(block)) {
            return std::unexpected(errc::wrong_owner);
        }
        return {};
    }

    void reset() noexcept {
        active_bytes_ = 0;
        active_allocations_ = 0;

        std::size_t retained{};
        std::vector<chunk> keep;
        try {
            keep.reserve(chunks_.size());
        } catch (...) {
            release_all_chunks();
            return;
        }

        for (auto& current : chunks_) {
            auto next_retained = checked_add(retained, current.block.size);
            if (next_retained && *next_retained <= options_.retained_bytes_limit) {
                current.cursor = 0;
                retained = *next_retained;
                keep.push_back(current);
            } else {
                static_cast<void>(upstream_.deallocate(current.block));
            }
        }
        chunks_ = std::move(keep);
        reserved_bytes_ = retained;
        next_chunk_size_ = options_.initial_chunk_size;
    }

    [[nodiscard]] resource_traits traits() const noexcept {
        return resource_traits{
            .name = "arena_resource",
            .ownership = resource_ownership::caller_owned,
            .thread_safety = resource_thread_safety::shard_confined,
            .supports_remote_deallocate = false,
        };
    }

    [[nodiscard]] usage_snapshot usage() const noexcept {
        return usage_snapshot{
            .active_bytes = active_bytes_,
            .reserved_bytes = reserved_bytes_,
            .active_allocations = active_allocations_,
            .total_allocations = total_allocations_,
            .total_deallocations = 0,
            .failed_allocations = failed_allocations_,
        };
    }

private:
    struct chunk {
        allocation block{};
        std::size_t cursor{};
    };

    [[nodiscard]] static arena_options normalize(arena_options options) noexcept {
        if (options.initial_chunk_size == 0) {
            options.initial_chunk_size = 1;
        }
        if (options.max_chunk_size < options.initial_chunk_size) {
            options.max_chunk_size = options.initial_chunk_size;
        }
        return options;
    }

    [[nodiscard]] std::expected<void, errc>
    grow_for(std::size_t required_size, std::size_t alignment) noexcept {
        auto needed = align_up(required_size, alignment);
        if (!needed) {
            return std::unexpected(needed.error());
        }

        std::size_t chunk_size = std::max(*needed, next_chunk_size_);
        chunk_size = std::min(chunk_size, options_.max_chunk_size);
        if (chunk_size < *needed) {
            chunk_size = *needed;
        }

        auto upstream_block = upstream_.allocate(chunk_size, alignment);
        if (!upstream_block) {
            return std::unexpected(upstream_block.error());
        }

        try {
            chunks_.push_back(chunk{*upstream_block, 0});
        } catch (...) {
            static_cast<void>(upstream_.deallocate(*upstream_block));
            return std::unexpected(errc::out_of_memory);
        }

        auto reserved = checked_add(reserved_bytes_, upstream_block->size);
        if (!reserved) {
            chunks_.pop_back();
            static_cast<void>(upstream_.deallocate(*upstream_block));
            return std::unexpected(reserved.error());
        }
        reserved_bytes_ = *reserved;

        auto doubled = checked_mul(next_chunk_size_, 2);
        next_chunk_size_ = doubled ? std::min(*doubled, options_.max_chunk_size)
                                   : options_.max_chunk_size;
        return {};
    }

    [[nodiscard]] std::expected<allocation, errc>
    allocate_from_existing(std::size_t size, std::size_t alignment) noexcept {
        for (auto& current : chunks_) {
            auto base = reinterpret_cast<std::uintptr_t>(current.block.data);
            auto cursor_address = checked_add(base, current.cursor);
            if (!cursor_address) {
                return std::unexpected(cursor_address.error());
            }
            auto aligned_address = align_up(*cursor_address, alignment);
            if (!aligned_address) {
                return std::unexpected(aligned_address.error());
            }
            auto offset = checked_sub(*aligned_address, base);
            if (!offset) {
                return std::unexpected(offset.error());
            }
            auto end = checked_add(*offset, size);
            if (!end) {
                return std::unexpected(end.error());
            }
            if (*end <= current.block.size) {
                current.cursor = *end;
                return allocation{reinterpret_cast<void*>(*aligned_address), size, alignment};
            }
        }
        return std::unexpected(errc::out_of_memory);
    }

    [[nodiscard]] bool contains_block(allocation block) const noexcept {
        const auto begin = reinterpret_cast<std::uintptr_t>(block.data);
        auto end = checked_add(begin, block.size);
        if (!end) {
            return false;
        }
        for (const auto& current : chunks_) {
            const auto chunk_begin = reinterpret_cast<std::uintptr_t>(current.block.data);
            auto chunk_end = checked_add(chunk_begin, current.block.size);
            if (chunk_end && begin >= chunk_begin && *end <= *chunk_end &&
                begin % block.alignment == 0) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::expected<void, errc>
    check_success_accounting(std::size_t size) const noexcept {
        auto active = checked_add(active_bytes_, size);
        if (!active) {
            return std::unexpected(active.error());
        }
        auto allocations = checked_add(active_allocations_, 1);
        if (!allocations) {
            return std::unexpected(allocations.error());
        }
        auto total = checked_add(total_allocations_, 1);
        if (!total) {
            return std::unexpected(total.error());
        }
        return {};
    }

    void account_success(std::size_t size) noexcept {
        active_bytes_ = *checked_add(active_bytes_, size);
        active_allocations_ = *checked_add(active_allocations_, 1);
        total_allocations_ = *checked_add(total_allocations_, 1);
    }

    void release_all_chunks() noexcept {
        for (auto& current : chunks_) {
            static_cast<void>(upstream_.deallocate(current.block));
        }
        chunks_.clear();
        reserved_bytes_ = 0;
    }

    resource_ref upstream_;
    arena_options options_{};
    std::size_t next_chunk_size_{};
    std::vector<chunk> chunks_{};
    std::size_t active_bytes_{};
    std::size_t reserved_bytes_{};
    std::size_t active_allocations_{};
    std::size_t total_allocations_{};
    std::size_t failed_allocations_{};
};

} // namespace voris::mem
