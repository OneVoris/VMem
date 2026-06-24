#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/detail/accounting.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/page_source.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__has_feature)
#    if __has_feature(address_sanitizer)
#        define VORIS_VMEM_HAS_ADDRESS_SANITIZER 1
#    endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#    define VORIS_VMEM_HAS_ADDRESS_SANITIZER 1
#endif
#if defined(VORIS_VMEM_HAS_ADDRESS_SANITIZER)
#    include <sanitizer/asan_interface.h>
#endif

namespace voris::mem {

namespace detail {

inline void asan_poison_memory_region(void* data, std::size_t size) noexcept {
#if defined(VORIS_VMEM_HAS_ADDRESS_SANITIZER)
    __asan_poison_memory_region(data, size);
#else
    static_cast<void>(data);
    static_cast<void>(size);
#endif
}

inline void asan_unpoison_memory_region(void* data, std::size_t size) noexcept {
#if defined(VORIS_VMEM_HAS_ADDRESS_SANITIZER)
    __asan_unpoison_memory_region(data, size);
#else
    static_cast<void>(data);
    static_cast<void>(size);
#endif
}

} // namespace detail

struct debug_resource_options {
    std::size_t redzone_size{16};
    bool poison_on_allocate{true};
    bool poison_on_free{true};
    std::byte allocate_poison{0xA5};
    std::byte free_poison{0x5A};
    std::byte redzone_poison{0xFD};
    bool preserve_sanitizer_diagnostics{true};
    bool guard_pages_for_large_allocations{};
    std::size_t guard_page_threshold{64 * 1024};
};

struct debug_allocation {
    allocation block{};
    std::size_t generation{};
};

struct debug_resource_snapshot {
    usage_snapshot usage{};
    std::size_t redzone_failure_count{};
    std::size_t guard_page_request_count{};
    std::size_t guard_page_allocation_count{};
    std::size_t guard_page_fallback_count{};
};

struct debug_leak_record {
    allocation block{};
    std::size_t generation{};
    memory_tag tag{};
    source_location location{};
    bool guard_pages{};
};

struct debug_leak_snapshot {
    std::vector<debug_leak_record> records{};
    std::size_t active_bytes{};
    std::size_t active_allocations{};
};

struct debug_leak_diff {
    std::vector<debug_leak_record> added{};
    std::vector<debug_leak_record> removed{};
    std::size_t added_bytes{};
    std::size_t removed_bytes{};
};

[[nodiscard]] inline debug_leak_diff
diff_leak_snapshots(const debug_leak_snapshot& before, const debug_leak_snapshot& after) {
    debug_leak_diff diff;
    for (const auto& record : after.records) {
        const auto found = std::find_if(before.records.begin(),
                                        before.records.end(),
                                        [&](const debug_leak_record& candidate) {
                                            return candidate.block.data == record.block.data &&
                                                   candidate.generation == record.generation;
                                        });
        if (found == before.records.end()) {
            diff.added.push_back(record);
            auto bytes = checked_add(diff.added_bytes, record.block.size);
            diff.added_bytes = bytes ? *bytes : diff.added_bytes;
        }
    }
    for (const auto& record : before.records) {
        const auto found = std::find_if(after.records.begin(),
                                        after.records.end(),
                                        [&](const debug_leak_record& candidate) {
                                            return candidate.block.data == record.block.data &&
                                                   candidate.generation == record.generation;
                                        });
        if (found == after.records.end()) {
            diff.removed.push_back(record);
            auto bytes = checked_add(diff.removed_bytes, record.block.size);
            diff.removed_bytes = bytes ? *bytes : diff.removed_bytes;
        }
    }
    return diff;
}

class debug_resource {
public:
    explicit debug_resource(resource_ref upstream, debug_resource_options options = {}) noexcept
        : upstream_(upstream), options_(options) {}

    debug_resource(const debug_resource&) = delete;
    debug_resource& operator=(const debug_resource&) = delete;

