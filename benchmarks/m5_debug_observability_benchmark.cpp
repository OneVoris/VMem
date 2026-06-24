#include <voris/mem/debug_resource.hpp>
#include <voris/mem/slab_resource.hpp>
#include <voris/mem/system_resource.hpp>

#include <chrono>
#include <cstddef>
#include <iostream>
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
    voris::mem::system_resource system;
    voris::mem::debug_resource debug{
        voris::mem::resource_ref{system},
        voris::mem::debug_resource_options{.redzone_size = 16U},
    };

    constexpr std::size_t iterations = 10000U;
    print_timed("m5_debug_allocate_deallocate", iterations, [&] {
        for (std::size_t index = 0U; index < iterations; ++index) {
            auto block = debug.allocate(voris::mem::make_allocation_request(64U, 16U));
            if (!block || !debug.deallocate(*block)) {
                std::terminate();
            }
        }
        if (debug.usage().active_allocations != 0U) {
            std::terminate();
        }
    });

    std::vector<voris::mem::allocation> live;
    live.reserve(128U);
    print_timed("m5_leak_snapshot_diff", 128U, [&] {
        auto before = debug.leak_snapshot();
        for (std::size_t index = 0U; index < 128U; ++index) {
            auto block = debug.allocate(voris::mem::make_allocation_request(32U, 16U));
            if (!block) {
                std::terminate();
            }
            live.push_back(*block);
        }
        auto after = debug.leak_snapshot();
        auto diff = voris::mem::diff_leak_snapshots(before, after);
        if (diff.added.size() != live.size()) {
            std::terminate();
        }
        for (auto block : live) {
            if (!debug.deallocate(block)) {
                std::terminate();
            }
        }
        live.clear();
    });

    voris::mem::slab_resource slab{
        voris::mem::resource_ref{system},
        voris::mem::slab_options{.slab_size = 4096U, .remote_queue_capacity = 16U},
    };
    print_timed("m5_slab_size_class_snapshot", iterations, [&] {
        for (std::size_t index = 0U; index < iterations; ++index) {
            auto snapshot = slab.size_class_snapshots();
            if (snapshot.empty()) {
                std::terminate();
            }
        }
    });

    return 0;
}
