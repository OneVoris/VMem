#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/detail/accounting.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace voris::mem
{

struct slab_size_class
{
    std::size_t block_size{};
    std::size_t block_alignment{};
};

struct slab_remote_snapshot
{
    std::size_t queued_count{};
    std::size_t drained_count{};
    std::size_t saturated_count{};
    std::size_t slow_path_count{};
};

struct slab_size_class_snapshot
{
    std::size_t class_index{};
    std::size_t block_size{};
    std::size_t block_alignment{};
    std::size_t active_count{};
    std::size_t free_count{};
    std::size_t remote_count{};
    std::size_t active_bytes{};
    std::size_t free_bytes{};
    std::size_t remote_bytes{};
    std::size_t fragmentation_bytes{};
};

struct slab_options
{
    std::size_t slab_size{64 * 1024};
    std::size_t remote_queue_capacity{256};
    bool force_remote_queue_push_failure{};
};

struct slab_allocation
{
    allocation block{};
    std::size_t generation{};
};

inline constexpr std::array<slab_size_class, 8> default_slab_size_classes{{
    {.block_size = 8, .block_alignment = 8},
    {.block_size = 16, .block_alignment = 16},
    {.block_size = 32, .block_alignment = 16},
    {.block_size = 64, .block_alignment = 16},
    {.block_size = 128, .block_alignment = 16},
    {.block_size = 256, .block_alignment = 16},
    {.block_size = 512, .block_alignment = 16},
    {.block_size = 1024, .block_alignment = 16},
}};

[[nodiscard]] inline std::optional<slab_size_class> select_slab_size_class(
    std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept
{
    if (!is_power_of_two(alignment))
    {
        return std::nullopt;
    }
    for (auto entry : default_slab_size_classes)
    {
        if (size <= entry.block_size && alignment <= entry.block_size)
        {
            entry.block_alignment = std::max(entry.block_alignment, alignment);
            return entry;
        }
    }
    return std::nullopt;
}

class slab_resource
{
  public:
    explicit slab_resource(resource_ref upstream, slab_options options = {}) noexcept
        : upstream_(upstream), options_(normalize(options))
    {
    }

    slab_resource(const slab_resource &) = delete;
    slab_resource &operator=(const slab_resource &) = delete;

    ~slab_resource()
    {
        for (auto block : slabs_)
        {
            static_cast<void>(upstream_.deallocate(block));
        }
    }

    [[nodiscard]] std::expected<allocation, errc> allocate(const allocation_request &request) noexcept
    {
        auto block = allocate_block(request);
        if (!block)
        {
            return std::unexpected(block.error());
        }
        return block->block;
    }

    [[nodiscard]] std::expected<slab_allocation, errc> allocate_block(const allocation_request &request) noexcept
    {
        if (!is_power_of_two(request.alignment))
        {
            failed_allocations_ += 1;
            return std::unexpected(errc::invalid_alignment);
        }
        if (request.size == 0)
        {
            return slab_allocation{.block = allocation{nullptr, 0, request.alignment}, .generation = 0};
        }

        auto selected = select_slab_size_class(request.size, request.alignment);
        if (!selected)
        {
            failed_allocations_ += 1;
            return std::unexpected(errc::out_of_memory);
        }

        const auto bucket_index = class_index(selected->block_size);
        for (;;)
        {
            std::unique_lock state_lock{state_mutex_};
            auto &bucket = buckets_[bucket_index];
            if (bucket.free_list.empty())
            {
                state_lock.unlock();
                auto growth = allocate_growth(*selected);
                if (!growth)
                {
                    std::lock_guard failed_lock{state_mutex_};
                    failed_allocations_ += 1;
                    return std::unexpected(growth.error());
                }

                state_lock.lock();
                auto published = publish_growth_locked(bucket, *growth);
                state_lock.unlock();
                if (!published)
                {
                    static_cast<void>(upstream_.deallocate(growth->block));
                    std::lock_guard failed_lock{state_mutex_};
                    failed_allocations_ += 1;
                    return std::unexpected(published.error());
                }
                continue;
            }
            auto allocated = allocate_from_bucket_locked(bucket, *selected);
            if (!allocated)
            {
                failed_allocations_ += 1;
            }
            return allocated;
        }
    }

    [[nodiscard]] std::expected<void, errc> deallocate(allocation block) noexcept
    {
        if (detail::is_empty_allocation(block))
        {
            return {};
        }
        std::lock_guard state_lock{state_mutex_};
        auto descriptor = descriptor_for_current_locked(block);
        if (!descriptor)
        {
            return std::unexpected(descriptor.error());
        }
        return release_local_locked(*descriptor, true);
    }

    [[nodiscard]] std::expected<void, errc> deallocate_block(slab_allocation descriptor) noexcept
    {
        if (detail::is_empty_allocation(descriptor.block))
        {
            return {};
        }
        std::lock_guard state_lock{state_mutex_};
        return release_local_locked(descriptor, true);
    }

    [[nodiscard]] std::expected<void, errc> remote_deallocate(allocation block) noexcept
    {
        if (detail::is_empty_allocation(block))
        {
            return {};
        }
        slab_allocation descriptor{};
        {
            std::lock_guard state_lock{state_mutex_};
            auto current = descriptor_for_current_locked(block);
            if (!current)
            {
                return std::unexpected(current.error());
            }
            descriptor = *current;
        }
        return remote_deallocate_block(descriptor);
    }

    [[nodiscard]] std::expected<void, errc> remote_deallocate_block(slab_allocation descriptor) noexcept
    {
        if (detail::is_empty_allocation(descriptor.block))
        {
            return {};
        }
        {
            std::lock_guard state_lock{state_mutex_};
            auto validated = validate_descriptor_locked(descriptor);
            if (!validated)
            {
                return std::unexpected(validated.error());
            }
        }
        bool use_slow_path{};
        {
            std::lock_guard lock{remote_mutex_};
            if (remote_queue_.size() < options_.remote_queue_capacity && !options_.force_remote_queue_push_failure)
            {
                try
                {
                    remote_queue_.push_back(descriptor);
                }
                catch (...)
                {
                    use_slow_path = true;
                }
                if (!use_slow_path)
                {
                    return {};
                }
            }
            else
            {
                if (remote_queue_.size() >= options_.remote_queue_capacity)
                {
                    auto saturated = checked_add(remote_saturated_count_, 1);
                    remote_saturated_count_ = saturated ? *saturated : remote_saturated_count_;
                }
                use_slow_path = true;
            }
        }
        return slow_path_remote_release(descriptor);
    }

    std::size_t drain_remote_frees() noexcept
    {
        std::vector<slab_allocation> pending;
        {
            std::lock_guard lock{remote_mutex_};
            pending.swap(remote_queue_);
        }

        std::size_t drained{};
        for (auto descriptor : pending)
        {
            auto released = deallocate_block(descriptor);
            if (released)
            {
                auto next_drained = checked_add(drained, 1);
                drained = next_drained ? *next_drained : drained;
            }
        }
        {
            std::lock_guard lock{remote_mutex_};
            auto drained_total = checked_add(remote_drained_count_, drained);
            remote_drained_count_ = drained_total ? *drained_total : remote_drained_count_;
        }
        return drained;
    }

    [[nodiscard]] slab_remote_snapshot remote_usage() const noexcept
    {
        std::lock_guard lock{remote_mutex_};
        return slab_remote_snapshot{
            .queued_count = remote_queue_.size(),
            .drained_count = remote_drained_count_,
            .saturated_count = remote_saturated_count_,
            .slow_path_count = remote_slow_path_count_,
        };
    }

    [[nodiscard]] std::array<slab_size_class_snapshot, default_slab_size_classes.size()> size_class_snapshots()
        const noexcept
    {
        std::array<slab_size_class_snapshot, default_slab_size_classes.size()> snapshots{};
        for (std::size_t index = 0; index < default_slab_size_classes.size(); ++index)
        {
            snapshots[index].class_index = index;
            snapshots[index].block_size = default_slab_size_classes[index].block_size;
            snapshots[index].block_alignment = default_slab_size_classes[index].block_alignment;
        }

        // Snapshot lock order is state then remote. Remote-free paths do not hold the
        // remote lock while acquiring state, so this produces a consistent view.
        std::lock_guard state_lock{state_mutex_};
        std::lock_guard remote_lock{remote_mutex_};
        for (const auto &[_, record] : active_)
        {
            auto &snapshot = snapshots[record.bucket_index];
            if (record.allocated)
            {
                snapshot.active_count = checked_add(snapshot.active_count, 1U).value_or(snapshot.active_count);
                snapshot.active_bytes =
                    checked_add(snapshot.active_bytes, record.block_size).value_or(snapshot.active_bytes);
            }
            else
            {
                snapshot.free_count = checked_add(snapshot.free_count, 1U).value_or(snapshot.free_count);
                snapshot.free_bytes = checked_add(snapshot.free_bytes, record.block_size).value_or(snapshot.free_bytes);
            }
        }

        for (const auto &descriptor : remote_queue_)
        {
            for (auto &snapshot : snapshots)
            {
                if (snapshot.block_size == descriptor.block.size)
                {
                    snapshot.remote_count = checked_add(snapshot.remote_count, 1U).value_or(snapshot.remote_count);
                    snapshot.remote_bytes =
                        checked_add(snapshot.remote_bytes, descriptor.block.size).value_or(snapshot.remote_bytes);
                    snapshot.active_count = checked_sub(snapshot.active_count, 1U).value_or(snapshot.active_count);
                    snapshot.active_bytes =
                        checked_sub(snapshot.active_bytes, descriptor.block.size).value_or(snapshot.active_bytes);
                    break;
                }
            }
        }

        for (auto &snapshot : snapshots)
        {
            auto free_and_remote = checked_add(snapshot.free_bytes, snapshot.remote_bytes);
            snapshot.fragmentation_bytes = free_and_remote ? *free_and_remote : snapshot.free_bytes;
        }
        return snapshots;
    }

    [[nodiscard]] resource_traits traits() const noexcept
    {
        return resource_traits{
            .name = "slab_resource",
            .ownership = resource_ownership::caller_owned,
            .thread_safety = resource_thread_safety::shard_confined,
            .supports_remote_deallocate = true,
        };
    }

    [[nodiscard]] usage_snapshot usage() const noexcept
    {
        std::lock_guard state_lock{state_mutex_};
        return usage_snapshot{
            .active_bytes = active_bytes_,
            .reserved_bytes = reserved_bytes_,
            .active_allocations = active_allocations_,
            .total_allocations = total_allocations_,
            .total_deallocations = total_deallocations_,
            .failed_allocations = failed_allocations_,
        };
    }

  private:
    struct block_record
    {
        std::size_t block_size{};
        std::size_t alignment{};
        std::size_t bucket_index{};
        bool allocated{};
        std::size_t generation{};
    };

    struct bucket
    {
        std::vector<void *> free_list{};
    };

    struct slab_growth
    {
        allocation block{};
        slab_size_class selected{};
        std::size_t capacity{};
        std::size_t stride{};
    };

    [[nodiscard]] static slab_options normalize(slab_options options) noexcept
    {
        if (options.slab_size == 0)
        {
            options.slab_size = 4096;
        }
        return options;
    }

    [[nodiscard]] static std::size_t class_index(std::size_t block_size) noexcept
    {
        for (std::size_t index = 0; index < default_slab_size_classes.size(); ++index)
        {
            if (default_slab_size_classes[index].block_size == block_size)
            {
                return index;
            }
        }
        return default_slab_size_classes.size() - 1;
    }

    [[nodiscard]] std::expected<slab_growth, errc> allocate_growth(slab_size_class selected) noexcept
    {
        auto stride = align_up(selected.block_size, selected.block_alignment);
        if (!stride)
        {
            return std::unexpected(stride.error());
        }
        const auto slab_size = std::max(options_.slab_size, *stride);
        auto capacity = slab_size / *stride;
        if (capacity == 0)
        {
            return std::unexpected(errc::out_of_memory);
        }
        auto total = checked_mul(capacity, *stride);
        if (!total)
        {
            return std::unexpected(total.error());
        }

        auto block = upstream_.allocate(*total, selected.block_alignment);
        if (!block)
        {
            return std::unexpected(block.error());
        }
        return slab_growth{.block = *block, .selected = selected, .capacity = capacity, .stride = *stride};
    }

    [[nodiscard]] std::expected<void, errc> publish_growth_locked(bucket &bucket_ref,
                                                                  const slab_growth &growth) noexcept
    {
        auto reserved = checked_add(reserved_bytes_, growth.block.size);
        if (!reserved)
        {
            return std::unexpected(reserved.error());
        }

        try
        {
            slabs_.reserve(slabs_.size() + 1);
            active_.reserve(active_.size() + growth.capacity);
            bucket_ref.free_list.reserve(bucket_ref.free_list.size() + growth.capacity);
            slabs_.push_back(growth.block);
            auto bucket_index = class_index(growth.selected.block_size);
            const auto base = reinterpret_cast<std::uintptr_t>(growth.block.data);
            for (std::size_t index = 0; index < growth.capacity; ++index)
            {
                auto offset = checked_mul(index, growth.stride);
                if (!offset)
                {
                    throw errc::size_overflow;
                }
                auto address = checked_add(base, *offset);
                if (!address)
                {
                    throw errc::size_overflow;
                }
                auto pointer = reinterpret_cast<void *>(*address);
                bucket_ref.free_list.push_back(pointer);
                active_.emplace(pointer, block_record{.block_size = growth.selected.block_size,
                                                      .alignment = growth.selected.block_alignment,
                                                      .bucket_index = bucket_index,
                                                      .allocated = false,
                                                      .generation = 0});
            }
        }
        catch (...)
        {
            if (!slabs_.empty() && slabs_.back().data == growth.block.data)
            {
                slabs_.pop_back();
            }
            const auto base = reinterpret_cast<std::uintptr_t>(growth.block.data);
            auto slab_end = checked_add(base, growth.block.size);
            if (slab_end)
            {
                for (auto it = active_.begin(); it != active_.end();)
                {
                    const auto address = reinterpret_cast<std::uintptr_t>(it->first);
                    if (address >= base && address < *slab_end)
                    {
                        it = active_.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
                bucket_ref.free_list.erase(std::remove_if(bucket_ref.free_list.begin(), bucket_ref.free_list.end(),
                                                          [&](void *pointer) {
                                                              const auto address =
                                                                  reinterpret_cast<std::uintptr_t>(pointer);
                                                              return address >= base && address < *slab_end;
                                                          }),
                                           bucket_ref.free_list.end());
            }
            return std::unexpected(errc::out_of_memory);
        }
        reserved_bytes_ = *reserved;
        return {};
    }

    [[nodiscard]] std::expected<slab_allocation, errc> allocate_from_bucket_locked(bucket &bucket_ref,
                                                                                   slab_size_class selected) noexcept
    {
        if (bucket_ref.free_list.empty())
        {
            return std::unexpected(errc::out_of_memory);
        }

        auto pointer = bucket_ref.free_list.back();
        bucket_ref.free_list.pop_back();
        auto found = active_.find(pointer);
        if (found == active_.end())
        {
            return std::unexpected(errc::wrong_owner);
        }
        auto active = checked_add(active_bytes_, selected.block_size);
        if (!active)
        {
            bucket_ref.free_list.push_back(pointer);
            return std::unexpected(active.error());
        }
        auto allocations = checked_add(active_allocations_, 1);
        if (!allocations)
        {
            bucket_ref.free_list.push_back(pointer);
            return std::unexpected(allocations.error());
        }
        auto total_allocations = checked_add(total_allocations_, 1);
        if (!total_allocations)
        {
            bucket_ref.free_list.push_back(pointer);
            return std::unexpected(total_allocations.error());
        }
        auto generation = checked_add(found->second.generation, 1);
        if (!generation)
        {
            bucket_ref.free_list.push_back(pointer);
            return std::unexpected(generation.error());
        }
        found->second.allocated = true;
        found->second.generation = *generation;
        active_bytes_ = *active;
        active_allocations_ = *allocations;
        total_allocations_ = *total_allocations;
        return slab_allocation{
            .block = allocation{pointer, selected.block_size, selected.block_alignment},
            .generation = *generation,
        };
    }

    [[nodiscard]] std::expected<void, errc> slow_path_remote_release(slab_allocation descriptor) noexcept
    {
        std::lock_guard lock{slow_path_mutex_};
        auto released = deallocate_block(descriptor);
        if (released)
        {
            std::lock_guard remote_lock{remote_mutex_};
            auto slow = checked_add(remote_slow_path_count_, 1);
            remote_slow_path_count_ = slow ? *slow : remote_slow_path_count_;
        }
        return released;
    }

    [[nodiscard]] std::expected<void, errc> release_local_locked(slab_allocation descriptor,
                                                                 bool count_deallocation) noexcept
    {
        const auto block = descriptor.block;
        if (detail::is_empty_allocation(block))
        {
            return {};
        }
        if (detail::has_invalid_non_empty_shape(block) || !is_power_of_two(block.alignment))
        {
            return std::unexpected(errc::wrong_owner);
        }

        auto found = active_.find(block.data);
        if (found == active_.end())
        {
            return std::unexpected(errc::wrong_owner);
        }
        auto &record = found->second;
        if (!record.allocated || record.block_size != block.size || record.alignment != block.alignment)
        {
            return std::unexpected(errc::wrong_owner);
        }
        if (record.generation != descriptor.generation)
        {
            return std::unexpected(errc::wrong_owner);
        }

        auto active = checked_sub(active_bytes_, record.block_size);
        if (!active)
        {
            return std::unexpected(active.error());
        }
        auto allocations = checked_sub(active_allocations_, 1);
        if (!allocations)
        {
            return std::unexpected(allocations.error());
        }
        auto total_deallocations = checked_add(total_deallocations_, count_deallocation ? 1U : 0U);
        if (!total_deallocations)
        {
            return std::unexpected(total_deallocations.error());
        }
        record.allocated = false;
        buckets_[record.bucket_index].free_list.push_back(block.data);
        active_bytes_ = *active;
        active_allocations_ = *allocations;
        total_deallocations_ = *total_deallocations;
        return {};
    }

    [[nodiscard]] std::expected<slab_allocation, errc> descriptor_for_current_locked(allocation block) const noexcept
    {
        if (detail::has_invalid_non_empty_shape(block) || !is_power_of_two(block.alignment))
        {
            return std::unexpected(errc::wrong_owner);
        }
        auto found = active_.find(block.data);
        if (found == active_.end())
        {
            return std::unexpected(errc::wrong_owner);
        }
        const auto &record = found->second;
        if (!record.allocated || record.block_size != block.size || record.alignment != block.alignment)
        {
            return std::unexpected(errc::wrong_owner);
        }
        return slab_allocation{.block = block, .generation = record.generation};
    }

    [[nodiscard]] std::expected<void, errc> validate_descriptor_locked(slab_allocation descriptor) const noexcept
    {
        const auto block = descriptor.block;
        if (detail::has_invalid_non_empty_shape(block) || !is_power_of_two(block.alignment))
        {
            return std::unexpected(errc::wrong_owner);
        }
        auto found = active_.find(block.data);
        if (found == active_.end())
        {
            return std::unexpected(errc::wrong_owner);
        }
        const auto &record = found->second;
        if (!record.allocated || record.block_size != block.size || record.alignment != block.alignment ||
            record.generation != descriptor.generation)
        {
            return std::unexpected(errc::wrong_owner);
        }
        return {};
    }

    resource_ref upstream_;
    slab_options options_{};
    mutable std::mutex state_mutex_{};
    std::array<bucket, default_slab_size_classes.size()> buckets_{};
    std::vector<allocation> slabs_{};
    std::unordered_map<void *, block_record> active_{};
    mutable std::mutex remote_mutex_{};
    std::vector<slab_allocation> remote_queue_{};
    std::mutex slow_path_mutex_{};
    std::size_t active_bytes_{};
    std::size_t reserved_bytes_{};
    std::size_t active_allocations_{};
    std::size_t total_allocations_{};
    std::size_t total_deallocations_{};
    std::size_t failed_allocations_{};
    std::size_t remote_drained_count_{};
    std::size_t remote_saturated_count_{};
    std::size_t remote_slow_path_count_{};
};

} // namespace voris::mem