    ~debug_resource() {
        std::lock_guard lock{mutex_};
        for (const auto& [_, entry] : records_) {
            if (!entry.allocated) {
                continue;
            }
            if (entry.guard_pages) {
                static_cast<void>(page_source_.release(entry.reserved_span));
            } else {
                static_cast<void>(upstream_.deallocate(entry.raw_block));
            }
        }
        records_.clear();
    }

    [[nodiscard]] std::expected<allocation, errc>
    allocate(const allocation_request& request) noexcept {
        auto descriptor = allocate_block(request);
        if (!descriptor) {
            return std::unexpected(descriptor.error());
        }
        return descriptor->block;
    }

    [[nodiscard]] std::expected<debug_allocation, errc>
    allocate_block(const allocation_request& request) noexcept {
        if (!is_power_of_two(request.alignment)) {
            count_failed_allocation();
            return std::unexpected(errc::invalid_alignment);
        }
        if (request.size == 0) {
            return debug_allocation{
                .block = allocation{nullptr, 0, request.alignment},
                .generation = 0,
            };
        }

        auto layout = make_layout(request.size, request.alignment);
        if (!layout) {
            count_failed_allocation();
            return std::unexpected(layout.error());
        }

        raw_storage storage{};
        const bool wants_guard_pages =
            options_.guard_pages_for_large_allocations &&
            request.size >= options_.guard_page_threshold;
        if (wants_guard_pages) {
            count_guard_request();
            auto guarded = allocate_guarded(*layout);
            if (guarded) {
                storage = *guarded;
                count_guard_allocation();
            } else if (guarded.error() == errc::size_overflow ||
                       guarded.error() == errc::invalid_alignment) {
                count_failed_allocation();
                return std::unexpected(guarded.error());
            } else {
                count_guard_fallback();
            }
        }

        if (storage.data == nullptr) {
            auto block = upstream_.allocate(layout->raw_size, request.alignment);
            if (!block) {
                count_failed_allocation();
                return std::unexpected(block.error());
            }
            storage.raw_block = *block;
            storage.data = block->data;
            storage.size = block->size;
        }

        auto payload = payload_from(storage.data, *layout, request.alignment);
        if (!payload) {
            release_raw_storage(storage);
            count_failed_allocation();
            return std::unexpected(payload.error());
        }

        auto payload_block = allocation{*payload, request.size, request.alignment};
        auto generation = register_allocation(storage, payload_block, request);
        if (!generation) {
            release_raw_storage(storage);
            count_failed_allocation();
            return std::unexpected(generation.error());
        }

        unpoison_debug_region(payload_block.data, request.size);
        fill_redzones(payload_block.data, request.size);
        if (options_.poison_on_allocate) {
            std::fill_n(static_cast<std::byte*>(payload_block.data),
                        request.size,
                        options_.allocate_poison);
        }
        poison_redzones(payload_block.data, request.size);

        return debug_allocation{.block = payload_block, .generation = *generation};
    }

    [[nodiscard]] std::expected<void, errc> deallocate(allocation block) noexcept {
        if (detail::is_empty_allocation(block)) {
            return {};
        }
        std::lock_guard lock{mutex_};
        auto found = descriptor_for_current_locked(block);
        if (!found) {
            return std::unexpected(found.error());
        }
        return release_locked(*found, false);
    }

    [[nodiscard]] std::expected<void, errc>
    deallocate_block(debug_allocation descriptor) noexcept {
        if (detail::is_empty_allocation(descriptor.block)) {
            return {};
        }
        std::lock_guard lock{mutex_};
        return release_locked(descriptor, false);
    }

    [[nodiscard]] std::expected<void, errc> remote_deallocate(allocation block) noexcept {
        if (detail::is_empty_allocation(block)) {
            return {};
        }
        std::lock_guard lock{mutex_};
        auto found = descriptor_for_current_locked(block);
        if (!found) {
            return std::unexpected(found.error());
        }
        return release_locked(*found, true);
    }

