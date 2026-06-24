#include <voris/mem/arena_resource.hpp>
#include <voris/mem/fixed_block_pool.hpp>
#include <voris/mem/pmr_adapter.hpp>
#include <voris/mem/slab_resource.hpp>
#include <voris/mem/synchronized_resource.hpp>
#include <voris/mem/system_resource.hpp>

#undef NDEBUG

#include <cassert>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory_resource>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>

#if defined(NDEBUG)
#    error "VMem assert-style tests require assertions enabled"
#endif

namespace {

class oversized_slab_upstream {
public:
    [[nodiscard]] std::expected<voris::mem::allocation, voris::mem::errc>
    allocate(const voris::mem::allocation_request& request) noexcept {
        const auto index = allocation_calls_.fetch_add(1, std::memory_order_relaxed);
        if (index >= storage_.size()) {
            return std::unexpected(voris::mem::errc::out_of_memory);
        }
        const auto reported_size =
            index == 0 ? std::numeric_limits<std::size_t>::max() : request.size;
        return voris::mem::allocation{storage_[index].data(), reported_size, request.alignment};
    }

    [[nodiscard]] std::expected<void, voris::mem::errc>
    deallocate(voris::mem::allocation) noexcept {
        deallocation_calls_.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    [[nodiscard]] voris::mem::resource_traits traits() const noexcept {
        return voris::mem::resource_traits{
            .name = "oversized_slab_upstream",
            .ownership = voris::mem::resource_ownership::caller_owned,
            .thread_safety = voris::mem::resource_thread_safety::externally_synchronized,
            .supports_remote_deallocate = false,
        };
    }

    [[nodiscard]] voris::mem::usage_snapshot usage() const noexcept {
        return {};
    }

    [[nodiscard]] std::size_t allocation_calls() const noexcept {
        return allocation_calls_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t deallocation_calls() const noexcept {
        return deallocation_calls_.load(std::memory_order_relaxed);
    }

private:
    std::array<std::array<std::byte, 4096>, 4> storage_{};
    std::atomic<std::size_t> allocation_calls_{};
    std::atomic<std::size_t> deallocation_calls_{};
};

class detecting_pmr_resource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] bool observed_concurrent_call() const noexcept {
        return observed_concurrent_call_.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        enter();
        auto* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        leave();
        return pointer;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
        enter();
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        leave();
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    void enter() noexcept {
        if (inside_.fetch_add(1, std::memory_order_acq_rel) != 0) {
            observed_concurrent_call_.store(true, std::memory_order_release);
        }
        std::this_thread::yield();
    }

    void leave() noexcept {
        inside_.fetch_sub(1, std::memory_order_acq_rel);
    }

