#include <voris/mem/counting_resource.hpp>
#include <voris/mem/fault_injection_resource.hpp>
#include <voris/mem/page_chunk.hpp>
#include <voris/mem/page_source.hpp>
#include <voris/mem/system_resource.hpp>

#undef NDEBUG

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>
#include <vector>

#if defined(NDEBUG)
#error "VMem assert-style tests require assertions enabled"
#endif

namespace
{

class fake_page_source
{
  public:
    explicit fake_page_source(std::size_t page_size = 4096) : page_size_(page_size)
    {
    }

    std::expected<std::size_t, voris::mem::errc> page_size() const noexcept
    {
        return page_size_;
    }

    std::expected<voris::mem::page_span, voris::mem::errc> reserve(std::size_t size) noexcept
    {
        if (fail_reserve_)
        {
            return std::unexpected(voris::mem::errc::out_of_memory);
        }
        last_reserved_size_ = size;
        reserved_ += 1;
        storage_.resize(size);
        return voris::mem::page_span{storage_.data(), size};
    }

    std::expected<void, voris::mem::errc> commit(voris::mem::page_span span) noexcept
    {
        last_committed_size_ = span.size;
        if (fail_commit_)
        {
            return std::unexpected(voris::mem::errc::out_of_memory);
        }
        committed_ += 1;
        return {};
    }

    std::expected<void, voris::mem::errc> decommit(voris::mem::page_span span) noexcept
    {
        last_decommitted_size_ = span.size;
        decommitted_ += 1;
        return {};
    }

    std::expected<void, voris::mem::errc> release(voris::mem::page_span span) noexcept
    {
        last_released_size_ = span.size;
        released_ += 1;
        storage_.clear();
        return {};
    }

    bool fail_reserve_{};
    bool fail_commit_{};
    std::size_t last_reserved_size_{};
    std::size_t last_committed_size_{};
    std::size_t last_decommitted_size_{};
    std::size_t last_released_size_{};
    std::size_t reserved_{};
    std::size_t committed_{};
    std::size_t decommitted_{};
    std::size_t released_{};

  private:
    std::size_t page_size_{};
    std::vector<std::byte> storage_{};
};

class fake_accounting_resource
{
  public:
    std::expected<voris::mem::allocation, voris::mem::errc> allocate(
        const voris::mem::allocation_request &request) noexcept
    {
        allocation_calls_ += 1;
        return voris::mem::allocation{fake_pointer_, request.size, request.alignment};
    }

    std::expected<void, voris::mem::errc> deallocate(voris::mem::allocation block) noexcept
    {
        last_deallocated_ = block;
        deallocation_calls_ += 1;
        return {};
    }

    [[nodiscard]] voris::mem::resource_traits traits() const noexcept
    {
        return voris::mem::resource_traits{
            .name = "fake_accounting_resource",
            .ownership = voris::mem::resource_ownership::caller_owned,
            .thread_safety = voris::mem::resource_thread_safety::externally_synchronized,
            .supports_remote_deallocate = false,
        };
    }

    [[nodiscard]] voris::mem::usage_snapshot usage() const noexcept
    {
        return {};
    }

    void *fake_pointer_{reinterpret_cast<void *>(alignof(std::max_align_t))};
    voris::mem::allocation last_deallocated_{};
    std::size_t allocation_calls_{};
    std::size_t deallocation_calls_{};
};

} // namespace

