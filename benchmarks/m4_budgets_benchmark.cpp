#include <voris/mem/budget.hpp>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

namespace {

template <typename Work>
void print_timed(std::string_view name, std::size_t operations, Work&& work) {
    const auto start = std::chrono::steady_clock::now();
    work();
    const auto stop = std::chrono::steady_clock::now();
    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    std::cout << name << ",operations=" << operations << ",elapsed_ns=" << elapsed_ns << '\n';
}

} // namespace

int main() {
    using voris::mem::budget_dimensions;
    using voris::mem::budget_limits;
    using voris::mem::budget_node;
    using voris::mem::budget_options;
    using voris::mem::memory_budget;
    using voris::mem::memory_tag;

    constexpr std::size_t iterations = 10000U;
    memory_budget process{budget_options{
        .dimensions = budget_dimensions{.process = "bench-process"},
        .limits = budget_limits{.hard_limit = 1U << 30U},
    }};
    budget_node shard{process,
                      budget_options{
                          .dimensions = budget_dimensions{
                              .process = "bench-process",
                              .shard = "shard-0",
                              .subsystem = "cache",
                          },
                          .limits = budget_limits{.hard_limit = 1U << 29U},
                      }};

    print_timed("m4_reserve_commit_release", iterations, [&] {
        for (std::size_t index = 0U; index < iterations; ++index) {
            auto token = shard.reserve(64U, memory_tag{"benchmark.m4.single"});
            if (!token || !token->commit()) {
                std::terminate();
            }
            if (!shard.release(64U, memory_tag{"benchmark.m4.single"})) {
                std::terminate();
            }
        }
        if (!shard.snapshots().empty() || !process.snapshots().empty()) {
            std::terminate();
        }
    });

    constexpr std::size_t thread_count = 4U;
    print_timed("m4_concurrent_reserve_rollback", iterations * thread_count, [&] {
        std::vector<std::thread> threads;
        threads.reserve(thread_count);
        for (std::size_t thread_index = 0U; thread_index < thread_count; ++thread_index) {
            threads.emplace_back([&] {
                for (std::size_t index = 0U; index < iterations; ++index) {
                    auto token = shard.reserve(1U, memory_tag{"benchmark.m4.concurrent"});
                    if (!token || !token->rollback()) {
                        std::terminate();
                    }
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        if (!shard.snapshots().empty() || !process.snapshots().empty()) {
            std::terminate();
        }
    });

    return 0;
}