    [[nodiscard]] resource_traits traits() const noexcept {
        auto traits = upstream_.traits();
        traits.name = "debug_resource";
        return traits;
    }

    [[nodiscard]] usage_snapshot usage() const noexcept {
        std::lock_guard lock{mutex_};
        return usage_snapshot{
            .active_bytes = active_bytes_,
            .reserved_bytes = reserved_bytes_,
            .active_allocations = active_allocations_,
            .total_allocations = total_allocations_,
            .total_deallocations = total_deallocations_,
            .failed_allocations = failed_allocations_,
        };
    }

    [[nodiscard]] debug_resource_snapshot debug_snapshot() const noexcept {
        std::lock_guard lock{mutex_};
        return debug_resource_snapshot{
            .usage =
                usage_snapshot{
                    .active_bytes = active_bytes_,
                    .reserved_bytes = reserved_bytes_,
                    .active_allocations = active_allocations_,
                    .total_allocations = total_allocations_,
                    .total_deallocations = total_deallocations_,
                    .failed_allocations = failed_allocations_,
                },
            .redzone_failure_count = redzone_failure_count_,
            .guard_page_request_count = guard_page_request_count_,
            .guard_page_allocation_count = guard_page_allocation_count_,
            .guard_page_fallback_count = guard_page_fallback_count_,
        };
    }

    [[nodiscard]] debug_leak_snapshot leak_snapshot() const {
        std::lock_guard lock{mutex_};
        debug_leak_snapshot snapshot{
            .active_bytes = active_bytes_,
            .active_allocations = active_allocations_,
        };
        snapshot.records.reserve(active_allocations_);
        for (const auto& [_, entry] : records_) {
            if (entry.allocated) {
                snapshot.records.push_back(debug_leak_record{
                    .block = entry.block,
                    .generation = entry.generation,
                    .tag = entry.tag,
                    .location = entry.location,
                    .guard_pages = entry.guard_pages,
                });
            }
        }
        return snapshot;
    }

private:
    struct layout_info {
        std::size_t raw_size{};
        std::size_t redzone_size{};
    };

    struct raw_storage {
        void* data{};
        std::size_t size{};
        allocation raw_block{};
        page_span reserved_span{};
        bool guard_pages{};
    };

    struct record {
        allocation block{};
        allocation raw_block{};
        page_span reserved_span{};
        std::size_t generation{};
        std::size_t redzone_size{};
        bool allocated{};
        bool guard_pages{};
        memory_tag tag{};
        source_location location{};
    };

    [[nodiscard]] std::expected<layout_info, errc>
    make_layout(std::size_t size, std::size_t alignment) const noexcept {
        auto prefix = checked_add(options_.redzone_size, alignment - 1U);
        if (!prefix) {
            return std::unexpected(prefix.error());
        }
        auto with_payload = checked_add(*prefix, size);
        if (!with_payload) {
            return std::unexpected(with_payload.error());
        }
        auto total = checked_add(*with_payload, options_.redzone_size);
        if (!total) {
            return std::unexpected(total.error());
        }
        return layout_info{.raw_size = *total, .redzone_size = options_.redzone_size};
    }

    [[nodiscard]] std::expected<raw_storage, errc>
    allocate_guarded(layout_info layout) noexcept {
        auto page_size = page_source_.page_size();
        if (!page_size) {
            return std::unexpected(page_size.error());
        }
        auto committed_size = align_up(layout.raw_size, *page_size);
        if (!committed_size) {
            return std::unexpected(committed_size.error());
        }
        auto guard_bytes = checked_mul(*page_size, 2U);
        if (!guard_bytes) {
            return std::unexpected(guard_bytes.error());
        }
        auto reserve_size = checked_add(*committed_size, *guard_bytes);
        if (!reserve_size) {
            return std::unexpected(reserve_size.error());
        }

        auto reserved = page_source_.reserve(*reserve_size);
        if (!reserved) {
            return std::unexpected(reserved.error());
        }

        const auto base = reinterpret_cast<std::uintptr_t>(reserved->data);
        auto committed_address = checked_add(base, *page_size);
        if (!committed_address) {
            static_cast<void>(page_source_.release(*reserved));
            return std::unexpected(committed_address.error());
        }
        page_span committed{reinterpret_cast<void*>(*committed_address), *committed_size};
        auto committed_result = page_source_.commit(committed);
        if (!committed_result) {
            static_cast<void>(page_source_.release(*reserved));
            return std::unexpected(committed_result.error());
        }
        return raw_storage{
            .data = committed.data,
            .size = committed.size,
            .reserved_span = *reserved,
            .guard_pages = true,
        };
    }

