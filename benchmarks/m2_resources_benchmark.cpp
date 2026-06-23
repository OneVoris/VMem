#include <voris/mem/arena_resource.hpp>
#include <voris/mem/slab_resource.hpp>
#include <voris/mem/synchronized_resource.hpp>
#include <voris/mem/system_resource.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;

std::uint64_t elapsed_us(clock_type::time_point start, clock_type::time_point finish) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count());
}

void print_result(const char* name,
                  std::size_t operations,
                  std::uint64_t micros,
                  voris::mem::usage_snapshot usage) {
    std::cout << name << ",ops=" << operations << ",micros=" << micros
              << ",active_bytes=" << usage.active_bytes
              << ",reserved_bytes=" << usage.reserved_bytes
              << ",active_allocations=" << usage.active_allocations
              << ",failed_allocations=" << usage.failed_allocations << '\n';
}

void print_remote_result(const char* name,
                         std::size_t remote_attempts,
                         std::size_t drained_count,
                         std::size_t producer_count,
                         std::size_t queue_capacity,
                         std::uint64_t micros,
                         voris::mem::usage_snapshot usage,
                         voris::mem::slab_remote_snapshot remote) {
    std::cout << name << ",mode=smoke"
              << ",remote_attempts=" << remote_attempts
              << ",drain_released=" << drained_count
              << ",producer_threads=" << producer_count
              << ",queue_capacity=" << queue_capacity
              << ",micros=" << micros
              << ",active_bytes=" << usage.active_bytes
              << ",reserved_bytes=" << usage.reserved_bytes
              << ",active_allocations=" << usage.active_allocations
              << ",failed_allocations=" << usage.failed_allocations
              << ",queued=" << remote.queued_count
              << ",drained_total=" << remote.drained_count
              << ",saturated=" << remote.saturated_count
              << ",slow_path=" << remote.slow_path_count << '\n';
}

} // namespace

int main() {
    using voris::mem::arena_options;
    using voris::mem::arena_resource;
    using voris::mem::make_allocation_request;
    using voris::mem::resource_ref;
    using voris::mem::slab_options;
    using voris::mem::slab_resource;
    using voris::mem::synchronized_resource;
    using voris::mem::system_resource;

    system_resource system;

    arena_resource arena{
        resource_ref{system},
        arena_options{.initial_chunk_size = 4096, .max_chunk_size = 64 * 1024, .retained_bytes_limit = 4096},
    };
    auto start = clock_type::now();
    for (std::size_t i = 0; i < 4096; ++i) {
        const std::size_t size = 8 + ((i * 17) % 240);
        auto block = arena.allocate(make_allocation_request(size, alignof(std::max_align_t)));
        if (!block) {
            return 1;
        }
    }
    auto finish = clock_type::now();
    print_result("arena_fragmentation_smoke", 4096, elapsed_us(start, finish), arena.usage());
    arena.reset();

    slab_resource slab{
        resource_ref{system},
        slab_options{.slab_size = 4096, .remote_queue_capacity = 32},
    };
    constexpr std::size_t remote_queue_capacity = 32;
    std::vector<voris::mem::allocation> blocks;
    blocks.reserve(256);
    for (std::size_t i = 0; i < 256; ++i) {
        auto block = slab.allocate(make_allocation_request(24 + (i % 5), 8));
        if (!block) {
            return 1;
        }
        blocks.push_back(*block);
    }
    start = clock_type::now();
    constexpr std::size_t producer_count = 4;
    std::atomic<std::size_t> next_block{};
    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&] {
            for (;;) {
                const auto index = next_block.fetch_add(1, std::memory_order_relaxed);
                if (index >= blocks.size()) {
                    break;
                }
                auto released = slab.remote_deallocate(blocks[index]);
                if (!released) {
                    std::terminate();
                }
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    const auto drained = slab.drain_remote_frees();
    finish = clock_type::now();
    const auto remote = slab.remote_usage();
    print_remote_result("slab_remote_free_smoke",
                        blocks.size(),
                        drained,
                        producer_count,
                        remote_queue_capacity,
                        elapsed_us(start, finish),
                        slab.usage(),
                        remote);

    synchronized_resource synchronized{resource_ref{system}};
    constexpr std::size_t thread_count = 4;
    constexpr std::size_t iterations = 1024;
    start = clock_type::now();
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([&] {
            for (std::size_t i = 0; i < iterations; ++i) {
                auto block = synchronized.allocate(
                    make_allocation_request(32, alignof(std::max_align_t)));
                if (block) {
                    static_cast<void>(synchronized.deallocate(*block));
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    finish = clock_type::now();
    print_result("synchronized_contention_smoke",
                 thread_count * iterations * 2,
                 elapsed_us(start, finish),
                 synchronized.usage());

    return 0;
}
