#include <voris/mem/debug_resource.hpp>
#include <voris/mem/system_resource.hpp>

#undef NDEBUG

#include <array>
#include <cassert>
#include <cstddef>
#include <thread>
#include <vector>

#if defined(NDEBUG)
#    error "VMem assert-style tests require assertions enabled"
#endif

int main() {
    voris::mem::system_resource system;
    voris::mem::debug_resource debug{
        voris::mem::resource_ref{system},
        voris::mem::debug_resource_options{.redzone_size = 32U},
    };

    constexpr std::size_t thread_count = 4U;
    constexpr std::size_t iterations = 256U;
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t thread_index = 0U; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([&, thread_index] {
            std::array<voris::mem::debug_allocation, 8> live{};
            for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
                const auto slot = iteration % live.size();
                if (live[slot].block.data != nullptr) {
                    assert(debug.deallocate_block(live[slot]));
                    live[slot] = {};
                }
                const auto size = 1U + ((iteration + thread_index) % 127U);
                auto block = debug.allocate_block(voris::mem::make_allocation_request(size, 16U));
                assert(block);
                live[slot] = *block;
            }
            for (auto block : live) {
                if (block.block.data != nullptr) {
                    assert(debug.deallocate_block(block));
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto snapshot = debug.debug_snapshot();
    assert(snapshot.usage.active_allocations == 0U);
    assert(snapshot.redzone_failure_count == 0U);
    return 0;
}