    [[nodiscard]] std::expected<void*, errc>
    payload_from(void* storage, layout_info layout, std::size_t alignment) const noexcept {
        const auto base = reinterpret_cast<std::uintptr_t>(storage);
        auto payload_minimum = checked_add(base, layout.redzone_size);
        if (!payload_minimum) {
            return std::unexpected(payload_minimum.error());
        }
        auto payload_address = align_up(*payload_minimum, alignment);
        if (!payload_address) {
            return std::unexpected(payload_address.error());
        }
        return reinterpret_cast<void*>(*payload_address);
    }

    [[nodiscard]] std::expected<std::size_t, errc>
    register_allocation(raw_storage storage,
                        allocation block,
                        const allocation_request& request) noexcept {
        std::lock_guard lock{mutex_};
        auto active_bytes = checked_add(active_bytes_, block.size);
        if (!active_bytes) {
            return std::unexpected(active_bytes.error());
        }
        auto reserved_delta = storage.guard_pages ? storage.reserved_span.size : storage.size;
        auto reserved_bytes = checked_add(reserved_bytes_, reserved_delta);
        if (!reserved_bytes) {
            return std::unexpected(reserved_bytes.error());
        }
        auto active_allocations = checked_add(active_allocations_, 1U);
        if (!active_allocations) {
            return std::unexpected(active_allocations.error());
        }
        auto total_allocations = checked_add(total_allocations_, 1U);
        if (!total_allocations) {
            return std::unexpected(total_allocations.error());
        }

        auto found = records_.find(block.data);
        std::size_t generation = 1U;
        if (found != records_.end()) {
            if (found->second.allocated) {
                return std::unexpected(errc::wrong_owner);
            }
            auto next_generation = checked_add(found->second.generation, 1U);
            if (!next_generation) {
                return std::unexpected(next_generation.error());
            }
            generation = *next_generation;
            found->second = record{
                .block = block,
                .raw_block = storage.raw_block,
                .reserved_span = storage.reserved_span,
                .generation = generation,
                .redzone_size = options_.redzone_size,
                .allocated = true,
                .guard_pages = storage.guard_pages,
                .tag = request.tag,
                .location = request.location,
            };
        } else {
            try {
                records_.emplace(block.data,
                                 record{
                                     .block = block,
                                     .raw_block = storage.raw_block,
                                     .reserved_span = storage.reserved_span,
                                     .generation = generation,
                                     .redzone_size = options_.redzone_size,
                                     .allocated = true,
                                     .guard_pages = storage.guard_pages,
                                     .tag = request.tag,
                                     .location = request.location,
                                 });
            } catch (...) {
                return std::unexpected(errc::out_of_memory);
            }
        }

        active_bytes_ = *active_bytes;
        reserved_bytes_ = *reserved_bytes;
        active_allocations_ = *active_allocations;
        total_allocations_ = *total_allocations;
        return generation;
    }

    [[nodiscard]] std::expected<debug_allocation, errc>
    descriptor_for_current_locked(allocation block) const noexcept {
        if (detail::has_invalid_non_empty_shape(block) || !is_power_of_two(block.alignment)) {
            return std::unexpected(errc::wrong_owner);
        }
        auto found = records_.find(block.data);
        if (found == records_.end()) {
            return std::unexpected(errc::wrong_owner);
        }
        const auto& entry = found->second;
        if (!entry.allocated || entry.block.size != block.size ||
            entry.block.alignment != block.alignment) {
            return std::unexpected(errc::wrong_owner);
        }
        return debug_allocation{.block = block, .generation = entry.generation};
    }

