#include <voris/mem/checked_math.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>

#include <cassert>
#include <cstddef>
#include <expected>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {

class recording_resource {
public:
    std::expected<voris::mem::allocation, voris::mem::errc>
    allocate(const voris::mem::allocation_request& request) noexcept {
        last_request_ = request;
        snapshot_.active_bytes += request.size;
        snapshot_.active_allocations += 1;
        snapshot_.total_allocations += 1;
        return voris::mem::allocation{storage_, request.size, request.alignment};
    }

    std::expected<void, voris::mem::errc>
    deallocate(voris::mem::allocation block) noexcept {
        last_deallocation_ = block;
        snapshot_.active_bytes -= block.size;
        snapshot_.active_allocations -= 1;
        snapshot_.total_deallocations += 1;
        return {};
    }

    [[nodiscard]] voris::mem::resource_traits traits() const noexcept {
        return voris::mem::resource_traits{
            .name = "recording_resource",
            .ownership = voris::mem::resource_ownership::caller_owned,
            .thread_safety = voris::mem::resource_thread_safety::externally_synchronized,
            .supports_remote_deallocate = false,
        };
    }

    [[nodiscard]] voris::mem::usage_snapshot usage() const noexcept {
        return snapshot_;
    }

    [[nodiscard]] const voris::mem::allocation_request& last_request() const noexcept {
        return last_request_;
    }

    [[nodiscard]] voris::mem::allocation last_deallocation() const noexcept {
        return last_deallocation_;
    }

private:
    alignas(std::max_align_t) std::byte storage_[64]{};
    voris::mem::allocation_request last_request_{};
    voris::mem::allocation last_deallocation_{};
    voris::mem::usage_snapshot snapshot_{};
};

} // namespace

static_assert(std::is_trivially_copyable_v<voris::mem::resource_ref>);
static_assert(std::is_nothrow_copy_constructible_v<voris::mem::resource_ref>);

int main() {
    using voris::mem::align_up;
    using voris::mem::checked_add;
    using voris::mem::checked_mul;
    using voris::mem::errc;
    using voris::mem::is_power_of_two;

    assert(is_power_of_two(1));
    assert(is_power_of_two(64));
    assert(!is_power_of_two(0));
    assert(!is_power_of_two(3));

    auto aligned = align_up(17, 8);
    assert(aligned);
    assert(*aligned == 24);

    auto zero_aligned = align_up(0, 16);
    assert(zero_aligned);
    assert(*zero_aligned == 0);

    auto invalid_alignment = align_up(8, 3);
    assert(!invalid_alignment);
    assert(invalid_alignment.error() == errc::invalid_alignment);

    auto overflow_alignment = align_up(std::numeric_limits<std::size_t>::max(), 2);
    assert(!overflow_alignment);
    assert(overflow_alignment.error() == errc::size_overflow);

    assert(checked_add(10, 20).value() == 30);
    assert(checked_add(std::numeric_limits<std::size_t>::max(), 1).error() == errc::size_overflow);
    assert(checked_mul(6, 7).value() == 42);
    assert(checked_mul(std::numeric_limits<std::size_t>::max(), 2).error() == errc::size_overflow);

    constexpr voris::mem::memory_tag tag{"contracts"};
    auto request = voris::mem::make_allocation_request(32, alignof(std::max_align_t), tag);
    assert(request.size == 32);
    assert(request.alignment == alignof(std::max_align_t));
    assert(request.tag.name == std::string_view{"contracts"});
    assert(request.location.line() != 0);

    recording_resource resource;
    voris::mem::resource_ref ref{resource};
    voris::mem::resource_ref copied = ref;

    auto block = copied.allocate(16, alignof(std::max_align_t), tag);
    assert(block);
    assert(block->size == 16);
    assert(resource.last_request().tag.name == std::string_view{"contracts"});
    assert(copied.traits().thread_safety == voris::mem::resource_thread_safety::externally_synchronized);
    assert(copied.traits().ownership == voris::mem::resource_ownership::caller_owned);
    assert(copied.usage().active_bytes == 16);
    assert(copied.usage().active_allocations == 1);

    auto deallocated = ref.deallocate(*block);
    assert(deallocated);
    assert(resource.last_deallocation().size == 16);
    assert(ref.usage().active_bytes == 0);
    assert(ref.usage().total_deallocations == 1);

    return 0;
}
