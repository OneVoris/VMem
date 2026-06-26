#include <voris/mem/debug_resource.hpp>
#include <voris/mem/slab_resource.hpp>
#include <voris/mem/system_resource.hpp>

#undef NDEBUG

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>

#if defined(NDEBUG)
#error "VMem assert-style tests require assertions enabled"
#endif

namespace
{

[[nodiscard]] bool os_page_source_supports_guard_pages() noexcept
{
    voris::mem::os_page_source pages;
    auto page_size = pages.page_size();
    if (!page_size)
    {
        return false;
    }
    auto reserved = pages.reserve(*page_size * 3U);
    if (!reserved)
    {
        return false;
    }
    auto committed = pages.commit(voris::mem::page_span{reserved->data, *page_size});
    auto released = pages.release(*reserved);
    return committed && released;
}

class bump_resource
{
  public:
    [[nodiscard]] std::expected<voris::mem::allocation, voris::mem::errc> allocate(
        const voris::mem::allocation_request &request) noexcept
    {
        if (!voris::mem::is_power_of_two(request.alignment))
        {
            return std::unexpected(voris::mem::errc::invalid_alignment);
        }
        const auto base = reinterpret_cast<std::uintptr_t>(storage_.data()) + cursor_;
        const auto aligned = (base + request.alignment - 1U) & ~(request.alignment - 1U);
        const auto offset = aligned - reinterpret_cast<std::uintptr_t>(storage_.data());
        const auto end = offset + request.size;
        if (end > storage_.size())
        {
            return std::unexpected(voris::mem::errc::out_of_memory);
        }
        cursor_ = end;
        ++active_allocations_;
        return voris::mem::allocation{
            reinterpret_cast<void *>(aligned),
            request.size,
            request.alignment,
        };
    }

    [[nodiscard]] std::expected<void, voris::mem::errc> deallocate(voris::mem::allocation block) noexcept
    {
        last_deallocated_ = block;
        ++deallocation_count_;
        if (active_allocations_ > 0U)
        {
            --active_allocations_;
        }
        return {};
    }

    [[nodiscard]] voris::mem::resource_traits traits() const noexcept
    {
        return voris::mem::resource_traits{
            .name = "bump_resource",
            .ownership = voris::mem::resource_ownership::caller_owned,
            .thread_safety = voris::mem::resource_thread_safety::externally_synchronized,
            .supports_remote_deallocate = false,
        };
    }

    [[nodiscard]] voris::mem::usage_snapshot usage() const noexcept
    {
        return voris::mem::usage_snapshot{.active_allocations = active_allocations_};
    }

    [[nodiscard]] std::size_t deallocation_count() const noexcept
    {
        return deallocation_count_;
    }

    [[nodiscard]] voris::mem::allocation last_deallocated() const noexcept
    {
        return last_deallocated_;
    }

  private:
    alignas(64) std::array<std::byte, 8192> storage_{};
    std::size_t cursor_{};
    std::size_t active_allocations_{};
    std::size_t deallocation_count_{};
    voris::mem::allocation last_deallocated_{};
};

class reusable_slot_resource
{
  public:
    [[nodiscard]] std::expected<voris::mem::allocation, voris::mem::errc> allocate(
        const voris::mem::allocation_request &request) noexcept
    {
        if (!voris::mem::is_power_of_two(request.alignment))
        {
            return std::unexpected(voris::mem::errc::invalid_alignment);
        }
        if (allocated_ || request.size > storage_.size() || request.alignment > 64U)
        {
            return std::unexpected(voris::mem::errc::out_of_memory);
        }
        allocated_ = true;
        return voris::mem::allocation{storage_.data(), request.size, request.alignment};
    }

    [[nodiscard]] std::expected<void, voris::mem::errc> deallocate(voris::mem::allocation) noexcept
    {
        allocated_ = false;
        return {};
    }

    [[nodiscard]] voris::mem::resource_traits traits() const noexcept
    {
        return voris::mem::resource_traits{
            .name = "reusable_slot_resource",
            .ownership = voris::mem::resource_ownership::caller_owned,
            .thread_safety = voris::mem::resource_thread_safety::externally_synchronized,
            .supports_remote_deallocate = false,
        };
    }

    [[nodiscard]] voris::mem::usage_snapshot usage() const noexcept
    {
        return voris::mem::usage_snapshot{.active_allocations = allocated_ ? 1U : 0U};
    }

  private:
    alignas(64) std::array<std::byte, 2048> storage_{};
    bool allocated_{};
};

class failing_deallocate_resource
{
  public:
    void fail_next_deallocate(bool enabled) noexcept
    {
        fail_deallocate_ = enabled;
    }

    [[nodiscard]] std::expected<voris::mem::allocation, voris::mem::errc> allocate(
        const voris::mem::allocation_request &request) noexcept
    {
        return system_.allocate(request);
    }

