#include <voris/mem/buffer.hpp>
#include <voris/mem/buffer_chain.hpp>
#include <voris/mem/buffer_parser.hpp>
#include <voris/mem/shared_buffer.hpp>
#include <voris/mem/unique_buffer.hpp>

#include "buffer_gather_adapter.hpp"

#undef NDEBUG

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <vector>

#if defined(NDEBUG)
#    error "VMem assert-style tests require assertions enabled"
#endif

namespace {

class recording_resource {
public:
    explicit recording_resource(voris::mem::resource_thread_safety thread_safety =
                                    voris::mem::resource_thread_safety::thread_safe,
                                bool supports_remote_deallocate = true) noexcept
        : thread_safety_(thread_safety),
          supports_remote_deallocate_(supports_remote_deallocate) {}

    [[nodiscard]] std::expected<voris::mem::allocation, voris::mem::errc>
    allocate(const voris::mem::allocation_request& request) noexcept {
        if ((request.alignment == 0U) ||
            ((request.alignment & (request.alignment - 1U)) != 0U)) {
            return std::unexpected(voris::mem::errc::invalid_alignment);
        }
        if (request.size == 0U) {
            return voris::mem::allocation{nullptr, 0U, request.alignment};
        }
        void* pointer{};
        try {
            pointer = ::operator new(request.size, std::align_val_t{request.alignment});
        } catch (...) {
            return std::unexpected(voris::mem::errc::out_of_memory);
        }
        std::lock_guard guard{mutex_};
        active_.push_back(voris::mem::allocation{pointer, request.size, request.alignment});
        ++allocation_calls_;
        return active_.back();
    }

    [[nodiscard]] std::expected<void, voris::mem::errc>
    deallocate(voris::mem::allocation block) noexcept {
        if (block.data == nullptr && block.size == 0U) {
            return {};
        }
        std::lock_guard guard{mutex_};
        if (fail_deallocate_) {
            return std::unexpected(voris::mem::errc::wrong_owner);
        }
        const auto found = std::find_if(active_.begin(), active_.end(), [&](const auto& item) {
            return item.data == block.data && item.size == block.size &&
                   item.alignment == block.alignment;
        });
        if (found == active_.end()) {
            return std::unexpected(voris::mem::errc::wrong_owner);
        }
        ::operator delete(block.data, std::align_val_t{block.alignment});
        active_.erase(found);
        ++deallocation_calls_;
        last_deallocate_thread_ = std::this_thread::get_id();
        return {};
    }

    [[nodiscard]] std::expected<void, voris::mem::errc>
    remote_deallocate(voris::mem::allocation block) noexcept {
        {
            std::lock_guard guard{mutex_};
            ++remote_deallocation_calls_;
        }
        return deallocate(block);
    }

    [[nodiscard]] voris::mem::resource_traits traits() const noexcept {
        return voris::mem::resource_traits{
            .name = "recording_resource",
            .ownership = voris::mem::resource_ownership::caller_owned,
            .thread_safety = thread_safety_,
            .supports_remote_deallocate = supports_remote_deallocate_,
        };
    }

    [[nodiscard]] voris::mem::usage_snapshot usage() const noexcept {
        std::lock_guard guard{mutex_};
        std::size_t bytes{};
        for (const auto& block : active_) {
            bytes += block.size;
        }
        return voris::mem::usage_snapshot{
            .active_bytes = bytes,
            .reserved_bytes = bytes,
            .active_allocations = active_.size(),
            .total_allocations = allocation_calls_,
            .total_deallocations = deallocation_calls_,
        };
    }

    [[nodiscard]] std::size_t deallocation_calls() const noexcept {
        std::lock_guard guard{mutex_};
        return deallocation_calls_;
    }

    [[nodiscard]] std::size_t remote_deallocation_calls() const noexcept {
        std::lock_guard guard{mutex_};
        return remote_deallocation_calls_;
    }