int main()
{
    using voris::mem::allocation;
    using voris::mem::counting_resource;
    using voris::mem::errc;
    using voris::mem::fault_injection_options;
    using voris::mem::fault_injection_resource;
    using voris::mem::make_allocation_request;
    using voris::mem::memory_tag;
    using voris::mem::os_page_source;
    using voris::mem::page_allocation_kind;
    using voris::mem::page_chunk_options;
    using voris::mem::resource_ref;
    using voris::mem::system_resource;

    system_resource system;
    assert(system.traits().thread_safety == voris::mem::resource_thread_safety::thread_safe);

    auto zero = system.allocate(make_allocation_request(0, alignof(std::max_align_t)));
    assert(zero);
    assert(zero->data == nullptr);
    assert(zero->size == 0);

    auto bad_alignment = system.allocate(make_allocation_request(16, 3));
    assert(!bad_alignment);
    assert(bad_alignment.error() == errc::invalid_alignment);

    auto impossible_size = system.allocate(make_allocation_request(std::numeric_limits<std::size_t>::max(), 2));
    assert(!impossible_size);
    assert(impossible_size.error() == errc::size_overflow);

    auto aligned = system.allocate(make_allocation_request(64, 64, memory_tag{"aligned"}));
    assert(aligned);
    assert(reinterpret_cast<std::uintptr_t>(aligned->data) % 64 == 0);
    assert(system.usage().active_bytes >= 64);

    const auto usage_before_null_deallocate = system.usage();
    auto null_nonzero = system.deallocate(allocation{nullptr, aligned->size, aligned->alignment});
    assert(!null_nonzero);
    assert(null_nonzero.error() == errc::wrong_owner);
    assert(system.usage().active_bytes == usage_before_null_deallocate.active_bytes);
    assert(system.usage().active_allocations == usage_before_null_deallocate.active_allocations);

    auto released = system.deallocate(*aligned);
    assert(released);

    auto double_free = system.deallocate(*aligned);
    assert(!double_free);
    assert(double_free.error() == errc::wrong_owner);

    system_resource foreign_owner;
    auto owned = system.allocate(make_allocation_request(48, alignof(std::max_align_t)));
    auto foreign = foreign_owner.allocate(make_allocation_request(48, alignof(std::max_align_t)));
    assert(owned);
    assert(foreign);
    const auto system_usage_before_foreign = system.usage();
    auto foreign_release = system.deallocate(*foreign);
    assert(!foreign_release);
    assert(foreign_release.error() == errc::wrong_owner);
    assert(system.usage().active_bytes == system_usage_before_foreign.active_bytes);
    assert(system.usage().active_allocations == system_usage_before_foreign.active_allocations);
    assert(system.deallocate(*owned));
    assert(foreign_owner.deallocate(*foreign));

    auto sized = system.allocate(make_allocation_request(96, 32));
    assert(sized);
    auto wrong_size = system.deallocate(allocation{sized->data, 32, sized->alignment});
    assert(!wrong_size);
    assert(wrong_size.error() == errc::wrong_owner);
    auto wrong_alignment = system.deallocate(allocation{sized->data, sized->size, 64});
    assert(!wrong_alignment);
    assert(wrong_alignment.error() == errc::wrong_owner);
    assert(system.deallocate(*sized));

    fake_page_source chunk_source;
    voris::mem::basic_page_chunk_manager<fake_page_source> chunks{
        chunk_source,
        page_chunk_options{.chunk_size = 16 * 1024, .direct_threshold = 64 * 1024},
    };

    auto normal_chunk = chunks.allocate_span(1024);
    assert(normal_chunk);
    assert(normal_chunk->kind == page_allocation_kind::chunk);
    assert(normal_chunk->size == 16 * 1024);
    assert(chunk_source.last_reserved_size_ == 16 * 1024);
    assert(chunks.release_span(*normal_chunk));
    assert(chunk_source.last_released_size_ == 16 * 1024);

    auto direct_chunk = chunks.allocate_span(80 * 1024);
    assert(direct_chunk);
    assert(direct_chunk->kind == page_allocation_kind::direct);
    assert(direct_chunk->size == 80 * 1024);
    assert(chunks.release_span(*direct_chunk));

    auto huge_chunk = chunks.allocate_span(std::numeric_limits<std::size_t>::max());
    assert(!huge_chunk);
    assert(huge_chunk.error() == errc::size_overflow);

    fake_page_source reserve_failure;
    reserve_failure.fail_reserve_ = true;
    voris::mem::basic_page_chunk_manager<fake_page_source> reserve_chunks{reserve_failure};
    auto reserve_failed = reserve_chunks.allocate_span(4096);
    assert(!reserve_failed);
    assert(reserve_failed.error() == errc::out_of_memory);
    assert(reserve_failure.released_ == 0);

    fake_page_source commit_failure;
    commit_failure.fail_commit_ = true;
    voris::mem::basic_page_chunk_manager<fake_page_source> commit_chunks{commit_failure};
    auto commit_failed = commit_chunks.allocate_span(4096);
    assert(!commit_failed);
    assert(commit_failed.error() == errc::out_of_memory);
    assert(commit_failure.released_ == 1);

    system_resource counted_backend;
    counting_resource counted{resource_ref{counted_backend}};
    resource_ref counted_ref{counted};
    static_cast<void>(counted_ref);
    auto counted_block = counted.allocate(make_allocation_request(32, alignof(std::max_align_t)));
    assert(counted_block);
    auto counted_usage = counted.usage();
    assert(counted_usage.active_bytes == 32);
    assert(counted_usage.reserved_bytes == 32);
    assert(counted_usage.total_allocations == 1);
    assert(counted.deallocate(*counted_block));
    assert(counted.usage().active_bytes == 0);
    assert(counted.usage().total_deallocations == 1);
    auto counted_zero = counted.allocate(make_allocation_request(0, alignof(std::max_align_t)));
    assert(counted_zero);
    assert(counted.deallocate(*counted_zero));
    assert(counted.usage().active_allocations == 0);
    assert(!counted.allocate(make_allocation_request(16, 3)));
    assert(counted.usage().failed_allocations == 1);

    fake_accounting_resource counted_fake;
    counting_resource counted_overflow{resource_ref{counted_fake}};
    auto counted_huge = counted_overflow.allocate(
        make_allocation_request(std::numeric_limits<std::size_t>::max() - 4, alignof(std::max_align_t)));
    assert(counted_huge);
    auto counted_overflowed = counted_overflow.allocate(make_allocation_request(8, alignof(std::max_align_t)));
    assert(!counted_overflowed);
    assert(counted_overflowed.error() == errc::size_overflow);
    assert(counted_overflow.usage().active_bytes == std::numeric_limits<std::size_t>::max() - 4);
    assert(counted_fake.allocation_calls_ == 1);

    fake_accounting_resource counted_underflow_fake;
    counting_resource counted_underflow{resource_ref{counted_underflow_fake}};
    auto counted_wrong_owner = counted_underflow.deallocate(allocation{counted_underflow_fake.fake_pointer_, 8, 8});
    assert(!counted_wrong_owner);
    assert(counted_wrong_owner.error() == errc::wrong_owner);
    assert(counted_underflow.usage().active_bytes == 0);
    assert(counted_underflow_fake.deallocation_calls_ == 0);

    fake_accounting_resource counted_shape_fake;
    counting_resource counted_shape{resource_ref{counted_shape_fake}};
    auto counted_shape_block = counted_shape.allocate(make_allocation_request(16, alignof(std::max_align_t)));
    assert(counted_shape_block);
    const auto counted_shape_usage = counted_shape.usage();
    auto counted_null_nonzero =
        counted_shape.deallocate(allocation{nullptr, counted_shape_block->size, counted_shape_block->alignment});
    assert(!counted_null_nonzero);
    assert(counted_null_nonzero.error() == errc::wrong_owner);
    assert(counted_shape.usage().active_bytes == counted_shape_usage.active_bytes);
    assert(counted_shape.usage().active_allocations == counted_shape_usage.active_allocations);
    assert(counted_shape_fake.deallocation_calls_ == 0);
    auto counted_nonnull_zero =
        counted_shape.deallocate(allocation{counted_shape_block->data, 0, counted_shape_block->alignment});
    assert(!counted_nonnull_zero);
    assert(counted_nonnull_zero.error() == errc::wrong_owner);
    assert(counted_shape.usage().active_bytes == counted_shape_usage.active_bytes);
    assert(counted_shape_fake.deallocation_calls_ == 0);
    assert(counted_shape.deallocate(*counted_shape_block));

    system_resource fault_backend;
    fault_injection_resource fail_second{
        resource_ref{fault_backend},
        fault_injection_options{.fail_on_allocation_call = 2},
    };
    resource_ref fault_ref{fail_second};
    static_cast<void>(fault_ref);
    auto first = fail_second.allocate(make_allocation_request(8, alignof(std::max_align_t)));
    assert(first);
    auto second = fail_second.allocate(make_allocation_request(8, alignof(std::max_align_t)));
    assert(!second);
    assert(second.error() == errc::out_of_memory);
    assert(fail_second.usage().failed_allocations == 1);
    assert(fail_second.deallocate(*first));
    auto fault_zero = fail_second.allocate(make_allocation_request(0, alignof(std::max_align_t)));
    assert(fault_zero);
    assert(fail_second.deallocate(*fault_zero));
    assert(fail_second.usage().active_allocations == 0);

    system_resource byte_backend;
    fault_injection_resource fail_after_bytes{
        resource_ref{byte_backend},
        fault_injection_options{.fail_after_requested_bytes = 10},
    };
    auto under_byte_limit = fail_after_bytes.allocate(make_allocation_request(6, alignof(std::max_align_t)));
    assert(under_byte_limit);
    auto over_bytes = fail_after_bytes.allocate(make_allocation_request(5, alignof(std::max_align_t)));
    assert(!over_bytes);
    assert(over_bytes.error() == errc::out_of_memory);
    assert(fail_after_bytes.deallocate(*under_byte_limit));

    constexpr memory_tag blocked_tag{"blocked"};
    system_resource tag_backend;
    fault_injection_resource fail_tag{
        resource_ref{tag_backend},
        fault_injection_options{.fail_tag = blocked_tag},
    };
    auto blocked = fail_tag.allocate(make_allocation_request(8, alignof(std::max_align_t), blocked_tag));
    assert(!blocked);
    assert(blocked.error() == errc::out_of_memory);

    fake_accounting_resource fault_fake;
    fault_injection_resource fault_overflow{resource_ref{fault_fake}};
    auto fault_huge = fault_overflow.allocate(
        make_allocation_request(std::numeric_limits<std::size_t>::max() - 4, alignof(std::max_align_t)));
    assert(fault_huge);
    auto fault_overflowed = fault_overflow.allocate(make_allocation_request(8, alignof(std::max_align_t)));
    assert(!fault_overflowed);
    assert(fault_overflowed.error() == errc::size_overflow);
    assert(fault_overflow.usage().active_bytes == std::numeric_limits<std::size_t>::max() - 4);
    assert(fault_fake.allocation_calls_ == 1);

    fake_accounting_resource fault_underflow_fake;
    fault_injection_resource fault_underflow{resource_ref{fault_underflow_fake}};
    auto fault_wrong_owner = fault_underflow.deallocate(allocation{fault_underflow_fake.fake_pointer_, 8, 8});
    assert(!fault_wrong_owner);
    assert(fault_wrong_owner.error() == errc::wrong_owner);
    assert(fault_underflow.usage().active_bytes == 0);
    assert(fault_underflow_fake.deallocation_calls_ == 0);

    fake_accounting_resource fault_shape_fake;
    fault_injection_resource fault_shape{resource_ref{fault_shape_fake}};
    auto fault_shape_block = fault_shape.allocate(make_allocation_request(16, alignof(std::max_align_t)));
    assert(fault_shape_block);
    const auto fault_shape_usage = fault_shape.usage();
    auto fault_null_nonzero =
        fault_shape.deallocate(allocation{nullptr, fault_shape_block->size, fault_shape_block->alignment});
    assert(!fault_null_nonzero);
    assert(fault_null_nonzero.error() == errc::wrong_owner);
    assert(fault_shape.usage().active_bytes == fault_shape_usage.active_bytes);
    assert(fault_shape.usage().active_allocations == fault_shape_usage.active_allocations);
    assert(fault_shape_fake.deallocation_calls_ == 0);
    auto fault_nonnull_zero =
        fault_shape.deallocate(allocation{fault_shape_block->data, 0, fault_shape_block->alignment});
    assert(!fault_nonnull_zero);
    assert(fault_nonnull_zero.error() == errc::wrong_owner);
    assert(fault_shape.usage().active_bytes == fault_shape_usage.active_bytes);
    assert(fault_shape_fake.deallocation_calls_ == 0);
    assert(fault_shape.deallocate(*fault_shape_block));

    auto system_wrong_owner = system.deallocate(allocation{nullptr, 8, 8});
    assert(!system_wrong_owner);
    assert(system_wrong_owner.error() == errc::wrong_owner);
    assert(system.usage().active_bytes == 0);

    os_page_source pages;
    auto page_size = pages.page_size();
    assert(page_size);
    assert(*page_size != 0);

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    auto span = pages.reserve(*page_size);
    assert(span);
    assert(span->size == *page_size);
    assert(pages.commit(*span));
    auto *page_bytes = static_cast<volatile unsigned char *>(span->data);
    page_bytes[0] = 0xC3U;
    assert(pages.decommit(*span));
    assert(pages.release(*span));
#else
    auto span = pages.reserve(*page_size);
    assert(!span);
    assert(span.error() == errc::unsupported_platform);
#endif

    static_cast<void>(allocation{});
    return 0;
}