    std::atomic<std::size_t> inside_{};
    std::atomic<bool> observed_concurrent_call_{};
};

class throwing_deallocate_pmr_resource final : public std::pmr::memory_resource {
public:
    void fail_deallocate(bool enabled) noexcept {
        fail_deallocate_.store(enabled, std::memory_order_relaxed);
    }

private:
    [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
        if (fail_deallocate_.load(std::memory_order_relaxed)) {
            throw std::bad_alloc{};
        }
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::atomic<bool> fail_deallocate_{};
};

} // namespace

int main() {
    using voris::mem::allocation;
    using voris::mem::arena_options;
    using voris::mem::arena_resource;
    using voris::mem::errc;
    using voris::mem::fixed_block_pool;
    using voris::mem::fixed_block_pool_options;
    using voris::mem::make_allocation_request;
    using voris::mem::pmr_memory_resource;
    using voris::mem::pmr_resource_adapter;
    using voris::mem::resource_ref;
    using voris::mem::resource_thread_safety;
    using voris::mem::slab_options;
    using voris::mem::slab_resource;
    using voris::mem::synchronized_resource;
    using voris::mem::system_resource;

    system_resource system;

    arena_resource arena{
        resource_ref{system},
        arena_options{.initial_chunk_size = 128, .max_chunk_size = 512, .retained_bytes_limit = 128},
    };
    assert(arena.traits().thread_safety == resource_thread_safety::shard_confined);
    auto arena_a = arena.allocate(make_allocation_request(24, 16));
    auto arena_b = arena.allocate(make_allocation_request(96, 32));
    assert(arena_a);
    assert(arena_b);
    assert(reinterpret_cast<std::uintptr_t>(arena_a->data) % 16 == 0);
    assert(reinterpret_cast<std::uintptr_t>(arena_b->data) % 32 == 0);
    assert(arena.usage().active_bytes == 120);
    assert(arena.usage().reserved_bytes >= 128);
    auto arena_zero = arena.allocate(make_allocation_request(0, 64));
    assert(arena_zero);
    assert(arena_zero->data == nullptr);
    assert(!arena.allocate(make_allocation_request(8, 3)));
    auto arena_huge =
        arena.allocate(make_allocation_request(std::numeric_limits<std::size_t>::max(), 2));
    assert(!arena_huge);
    assert(arena_huge.error() == errc::size_overflow);
    arena.reset();
    assert(arena.usage().active_bytes == 0);
    assert(arena.usage().reserved_bytes <= 128);

    fixed_block_pool pool{
        resource_ref{system},
        fixed_block_pool_options{.block_size = 24, .block_alignment = 32, .capacity = 2},
    };
    assert(pool.traits().thread_safety == resource_thread_safety::shard_confined);
    auto pool_a = pool.allocate(make_allocation_request(16, 16));
    auto pool_b = pool.allocate(make_allocation_request(24, 32));
    auto pool_full = pool.allocate(make_allocation_request(8, 8));
    assert(pool_a);
    assert(pool_b);
    assert(!pool_full);
    assert(pool_full.error() == errc::out_of_memory);
    assert(reinterpret_cast<std::uintptr_t>(pool_a->data) % 32 == 0);
    assert(pool.usage().active_allocations == 2);
    auto pool_wrong_size = pool.deallocate(allocation{pool_a->data, 8, pool_a->alignment});
    assert(!pool_wrong_size);
    assert(pool_wrong_size.error() == errc::wrong_owner);
    assert(pool.deallocate(*pool_a));
    auto pool_double = pool.deallocate(*pool_a);
    assert(!pool_double);
    assert(pool_double.error() == errc::wrong_owner);
    fixed_block_pool other_pool{
        resource_ref{system},
        fixed_block_pool_options{.block_size = 24, .block_alignment = 32, .capacity = 1},
    };
    auto other_block = other_pool.allocate(make_allocation_request(16, 16));
    assert(other_block);
    auto wrong_owner = pool.deallocate(*other_block);
    assert(!wrong_owner);
    assert(wrong_owner.error() == errc::wrong_owner);
    assert(other_pool.deallocate(*other_block));
    assert(pool.deallocate(*pool_b));

    auto pool_descriptor = pool.allocate_block(make_allocation_request(16, 16));
    assert(pool_descriptor);
    auto stale_pool_descriptor = *pool_descriptor;
    assert(pool.deallocate_block(*pool_descriptor));
    auto pool_reused = pool.allocate_block(make_allocation_request(16, 16));
    assert(pool_reused);
    assert(pool_reused->block.data == stale_pool_descriptor.block.data);
    auto stale_pool_release = pool.deallocate_block(stale_pool_descriptor);
    assert(!stale_pool_release);
    assert(stale_pool_release.error() == errc::wrong_owner);
    assert(pool.deallocate_block(*pool_reused));

    slab_resource slab{
        resource_ref{system},
        slab_options{.slab_size = 4096, .remote_queue_capacity = 1},
    };
    assert(slab.traits().thread_safety == resource_thread_safety::shard_confined);
    assert(slab.traits().supports_remote_deallocate);
    auto small_class = voris::mem::select_slab_size_class(17);
    assert(small_class);
    assert(small_class->block_size >= 17);
    auto slab_a = slab.allocate(make_allocation_request(17, 8));
    auto slab_b = slab.allocate(make_allocation_request(80, 16));
    assert(slab_a);
    assert(slab_b);
    assert(slab_a->size == small_class->block_size);
    assert(reinterpret_cast<std::uintptr_t>(slab_b->data) % 16 == 0);
    auto slab_wrong_alignment = slab.deallocate(allocation{slab_a->data, slab_a->size, 32});
    assert(!slab_wrong_alignment);
    assert(slab_wrong_alignment.error() == errc::wrong_owner);
    assert(slab.remote_deallocate(*slab_a));
    assert(slab.usage().active_allocations == 2);
    assert(slab.drain_remote_frees() == 1);
    assert(slab.usage().active_allocations == 1);
    auto slab_c = slab.allocate(make_allocation_request(24, 8));
    auto slab_d = slab.allocate(make_allocation_request(24, 8));
    assert(slab_c);
    assert(slab_d);
    assert(slab.remote_deallocate(*slab_c));
    assert(slab.remote_deallocate(*slab_d));
    auto remote = slab.remote_usage();
    assert(remote.saturated_count == 1);
    assert(remote.slow_path_count == 1);
    auto slab_double = slab.deallocate(*slab_d);
    assert(!slab_double);
    assert(slab_double.error() == errc::wrong_owner);
    assert(slab.drain_remote_frees() == 1);
    assert(slab.deallocate(*slab_b));

    auto slab_descriptor = slab.allocate_block(make_allocation_request(24, 8));
    assert(slab_descriptor);
    auto stale_slab_descriptor = *slab_descriptor;
    assert(slab.deallocate_block(*slab_descriptor));
    auto slab_reused = slab.allocate_block(make_allocation_request(24, 8));
    assert(slab_reused);
    assert(slab_reused->block.data == stale_slab_descriptor.block.data);
    auto stale_slab_release = slab.deallocate_block(stale_slab_descriptor);
    assert(!stale_slab_release);
    assert(stale_slab_release.error() == errc::wrong_owner);
    assert(slab.deallocate_block(*slab_reused));

    auto remote_stale = slab.allocate_block(make_allocation_request(24, 8));
    assert(remote_stale);
    auto stale_remote_descriptor = *remote_stale;
    assert(slab.deallocate_block(*remote_stale));
    auto remote_current = slab.allocate_block(make_allocation_request(24, 8));
    assert(remote_current);
    assert(remote_current->block.data == stale_remote_descriptor.block.data);
    auto remote_stale_release = slab.remote_deallocate_block(stale_remote_descriptor);
    assert(!remote_stale_release);
    assert(remote_stale_release.error() == errc::wrong_owner);
    assert(slab.usage().active_allocations >= 1);
    assert(slab.deallocate_block(*remote_current));

    slab_resource push_failure_slab{
        resource_ref{system},
        slab_options{
            .slab_size = 4096,
            .remote_queue_capacity = 16,
            .force_remote_queue_push_failure = true,
        },
    };
    auto push_failure_block = push_failure_slab.allocate(make_allocation_request(24, 8));
    assert(push_failure_block);
    assert(push_failure_slab.remote_deallocate(*push_failure_block));
    auto push_failure_remote = push_failure_slab.remote_usage();
    assert(push_failure_remote.queued_count == 0);
    assert(push_failure_remote.slow_path_count == 1);
    assert(push_failure_slab.usage().active_allocations == 0);

    oversized_slab_upstream oversized_upstream;
    slab_resource overflow_slab{
        resource_ref{oversized_upstream},
        slab_options{.slab_size = 4096, .remote_queue_capacity = 1},
    };
    auto first_oversized = overflow_slab.allocate(make_allocation_request(24, 8));
    assert(first_oversized);
    auto overflow_before = overflow_slab.usage();
    auto overflow_failed = overflow_slab.allocate(make_allocation_request(80, 16));
    assert(!overflow_failed);
    assert(overflow_failed.error() == errc::size_overflow);
    assert(oversized_upstream.allocation_calls() == 2);
    assert(oversized_upstream.deallocation_calls() == 1);
    assert(overflow_slab.usage().reserved_bytes == overflow_before.reserved_bytes);
    assert(overflow_slab.deallocate(*first_oversized));

    slab_resource threaded_slab{
        resource_ref{system},
        slab_options{.slab_size = 4096, .remote_queue_capacity = 0},
    };
    std::vector<voris::mem::slab_allocation> threaded_blocks;
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t blocks_per_producer = 32;
    threaded_blocks.reserve(producer_count * blocks_per_producer);
    for (std::size_t i = 0; i < producer_count * blocks_per_producer; ++i) {
        auto block = threaded_slab.allocate_block(make_allocation_request(24, 8));
        assert(block);
        threaded_blocks.push_back(*block);
    }
    std::atomic<std::size_t> next_remote_index{};
    std::atomic<bool> producers_done{};
    std::atomic<std::size_t> valid_remote_releases{};
    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&] {
            for (;;) {
                const auto index = next_remote_index.fetch_add(1, std::memory_order_relaxed);
                if (index >= threaded_blocks.size()) {
                    break;
                }
                auto released = threaded_slab.remote_deallocate(threaded_blocks[index].block);
                assert(released);
                valid_remote_releases.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    std::thread drainer{[&] {
        while (!producers_done.load(std::memory_order_acquire)) {
            static_cast<void>(threaded_slab.drain_remote_frees());
            auto local = threaded_slab.allocate_block(make_allocation_request(24, 8));
            if (local) {
                assert(threaded_slab.deallocate_block(*local));
            }
            std::this_thread::yield();
        }
        static_cast<void>(threaded_slab.drain_remote_frees());
    }};
    for (auto& producer : producers) {
        producer.join();
    }
    producers_done.store(true, std::memory_order_release);
    drainer.join();
    while (threaded_slab.drain_remote_frees() != 0) {
    }
    auto threaded_remote = threaded_slab.remote_usage();
    assert(valid_remote_releases.load(std::memory_order_relaxed) == threaded_blocks.size());
    assert(threaded_remote.saturated_count == threaded_blocks.size());
    assert(threaded_remote.slow_path_count == threaded_blocks.size());
    assert(threaded_slab.usage().active_allocations == 0);

    synchronized_resource sync{resource_ref{system}};
    assert(sync.traits().thread_safety == resource_thread_safety::thread_safe);
    auto sync_block = sync.allocate(make_allocation_request(32, alignof(std::max_align_t)));
    assert(sync_block);
    assert(sync.deallocate(*sync_block));
    std::thread t1{[&] {
        auto block = sync.allocate(make_allocation_request(16, alignof(std::max_align_t)));
        assert(block);
        assert(sync.deallocate(*block));
    }};
    std::thread t2{[&] {
        auto block = sync.allocate(make_allocation_request(16, alignof(std::max_align_t)));
        assert(block);
        assert(sync.deallocate(*block));
    }};
    t1.join();
    t2.join();

    pmr_memory_resource pmr_vmem{resource_ref{system}};
    void* pmr_block = pmr_vmem.allocate(48, 32);
    assert(reinterpret_cast<std::uintptr_t>(pmr_block) % 32 == 0);
    pmr_vmem.deallocate(pmr_block, 48, 32);

    pmr_resource_adapter from_pmr{std::pmr::new_delete_resource()};
    assert(from_pmr.traits().thread_safety == resource_thread_safety::thread_safe);
    auto adapted = from_pmr.allocate(make_allocation_request(64, 32));
    assert(adapted);
    assert(reinterpret_cast<std::uintptr_t>(adapted->data) % 32 == 0);
    assert(from_pmr.deallocate(*adapted));
    std::vector<std::thread> pmr_threads;
    for (std::size_t index = 0; index < 4; ++index) {
        pmr_threads.emplace_back([&] {
            for (std::size_t i = 0; i < 128; ++i) {
                auto block = from_pmr.allocate(make_allocation_request(32, 16));
                assert(block);
                assert(from_pmr.deallocate(*block));
            }
        });
    }
    for (auto& thread : pmr_threads) {
        thread.join();
    }
    assert(from_pmr.usage().active_allocations == 0);

    detecting_pmr_resource detecting_pmr;
    pmr_resource_adapter serialized_pmr{&detecting_pmr};
    std::vector<std::thread> serialized_threads;
    for (std::size_t index = 0; index < 4; ++index) {
        serialized_threads.emplace_back([&] {
            for (std::size_t i = 0; i < 128; ++i) {
                auto block = serialized_pmr.allocate(make_allocation_request(32, 16));
                assert(block);
                assert(serialized_pmr.deallocate(*block));
                auto bad_alignment = serialized_pmr.allocate(make_allocation_request(32, 3));
                assert(!bad_alignment);
                assert(bad_alignment.error() == errc::invalid_alignment);
            }
        });
    }
    for (auto& thread : serialized_threads) {
        thread.join();
    }
    assert(!detecting_pmr.observed_concurrent_call());
    assert(serialized_pmr.usage().active_allocations == 0);

    throwing_deallocate_pmr_resource throwing_pmr;
    pmr_resource_adapter throwing_adapter{&throwing_pmr};
    auto throwing_block = throwing_adapter.allocate(make_allocation_request(32, 16));
    assert(throwing_block);
    throwing_pmr.fail_deallocate(true);
    auto throwing_release = throwing_adapter.deallocate(*throwing_block);
    assert(!throwing_release);
    assert(throwing_release.error() == errc::wrong_owner);
    assert(throwing_adapter.usage().active_allocations == 1);
    assert(throwing_adapter.usage().active_bytes == 32);
    throwing_pmr.fail_deallocate(false);
    assert(throwing_adapter.deallocate(*throwing_block));
    assert(throwing_adapter.usage().active_allocations == 0);

    return 0;
}