    [[nodiscard]] std::expected<void, voris::mem::errc> deallocate(voris::mem::allocation block) noexcept
    {
        ++deallocation_attempts_;
        if (fail_deallocate_)
        {
            fail_deallocate_ = false;
            return std::unexpected(voris::mem::errc::wrong_owner);
        }
        return system_.deallocate(block);
    }

    [[nodiscard]] voris::mem::resource_traits traits() const noexcept
    {
        return voris::mem::resource_traits{
            .name = "failing_deallocate_resource",
            .ownership = voris::mem::resource_ownership::caller_owned,
            .thread_safety = voris::mem::resource_thread_safety::thread_safe,
            .supports_remote_deallocate = true,
        };
    }

    [[nodiscard]] voris::mem::usage_snapshot usage() const noexcept
    {
        return system_.usage();
    }

    [[nodiscard]] std::size_t deallocation_attempts() const noexcept
    {
        return deallocation_attempts_;
    }

  private:
    voris::mem::system_resource system_;
    bool fail_deallocate_{};
    std::size_t deallocation_attempts_{};
};

void test_debug_resource_poisons_payload_and_checks_redzones()
{
    bump_resource upstream;
    voris::mem::debug_resource debug{
        voris::mem::resource_ref{upstream},
        voris::mem::debug_resource_options{
            .redzone_size = 16U,
            .poison_on_allocate = true,
            .poison_on_free = true,
            .allocate_poison = std::byte{0xA5},
            .free_poison = std::byte{0x5A},
            .redzone_poison = std::byte{0xFD},
            .preserve_sanitizer_diagnostics = false,
        },
    };

    auto block = debug.allocate(voris::mem::make_allocation_request(32U, 16U));
    assert(block);
    auto *payload = static_cast<std::byte *>(block->data);
    assert(std::all_of(payload, payload + block->size, [](std::byte byte) { return byte == std::byte{0xA5}; }));

    payload[block->size] = std::byte{0x00};
    auto corrupted = debug.deallocate(*block);
    assert(!corrupted);
    assert(corrupted.error() == voris::mem::errc::wrong_owner);
    assert(debug.usage().active_allocations == 1U);

    auto clean = debug.allocate(voris::mem::make_allocation_request(24U, 8U));
    assert(clean);
    auto *clean_payload = static_cast<std::byte *>(clean->data);
    assert(debug.deallocate(*clean));
    assert(std::all_of(clean_payload, clean_payload + clean->size,
                       [](std::byte byte) { return byte == std::byte{0x5A}; }));
    assert(upstream.deallocation_count() == 1U);
}

void test_failed_upstream_release_preserves_debug_allocation_state()
{
    failing_deallocate_resource upstream;
    voris::mem::debug_resource debug{
        voris::mem::resource_ref{upstream},
        voris::mem::debug_resource_options{
            .redzone_size = 16U,
            .poison_on_free = true,
            .free_poison = std::byte{0x5A},
            .preserve_sanitizer_diagnostics = false,
        },
    };

    auto block = debug.allocate(voris::mem::make_allocation_request(32U, 16U));
    assert(block);
    auto *payload = static_cast<std::byte *>(block->data);
    std::fill_n(payload, block->size, std::byte{0x11});

    upstream.fail_next_deallocate(true);
    auto failed = debug.deallocate(*block);
    assert(!failed);
    assert(failed.error() == voris::mem::errc::wrong_owner);
    assert(debug.usage().active_allocations == 1U);
    assert(debug.leak_snapshot().records.size() == 1U);
    assert(std::all_of(payload, payload + block->size, [](std::byte byte) { return byte == std::byte{0x11}; }));

    payload[0] = std::byte{0x22};
    assert(debug.deallocate(*block));
    assert(debug.usage().active_allocations == 0U);
    assert(upstream.deallocation_attempts() == 2U);
}

void test_debug_resource_detects_double_free_wrong_resource_and_wrong_generation()
{
    voris::mem::system_resource system;
    voris::mem::debug_resource first{voris::mem::resource_ref{system}};
    voris::mem::debug_resource second{voris::mem::resource_ref{system}};

    auto block = first.allocate(voris::mem::make_allocation_request(64U, 16U));
    assert(block);
    assert(first.deallocate(*block));
    auto double_free = first.deallocate(*block);
    assert(!double_free);
    assert(double_free.error() == voris::mem::errc::wrong_owner);

    auto foreign = second.allocate(voris::mem::make_allocation_request(32U, 16U));
    assert(foreign);
    auto wrong_resource = first.deallocate(*foreign);
    assert(!wrong_resource);
    assert(wrong_resource.error() == voris::mem::errc::wrong_owner);
    assert(second.deallocate(*foreign));

    reusable_slot_resource reusable;
    voris::mem::debug_resource generation_debug{voris::mem::resource_ref{reusable}};
    auto descriptor = generation_debug.allocate_block(voris::mem::make_allocation_request(48U, 16U));
    assert(descriptor);
    auto stale = *descriptor;
    assert(generation_debug.deallocate_block(*descriptor));
    auto current = generation_debug.allocate_block(voris::mem::make_allocation_request(48U, 16U));
    assert(current);
    assert(current->block.data == stale.block.data);
    auto stale_release = generation_debug.deallocate_block(stale);
    assert(!stale_release);
    assert(stale_release.error() == voris::mem::errc::wrong_owner);
    assert(generation_debug.deallocate_block(*current));
}

void test_guard_page_requests_have_explicit_fallback_accounting()
{
    voris::mem::system_resource system;
    voris::mem::debug_resource debug{
        voris::mem::resource_ref{system},
        voris::mem::debug_resource_options{
            .redzone_size = 16U,
            .guard_pages_for_large_allocations = true,
            .guard_page_threshold = 1024U,
        },
    };

    auto block = debug.allocate(voris::mem::make_allocation_request(2048U, 64U));
    assert(block);
    auto snapshot = debug.debug_snapshot();
    assert(snapshot.guard_page_request_count == 1U);
    if (os_page_source_supports_guard_pages())
    {
        assert(snapshot.guard_page_allocation_count == 1U);
        assert(snapshot.guard_page_fallback_count == 0U);
    }
    else
    {
        assert(snapshot.guard_page_allocation_count == 0U);
        assert(snapshot.guard_page_fallback_count == 1U);
    }
    assert(debug.deallocate(*block));
}

void test_leak_snapshots_diff_without_process_exit_hook()
{
    voris::mem::system_resource system;
    voris::mem::debug_resource debug{voris::mem::resource_ref{system}};

    auto first = debug.allocate(voris::mem::make_allocation_request(32U, 16U));
    assert(first);
    auto before = debug.leak_snapshot();

    auto second = debug.allocate(voris::mem::make_allocation_request(64U, 16U));
    assert(second);
    assert(debug.deallocate(*first));
    auto after = debug.leak_snapshot();

    auto diff = voris::mem::diff_leak_snapshots(before, after);
    assert(diff.added.size() == 1U);
    assert(diff.added[0].block.data == second->data);
    assert(diff.removed.size() == 1U);
    assert(diff.removed[0].block.data == first->data);
    assert(diff.added_bytes == 64U);
    assert(diff.removed_bytes == 32U);

    assert(debug.deallocate(*second));
    assert(debug.leak_snapshot().records.empty());
}

void test_slab_size_class_snapshots_report_active_free_remote_and_fragmentation()
{
    voris::mem::system_resource system;
    voris::mem::slab_resource slab{
        voris::mem::resource_ref{system},
        voris::mem::slab_options{.slab_size = 4096U, .remote_queue_capacity = 4U},
    };

    auto block = slab.allocate(voris::mem::make_allocation_request(17U, 8U));
    assert(block);
    auto snapshots = slab.size_class_snapshots();
    auto class32 = std::find_if(snapshots.begin(), snapshots.end(),
                                [](const auto &snapshot) { return snapshot.block_size == 32U; });
    assert(class32 != snapshots.end());
    assert(class32->active_count == 1U);
    assert(class32->free_count > 0U);
    assert(class32->remote_count == 0U);
    assert(class32->fragmentation_bytes >= class32->free_bytes);

    assert(slab.remote_deallocate(*block));
    snapshots = slab.size_class_snapshots();
    class32 = std::find_if(snapshots.begin(), snapshots.end(),
                           [](const auto &snapshot) { return snapshot.block_size == 32U; });
    assert(class32 != snapshots.end());
    assert(class32->remote_count == 1U);
    assert(class32->active_count == 0U);
    assert(class32->active_bytes == 0U);

    assert(slab.drain_remote_frees() == 1U);
    snapshots = slab.size_class_snapshots();
    class32 = std::find_if(snapshots.begin(), snapshots.end(),
                           [](const auto &snapshot) { return snapshot.block_size == 32U; });
    assert(class32 != snapshots.end());
    assert(class32->active_count == 0U);
    assert(class32->remote_count == 0U);
}

} // namespace

int main()
{
    test_debug_resource_poisons_payload_and_checks_redzones();
    test_failed_upstream_release_preserves_debug_allocation_state();
    test_debug_resource_detects_double_free_wrong_resource_and_wrong_generation();
    test_guard_page_requests_have_explicit_fallback_accounting();
    test_leak_snapshots_diff_without_process_exit_hook();
    test_slab_size_class_snapshots_report_active_free_remote_and_fragmentation();
    return 0;
}