    [[nodiscard]] std::size_t active_allocations() const noexcept {
        std::lock_guard guard{mutex_};
        return active_.size();
    }

    [[nodiscard]] std::thread::id last_deallocate_thread() const noexcept {
        std::lock_guard guard{mutex_};
        return last_deallocate_thread_;
    }

    void fail_deallocate(bool enabled) noexcept {
        std::lock_guard guard{mutex_};
        fail_deallocate_ = enabled;
    }

    void release_all_for_test() noexcept {
        std::lock_guard guard{mutex_};
        fail_deallocate_ = false;
        for (const auto& block : active_) {
            ::operator delete(block.data, std::align_val_t{block.alignment});
            ++deallocation_calls_;
        }
        active_.clear();
    }

private:
    voris::mem::resource_thread_safety thread_safety_{};
    bool supports_remote_deallocate_{};
    mutable std::mutex mutex_{};
    std::vector<voris::mem::allocation> active_{};
    std::size_t allocation_calls_{};
    std::size_t deallocation_calls_{};
    std::size_t remote_deallocation_calls_{};
    std::thread::id last_deallocate_thread_{};
    bool fail_deallocate_{};
};

[[nodiscard]] voris::mem::const_buffer literal_buffer(const char* text) noexcept {
    return voris::mem::const_buffer{
        reinterpret_cast<const std::byte*>(text),
        std::char_traits<char>::length(text),
    };
}

[[nodiscard]] voris::mem::unique_buffer
make_test_unique(recording_resource& backend, char fill, std::size_t size = 4U) {
    auto buffer = voris::mem::make_unique_buffer(
        voris::mem::resource_ref{backend}, size, 8U, voris::mem::memory_tag{"m3.test.unique"});
    assert(buffer);
    assert(buffer->resize(size));
    std::memset(buffer->data(), fill, size);
    return std::move(*buffer);
}

[[nodiscard]] voris::mem::shared_buffer
make_test_shared(recording_resource& backend, char fill, std::size_t size = 4U) {
    auto buffer = voris::mem::make_shared_buffer(
        voris::mem::resource_ref{backend}, size, 8U, voris::mem::memory_tag{"m3.test.shared"});
    assert(buffer);
    assert(buffer->resize(size));
    std::memset(buffer->data(), fill, size);
    return std::move(*buffer);
}

void test_buffer_views() {
    std::array<std::byte, 6> bytes{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'},
        std::byte{'d'}, std::byte{'e'}, std::byte{'f'},
    };
    voris::mem::mutable_buffer mutable_view{bytes.data(), bytes.size()};
    auto middle = mutable_view.slice(2U, 3U);
    assert(middle);
    assert(middle->size == 3U);
    middle->data[1] = std::byte{'X'};
    assert(bytes[3] == std::byte{'X'});
    assert(!mutable_view.slice(5U, 2U));
    assert(mutable_view.slice(5U, 2U).error() == voris::mem::errc::size_overflow);

    const voris::mem::const_buffer const_view{bytes.data(), bytes.size()};
    auto suffix = const_view.slice(4U, 2U);
    assert(suffix);
    assert(suffix->data[0] == std::byte{'e'});
    assert(!const_view.slice(std::numeric_limits<std::size_t>::max(), 1U));
}

void test_unique_buffer() {
    recording_resource backend;
    auto made = voris::mem::make_unique_buffer(
        voris::mem::resource_ref{backend}, 32U, 16U, voris::mem::memory_tag{"m3.unique"});
    assert(made);
    assert(made->capacity() == 32U);
    assert(made->size() == 0U);
    assert(made->resize(12U));
    assert(made->size() == 12U);
    auto view = made->mutable_view();
    std::fill(view.data, view.data + view.size, std::byte{0x5A});
    voris::mem::unique_buffer moved{std::move(*made)};
    assert(!made->data());
    assert(moved.size() == 12U);
    assert(moved.const_view().data[4] == std::byte{0x5A});
    assert(!moved.resize(33U));
    assert(moved.resize(0U));
    assert(moved.reset());
    assert(backend.deallocation_calls() == 1U);

    recording_resource assignment_old_backend;
    recording_resource assignment_new_backend;
    auto assignment_old = voris::mem::make_unique_buffer(
        voris::mem::resource_ref{assignment_old_backend},
        8U,
        8U,
        voris::mem::memory_tag{"m3.unique.assign.old"});
    auto assignment_new = voris::mem::make_unique_buffer(
        voris::mem::resource_ref{assignment_new_backend},
        8U,
        8U,
        voris::mem::memory_tag{"m3.unique.assign.new"});
    assert(assignment_old);
    assert(assignment_new);
    assert(assignment_old->resize(3U));
    assert(assignment_new->resize(5U));
    auto* old_data = assignment_old->data();
    auto* new_data = assignment_new->data();
    assignment_old_backend.fail_deallocate(true);
    *assignment_old = std::move(*assignment_new);
    assert(assignment_old->data() == old_data);
    assert(assignment_old->size() == 3U);
    assert(assignment_new->data() == new_data);
    assert(assignment_new->size() == 5U);
    assert(assignment_old_backend.active_allocations() == 1U);
    assert(assignment_new_backend.active_allocations() == 1U);
    assignment_old_backend.fail_deallocate(false);
    assert(assignment_old->reset());
    assert(assignment_new->reset());

    auto bad_alignment = voris::mem::make_unique_buffer(
        voris::mem::resource_ref{backend}, 8U, 3U, voris::mem::memory_tag{"m3.bad"});
    assert(!bad_alignment);
    assert(bad_alignment.error() == voris::mem::errc::invalid_alignment);
}

void test_shared_buffer() {
    recording_resource backend;
    auto made = voris::mem::make_shared_buffer(
        voris::mem::resource_ref{backend},
        24U,
        8U,
        voris::mem::memory_tag{"m3.shared"},
        voris::mem::shared_buffer_options{.owner_generation = 7U});
    assert(made);
    assert(made->use_count() == 1U);
    assert(made->owner_generation() == 7U);
    assert(made->resize(8U));

    auto cloned = made->clone();
    assert(cloned);
    assert(made->use_count() == 2U);
    assert(made->reset());
    assert(cloned->use_count() == 1U);

    std::thread::id releasing_thread{};
    std::thread releaser{[shared = std::move(*cloned), &releasing_thread]() mutable {
        releasing_thread = std::this_thread::get_id();
        assert(shared.reset());
    }};
    releaser.join();
    assert(backend.deallocation_calls() == 1U);
    assert(backend.last_deallocate_thread() == releasing_thread);

    recording_resource reset_retry_backend;
    auto reset_retry = voris::mem::make_shared_buffer(
        voris::mem::resource_ref{reset_retry_backend},
        8U,
        8U,
        voris::mem::memory_tag{"m3.shared.reset.retry"});
    assert(reset_retry);
    reset_retry_backend.fail_deallocate(true);
    auto failed_reset = reset_retry->reset();
    assert(!failed_reset);
    assert(failed_reset.error() == voris::mem::errc::wrong_owner);
    assert(reset_retry->use_count() == 1U);
    assert(reset_retry_backend.active_allocations() == 1U);
    reset_retry_backend.fail_deallocate(false);
    assert(reset_retry->reset());
    assert(reset_retry_backend.active_allocations() == 0U);

    recording_resource assignment_old_backend;
    recording_resource assignment_new_backend;
    auto assignment_old = voris::mem::make_shared_buffer(
        voris::mem::resource_ref{assignment_old_backend},
        8U,
        8U,
        voris::mem::memory_tag{"m3.shared.assign.old"});
    auto assignment_new = voris::mem::make_shared_buffer(
        voris::mem::resource_ref{assignment_new_backend},
        8U,
        8U,
        voris::mem::memory_tag{"m3.shared.assign.new"});
    assert(assignment_old);
    assert(assignment_new);
    assert(assignment_old->resize(3U));
    assert(assignment_new->resize(5U));
    auto* old_data = assignment_old->data();
    auto* new_data = assignment_new->data();
    assignment_old_backend.fail_deallocate(true);
    *assignment_old = std::move(*assignment_new);
    assert(assignment_old->data() == old_data);
    assert(assignment_old->size() == 3U);
    assert(assignment_old->use_count() == 1U);
    assert(assignment_new->data() == new_data);
    assert(assignment_new->size() == 5U);
    assert(assignment_new->use_count() == 1U);
    assignment_old_backend.fail_deallocate(false);
    assert(assignment_old->reset());
    assert(assignment_new->reset());

    recording_resource shard_confined{
        voris::mem::resource_thread_safety::shard_confined,
        false,
    };
    auto rejected = voris::mem::make_shared_buffer(
        voris::mem::resource_ref{shard_confined},
        16U,
        8U,
        voris::mem::memory_tag{"m3.shared.reject"});
    assert(!rejected);
    assert(rejected.error() == voris::mem::errc::wrong_owner);

    recording_resource remote_capable{
        voris::mem::resource_thread_safety::shard_confined,
        true,
    };
    auto remote_final = voris::mem::make_shared_buffer(
        voris::mem::resource_ref{remote_capable},
        16U,
        8U,
        voris::mem::memory_tag{"m3.shared.remote"});
    assert(remote_final);
    assert(remote_final->reset());
    assert(remote_capable.remote_deallocation_calls() == 1U);

    auto overflow = voris::mem::make_shared_buffer(
        voris::mem::resource_ref{backend},
        8U,
        8U,
        voris::mem::memory_tag{"m3.shared.overflow"},
        voris::mem::shared_buffer_options{.max_refcount = 1U});
    assert(overflow);
    auto overflow_clone = overflow->clone();
    assert(!overflow_clone);
    assert(overflow_clone.error() == voris::mem::errc::size_overflow);
    assert(overflow->reset());

#if defined(VORIS_VMEM_ENABLE_TEST_HOOKS)
    auto generation = voris::mem::make_shared_buffer(
        voris::mem::resource_ref{backend},
        8U,
        8U,
        voris::mem::memory_tag{"m3.shared.generation"},
        voris::mem::shared_buffer_options{.owner_generation = 11U});
    assert(generation);
    generation->test_override_handle_generation(12U);
    auto stale_release = generation->reset();
    assert(!stale_release);
    assert(stale_release.error() == voris::mem::errc::wrong_owner);
    generation->test_override_handle_generation(11U);
    assert(generation->reset());
#endif
}

void test_buffer_chain_operations() {
    recording_resource backend;
    voris::mem::buffer_chain chain;
    assert(chain.append(literal_buffer("cd")));
    assert(chain.prepend(literal_buffer("ab")));
    assert(chain.append(literal_buffer("ef")));
    assert(chain.segment_count() == 3U);
    assert(chain.size() == 6U);

    auto slice = chain.slice(1U, 4U);
    assert(slice);
    auto coalesced = slice->coalesce(
        voris::mem::resource_ref{backend}, 4U, 8U, voris::mem::memory_tag{"m3.coalesce"});
    assert(coalesced);
    assert(coalesced->size() == 4U);
    assert(std::memcmp(coalesced->data(), "bcde", 4U) == 0);
    assert(coalesced->reset());

    auto too_large = chain.coalesce(
        voris::mem::resource_ref{backend}, 5U, 8U, voris::mem::memory_tag{"m3.too_large"});
    assert(!too_large);
    assert(too_large.error() == voris::mem::errc::budget_exceeded);

    assert(chain.consume(2U));
    assert(chain.size() == 4U);
    assert(chain.trim(1U));
    assert(chain.size() == 3U);
    auto final = chain.coalesce(
        voris::mem::resource_ref{backend}, 3U, 8U, voris::mem::memory_tag{"m3.final"});
    assert(final);
    assert(std::memcmp(final->data(), "cde", 3U) == 0);
    assert(final->reset());

    auto owned = voris::mem::make_unique_buffer(
        voris::mem::resource_ref{backend}, 3U, 8U, voris::mem::memory_tag{"m3.owned"});
    assert(owned);
    assert(owned->resize(3U));
    std::memcpy(owned->data(), "xyz", 3U);
    voris::mem::buffer_chain owning_chain;
    assert(owning_chain.append(std::move(*owned)));
    assert(owning_chain.segment(0U).data[2] == std::byte{'z'});
}

void test_buffer_chain_invalid_buffer_shapes() {
    voris::mem::buffer_chain chain;
    const voris::mem::const_buffer invalid_source{nullptr, 4U};
    auto append_invalid = chain.append(invalid_source);
    assert(!append_invalid);
    assert(append_invalid.error() == voris::mem::errc::wrong_owner);
    assert(chain.empty());
    auto prepend_invalid = chain.prepend(invalid_source);
    assert(!prepend_invalid);
    assert(prepend_invalid.error() == voris::mem::errc::wrong_owner);
    assert(chain.empty());
    auto invalid_slice = invalid_source.slice(0U, 1U);
    assert(!invalid_slice);
    assert(invalid_slice.error() == voris::mem::errc::wrong_owner);

    assert(chain.append(literal_buffer("ab")));
    voris::mem::mutable_buffer invalid_destination{nullptr, 2U};
    auto copied = voris::mem::copy_prefix(chain, invalid_destination, 1U);
    assert(!copied);
    assert(copied.error() == voris::mem::errc::wrong_owner);
}

void test_buffer_chain_move_empties_source() {
    recording_resource inline_backend;
    voris::mem::buffer_chain inline_chain;
    auto inline_owned = make_test_unique(inline_backend, 'i');
    assert(inline_chain.append(std::move(inline_owned)));
    assert(inline_chain.append(literal_buffer("ab")));
    voris::mem::buffer_chain moved_inline{std::move(inline_chain)};
    assert(inline_chain.empty());
    assert(inline_chain.size() == 0U);
    assert(inline_chain.segment_count() == 0U);
    assert(moved_inline.size() == 6U);
    assert(inline_backend.deallocation_calls() == 0U);
    assert(moved_inline.consume(4U));
    assert(inline_backend.deallocation_calls() == 1U);

    std::array<recording_resource, 5> spill_backends;
    voris::mem::buffer_chain spill_chain;
    for (std::size_t index = 0U; index < spill_backends.size(); ++index) {
        auto owned =
            make_test_unique(spill_backends[index], static_cast<char>('a' + static_cast<char>(index)));
        assert(spill_chain.append(std::move(owned)));
    }
    voris::mem::buffer_chain moved_spill{std::move(spill_chain)};
    assert(spill_chain.empty());
    assert(spill_chain.size() == 0U);
    assert(spill_chain.segment_count() == 0U);
    assert(moved_spill.segment_count() == 5U);
    assert(spill_backends[0].deallocation_calls() == 0U);
    assert(moved_spill.consume(4U));
    assert(spill_backends[0].deallocation_calls() == 1U);

    recording_resource assign_old_backend;
    recording_resource assign_new_backend;
    voris::mem::buffer_chain assign_target;
    auto old_owned = make_test_unique(assign_old_backend, 'o');
    assert(assign_target.append(std::move(old_owned)));
    voris::mem::buffer_chain assign_source;
    auto new_owned = make_test_unique(assign_new_backend, 'n');
    assert(assign_source.append(std::move(new_owned)));
    assign_target = std::move(assign_source);
    assert(assign_source.empty());
    assert(assign_source.size() == 0U);
    assert(assign_source.segment_count() == 0U);
    assert(assign_old_backend.deallocation_calls() == 1U);
    assert(assign_new_backend.deallocation_calls() == 0U);

    recording_resource failing_old_backend;
    recording_resource new_backend;
    recording_resource coalesce_backend;
    voris::mem::buffer_chain mixed_target;
    auto failing_old_owned = make_test_unique(failing_old_backend, 'x');
    assert(mixed_target.append(std::move(failing_old_owned)));
    failing_old_backend.fail_deallocate(true);
    {
        voris::mem::buffer_chain scoped_source;
        auto scoped_new_owned = make_test_unique(new_backend, 'q');
        assert(scoped_source.append(std::move(scoped_new_owned)));
        mixed_target = std::move(scoped_source);
        assert(scoped_source.empty());
        assert(scoped_source.size() == 0U);
        assert(scoped_source.segment_count() == 0U);
    }
    assert(new_backend.deallocation_calls() == 0U);
    assert(mixed_target.size() == 4U);
    auto flat = mixed_target.coalesce(
        voris::mem::resource_ref{coalesce_backend},
        4U,
        8U,
        voris::mem::memory_tag{"m3.move.assign.mixed"});
    assert(flat);
    assert(flat->size() == 4U);
    assert(std::memcmp(flat->data(), "qqqq", 4U) == 0);
    assert(flat->reset());
    assert(mixed_target.consume(mixed_target.size()));
    assert(new_backend.deallocation_calls() == 1U);
    failing_old_backend.release_all_for_test();
}

void test_buffer_chain_owned_segments_release_immediately() {
    recording_resource unique_backend;
    voris::mem::buffer_chain single_unique;
    auto unique = make_test_unique(unique_backend, 'a');
    assert(single_unique.append(std::move(unique)));
    assert(unique_backend.deallocation_calls() == 0U);
    assert(single_unique.consume(single_unique.size()));
    assert(single_unique.empty());
    assert(unique_backend.deallocation_calls() == 1U);

    recording_resource inline_two_a;
    recording_resource inline_two_tail;
    voris::mem::buffer_chain inline_two;
    auto two_a = make_test_unique(inline_two_a, 'a');
    auto two_tail = make_test_unique(inline_two_tail, 'b');
    assert(inline_two.append(std::move(two_a)));
    assert(inline_two.append(std::move(two_tail)));
    assert(inline_two.segment_count() == 2U);
    assert(inline_two.trim(4U));
    assert(inline_two.segment_count() == 1U);
    assert(inline_two_tail.deallocation_calls() == 1U);
    assert(inline_two_a.deallocation_calls() == 0U);

    std::array<recording_resource, 4> inline_four_backends;
    voris::mem::buffer_chain inline_four;
    for (std::size_t index = 0U; index < inline_four_backends.size(); ++index) {
        auto segment = make_test_unique(
            inline_four_backends[index], static_cast<char>('a' + static_cast<char>(index)));
        assert(inline_four.append(std::move(segment)));
    }
    assert(inline_four.segment_count() == 4U);
    assert(inline_four.trim(4U));
    assert(inline_four.segment_count() == 3U);
    assert(inline_four_backends[3].deallocation_calls() == 1U);
    assert(inline_four_backends[0].deallocation_calls() == 0U);

    std::array<recording_resource, 5> spill_backends;
    voris::mem::buffer_chain spill;
    for (std::size_t index = 0U; index < spill_backends.size(); ++index) {
        auto segment =
            make_test_unique(spill_backends[index], static_cast<char>('k' + static_cast<char>(index)));
        assert(spill.append(std::move(segment)));
    }
    assert(spill.segment_count() == 5U);
    assert(spill.trim(4U));
    assert(spill.segment_count() == 4U);
    assert(spill_backends[4].deallocation_calls() == 1U);

    recording_resource shared_remote{
        voris::mem::resource_thread_safety::shard_confined,
        true,
    };
    voris::mem::buffer_chain shared_consume;
    auto shared = make_test_shared(shared_remote, 's');
    assert(shared_consume.append(std::move(shared)));
    assert(shared_consume.consume(shared_consume.size()));
    assert(shared_remote.remote_deallocation_calls() == 1U);

    recording_resource shared_tail_remote{
        voris::mem::resource_thread_safety::shard_confined,
        true,
    };
    recording_resource shared_head_remote{
        voris::mem::resource_thread_safety::shard_confined,
        true,
    };
    voris::mem::buffer_chain shared_trim;
    auto shared_head = make_test_shared(shared_head_remote, 'h');
    auto shared_tail = make_test_shared(shared_tail_remote, 't');
    assert(shared_trim.append(std::move(shared_head)));
    assert(shared_trim.append(std::move(shared_tail)));
    assert(shared_trim.trim(4U));
    assert(shared_tail_remote.remote_deallocation_calls() == 1U);
    assert(shared_head_remote.remote_deallocation_calls() == 0U);
}

void test_parser_helpers_and_gather() {
    voris::mem::buffer_chain chain;
    assert(chain.append(voris::mem::const_buffer{
        reinterpret_cast<const std::byte*>("\x01\x02"), 2U}));
    assert(chain.append(voris::mem::const_buffer{
        reinterpret_cast<const std::byte*>("\x03:\x05"), 3U}));

    auto be = voris::mem::peek_uint_be<std::uint32_t>(chain, 0U);
    assert(be);
    assert(*be == 0x0102033AU);
    auto le = voris::mem::peek_uint_le<std::uint16_t>(chain, 1U);
    assert(le);
    assert(*le == 0x0302U);
    assert(!voris::mem::peek_uint_be<std::uint32_t>(chain, 2U));

    auto delimiter = voris::mem::find_delimiter(chain, std::byte{':'});
    assert(delimiter);
    assert(*delimiter == 3U);
    assert(!voris::mem::find_delimiter(chain, std::byte{'!'}));

    std::array<std::byte, 4> prefix{};
    auto copied = voris::mem::copy_prefix(chain, voris::mem::mutable_buffer{prefix.data(), prefix.size()}, 4U);
    assert(copied);
    assert(*copied == 4U);
    assert(prefix[0] == std::byte{0x01});
    assert(prefix[3] == std::byte{':'});

    auto neutral = voris::mem::detail::to_neutral_gather(chain, 8U);
    assert(neutral);
    assert(neutral->size() == 2U);
    assert((*neutral)[0].size == 2U);

#if defined(__unix__) || defined(__APPLE__)
    auto posix_entries = voris::mem::detail::to_posix_iovec(chain, 8U);
    assert(posix_entries);
    assert(posix_entries->size() == 2U);
#endif
#if defined(_WIN32)
    auto windows_entries = voris::mem::detail::to_windows_wsabuf(chain, 8U);
    assert(windows_entries);
    assert(windows_entries->size() == 2U);
#endif
}

void test_buffer_chain_properties() {
    recording_resource backend;
    recording_resource owned_backend;
    recording_resource shared_backend;
    std::mt19937_64 rng{0xC0FFEEU};
    for (std::size_t iteration = 0; iteration < 128U; ++iteration) {
        voris::mem::buffer_chain chain;
        std::vector<std::byte> model;
        std::vector<std::array<std::byte, 8>> stable_storage;
        stable_storage.reserve(64U);
        for (std::size_t op = 0; op < 64U; ++op) {
            const auto choice = rng() % 8U;
            if (choice < 2U) {
                std::array<std::byte, 8> storage{};
                const auto count = static_cast<std::size_t>((rng() % storage.size()) + 1U);
                for (std::size_t i = 0; i < count; ++i) {
                    storage[i] = std::byte{static_cast<unsigned char>(rng() & 0xFFU)};
                }
                stable_storage.push_back(storage);
                const auto view = voris::mem::const_buffer{stable_storage.back().data(), count};
                if (choice == 0U) {
                    assert(chain.append(view));
                    model.insert(model.end(), view.data, view.data + view.size);
                } else {
                    assert(chain.prepend(view));
                    model.insert(model.begin(), view.data, view.data + view.size);
                }
            } else if (choice == 2U) {
                auto owned = voris::mem::make_unique_buffer(
                    voris::mem::resource_ref{owned_backend},
                    4U,
                    8U,
                    voris::mem::memory_tag{"m3.property.unique"});
                assert(owned);
                assert(owned->resize(4U));
                for (std::size_t i = 0U; i < 4U; ++i) {
                    owned->data()[i] = std::byte{static_cast<unsigned char>(rng() & 0xFFU)};
                }
                std::array<std::byte, 4> copied{};
                std::memcpy(copied.data(), owned->data(), copied.size());
                assert(chain.append(std::move(*owned)));
                model.insert(model.end(), copied.begin(), copied.end());
            } else if (choice == 3U) {
                auto shared = voris::mem::make_shared_buffer(
                    voris::mem::resource_ref{shared_backend},
                    4U,
                    8U,
                    voris::mem::memory_tag{"m3.property.shared"});
                assert(shared);
                assert(shared->resize(4U));
                for (std::size_t i = 0U; i < 4U; ++i) {
                    shared->data()[i] = std::byte{static_cast<unsigned char>(rng() & 0xFFU)};
                }
                std::array<std::byte, 4> copied{};
                std::memcpy(copied.data(), shared->data(), copied.size());
                assert(chain.append(std::move(*shared)));
                model.insert(model.end(), copied.begin(), copied.end());
            } else if (choice == 4U && !model.empty()) {
                const auto count = static_cast<std::size_t>(rng() % (model.size() + 1U));
                assert(chain.consume(count));
                model.erase(model.begin(), model.begin() + static_cast<std::ptrdiff_t>(count));
            } else if (choice == 5U && !model.empty()) {
                const auto count = static_cast<std::size_t>(rng() % (model.size() + 1U));
                assert(chain.trim(count));
                model.resize(model.size() - count);
            } else if (choice == 6U) {
                const auto invalid_count = model.empty() ? 1U : model.size() + 1U;
                assert(!chain.consume(invalid_count));
                assert(!chain.trim(invalid_count));
                assert(!chain.slice(model.size(), 1U));
            } else if (!model.empty()) {
                const auto offset = static_cast<std::size_t>(rng() % model.size());
                const auto max_count = model.size() - offset;
                const auto count = static_cast<std::size_t>(rng() % (max_count + 1U));
                auto sliced = chain.slice(offset, count);
                assert(sliced);
                auto flat_slice = sliced->coalesce(
                    voris::mem::resource_ref{backend},
                    count,
                    8U,
                    voris::mem::memory_tag{"m3.property.slice"});
                assert(flat_slice);
                assert(std::equal(model.begin() + static_cast<std::ptrdiff_t>(offset),
                                  model.begin() + static_cast<std::ptrdiff_t>(offset + count),
                                  flat_slice->const_view().data));
                assert(flat_slice->reset());
            }
            assert(chain.size() == model.size());
            auto flat = chain.coalesce(
                voris::mem::resource_ref{backend},
                model.size(),
                8U,
                voris::mem::memory_tag{"m3.property"});
            assert(flat);
            assert(flat->size() == model.size());
            assert(std::equal(model.begin(), model.end(), flat->const_view().data));
            assert(flat->reset());
        }
    }
}

} // namespace

int main() {
    test_buffer_views();
    test_unique_buffer();
    test_shared_buffer();
    test_buffer_chain_operations();
    test_buffer_chain_invalid_buffer_shapes();
    test_buffer_chain_move_empties_source();
    test_buffer_chain_owned_segments_release_immediately();
    test_parser_helpers_and_gather();
    test_buffer_chain_properties();
    return 0;
}