    [[nodiscard]] std::expected<void, errc>
    release_locked(debug_allocation descriptor, bool remote) noexcept {
        const auto block = descriptor.block;
        if (detail::has_invalid_non_empty_shape(block) || !is_power_of_two(block.alignment)) {
            return std::unexpected(errc::wrong_owner);
        }
        auto found = records_.find(block.data);
        if (found == records_.end()) {
            return std::unexpected(errc::wrong_owner);
        }
        auto& entry = found->second;
        if (!entry.allocated || entry.block.size != block.size ||
            entry.block.alignment != block.alignment ||
            entry.generation != descriptor.generation) {
            return std::unexpected(errc::wrong_owner);
        }
        unpoison_redzones(entry);
        if (!redzones_intact(entry)) {
            auto failures = checked_add(redzone_failure_count_, 1U);
            redzone_failure_count_ = failures ? *failures : redzone_failure_count_;
            poison_redzones(entry.block.data, entry.block.size);
            return std::unexpected(errc::wrong_owner);
        }

        auto active_bytes = checked_sub(active_bytes_, entry.block.size);
        if (!active_bytes) {
            poison_redzones(entry.block.data, entry.block.size);
            return std::unexpected(active_bytes.error());
        }
        auto reserved_bytes = checked_sub(reserved_bytes_, entry.guard_pages
                                                               ? entry.reserved_span.size
                                                               : entry.raw_block.size);
        if (!reserved_bytes) {
            poison_redzones(entry.block.data, entry.block.size);
            return std::unexpected(reserved_bytes.error());
        }
        auto active_allocations = checked_sub(active_allocations_, 1U);
        if (!active_allocations) {
            poison_redzones(entry.block.data, entry.block.size);
            return std::unexpected(active_allocations.error());
        }
        auto total_deallocations = checked_add(total_deallocations_, 1U);
        if (!total_deallocations) {
            poison_redzones(entry.block.data, entry.block.size);
            return std::unexpected(total_deallocations.error());
        }

        std::vector<std::byte> saved_payload;
        if (options_.poison_on_free && entry.block.size != 0U) {
            try {
                const auto* begin = static_cast<const std::byte*>(entry.block.data);
                saved_payload.assign(begin, begin + entry.block.size);
            } catch (...) {
                poison_redzones(entry.block.data, entry.block.size);
                return std::unexpected(errc::out_of_memory);
            }
            std::fill_n(static_cast<std::byte*>(entry.block.data),
                        entry.block.size,
                        options_.free_poison);
        }
        if (options_.preserve_sanitizer_diagnostics) {
            detail::asan_poison_memory_region(entry.block.data, entry.block.size);
        }

        std::expected<void, errc> released;
        if (entry.guard_pages) {
            released = page_source_.release(entry.reserved_span);
        } else if (remote) {
            released = upstream_.remote_deallocate(entry.raw_block);
        } else {
            released = upstream_.deallocate(entry.raw_block);
        }
        if (!released) {
            if (options_.preserve_sanitizer_diagnostics) {
                detail::asan_unpoison_memory_region(entry.block.data, entry.block.size);
            }
            if (!saved_payload.empty()) {
                std::copy(saved_payload.begin(),
                          saved_payload.end(),
                          static_cast<std::byte*>(entry.block.data));
            }
            poison_redzones(entry.block.data, entry.block.size);
            return std::unexpected(released.error());
        }

        entry.allocated = false;
        active_bytes_ = *active_bytes;
        reserved_bytes_ = *reserved_bytes;
        active_allocations_ = *active_allocations;
        total_deallocations_ = *total_deallocations;
        return {};
    }

    void fill_redzones(void* payload, std::size_t size) const noexcept {
        auto* bytes = static_cast<std::byte*>(payload);
        std::fill_n(bytes - options_.redzone_size,
                    options_.redzone_size,
                    options_.redzone_poison);
        std::fill_n(bytes + size, options_.redzone_size, options_.redzone_poison);
    }

    void unpoison_debug_region(void* payload, std::size_t size) const noexcept {
        auto* bytes = static_cast<std::byte*>(payload);
        if (!options_.preserve_sanitizer_diagnostics) {
            return;
        }
        auto total = checked_add(options_.redzone_size, size);
        if (!total) {
            return;
        }
        total = checked_add(*total, options_.redzone_size);
        if (!total) {
            return;
        }
        detail::asan_unpoison_memory_region(bytes - options_.redzone_size, *total);
    }

    void poison_redzones(void* payload, std::size_t size) const noexcept {
        auto* bytes = static_cast<std::byte*>(payload);
        if (!options_.preserve_sanitizer_diagnostics) {
            return;
        }
        detail::asan_poison_memory_region(bytes - options_.redzone_size, options_.redzone_size);
        detail::asan_poison_memory_region(bytes + size, options_.redzone_size);
    }

    void unpoison_redzones(const record& entry) const noexcept {
        const auto* payload = static_cast<const std::byte*>(entry.block.data);
        if (!options_.preserve_sanitizer_diagnostics) {
            return;
        }
        detail::asan_unpoison_memory_region(const_cast<std::byte*>(payload - entry.redzone_size),
                                            entry.redzone_size);
        detail::asan_unpoison_memory_region(
            const_cast<std::byte*>(payload + entry.block.size),
            entry.redzone_size);
    }

    [[nodiscard]] bool redzones_intact(const record& entry) const noexcept {
        const auto* payload = static_cast<const std::byte*>(entry.block.data);
        const auto* left = payload - entry.redzone_size;
        const auto* right = payload + entry.block.size;
        return std::all_of(left, left + entry.redzone_size, [&](std::byte byte) {
                   return byte == options_.redzone_poison;
               }) &&
               std::all_of(right, right + entry.redzone_size, [&](std::byte byte) {
                   return byte == options_.redzone_poison;
               });
    }

    void release_raw_storage(raw_storage storage) noexcept {
        if (storage.guard_pages) {
            static_cast<void>(page_source_.release(storage.reserved_span));
        } else {
            static_cast<void>(upstream_.deallocate(storage.raw_block));
        }
    }

    void count_failed_allocation() noexcept {
        std::lock_guard lock{mutex_};
        auto failed = checked_add(failed_allocations_, 1U);
        failed_allocations_ = failed ? *failed : failed_allocations_;
    }

    void count_guard_request() noexcept {
        std::lock_guard lock{mutex_};
        auto count = checked_add(guard_page_request_count_, 1U);
        guard_page_request_count_ = count ? *count : guard_page_request_count_;
    }

    void count_guard_allocation() noexcept {
        std::lock_guard lock{mutex_};
        auto count = checked_add(guard_page_allocation_count_, 1U);
        guard_page_allocation_count_ = count ? *count : guard_page_allocation_count_;
    }

    void count_guard_fallback() noexcept {
        std::lock_guard lock{mutex_};
        auto count = checked_add(guard_page_fallback_count_, 1U);
        guard_page_fallback_count_ = count ? *count : guard_page_fallback_count_;
    }

    resource_ref upstream_;
    debug_resource_options options_{};
    mutable std::mutex mutex_{};
    mutable os_page_source page_source_{};
    std::unordered_map<void*, record> records_{};
    std::size_t active_bytes_{};
    std::size_t reserved_bytes_{};
    std::size_t active_allocations_{};
    std::size_t total_allocations_{};
    std::size_t total_deallocations_{};
    std::size_t failed_allocations_{};
    std::size_t redzone_failure_count_{};
    std::size_t guard_page_request_count_{};
    std::size_t guard_page_allocation_count_{};
    std::size_t guard_page_fallback_count_{};
};

} // namespace voris::mem
