#include <voris/mem/debug_resource.hpp>
#include <voris/mem/slab_resource.hpp>
#include <voris/mem/system_resource.hpp>

#include <atomic>
#include <climits>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

[[nodiscard]] bool address_sanitizer_build() noexcept {
#if defined(__has_feature)
#    if __has_feature(address_sanitizer)
    return true;
#    endif
#endif
#if defined(__SANITIZE_ADDRESS__)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] bool thread_sanitizer_build() noexcept {
#if defined(__has_feature)
#    if __has_feature(thread_sanitizer)
    return true;
#    endif
#endif
#if defined(__SANITIZE_THREAD__)
    return true;
#else
    return false;
#endif
}

int run_asan_probe() {
    if (!address_sanitizer_build()) {
        std::cout << "m5_asan_visibility_probe,status=not_instrumented\n";
        return 0;
    }

    voris::mem::system_resource system;
    voris::mem::slab_resource slab{
        voris::mem::resource_ref{system},
        voris::mem::slab_options{.slab_size = 4096U, .remote_queue_capacity = 4U},
    };
    voris::mem::debug_resource debug{
        voris::mem::resource_ref{slab},
        voris::mem::debug_resource_options{.redzone_size = 32U},
    };

    auto block = debug.allocate(voris::mem::make_allocation_request(64U, 16U));
    if (!block) {
        return 2;
    }
    auto* payload = static_cast<std::byte*>(block->data);
    payload[block->size] = std::byte{0x7F};
    return 3;
}

int run_ubsan_probe() {
    voris::mem::system_resource system;
    voris::mem::debug_resource debug{voris::mem::resource_ref{system}};
    auto block = debug.allocate(voris::mem::make_allocation_request(sizeof(int), alignof(int)));
    if (!block) {
        return 4;
    }
    auto* value = static_cast<int*>(block->data);
    *value = INT_MAX;
    volatile int increment = 1;
    *value = *value + increment;
    static_cast<void>(debug.deallocate(*block));
    std::cout << "m5_ubsan_visibility_probe,status=completed_without_ubsan_abort\n";
    return 0;
}

int run_tsan_probe() {
    if (!thread_sanitizer_build()) {
        std::cout << "m5_tsan_visibility_probe,status=not_instrumented\n";
        return 0;
    }

    voris::mem::system_resource system;
    voris::mem::debug_resource debug{voris::mem::resource_ref{system}};
    auto block = debug.allocate(voris::mem::make_allocation_request(sizeof(int), alignof(int)));
    if (!block) {
        return 5;
    }
    auto* shared = static_cast<int*>(block->data);
    std::atomic<bool> start{};
    std::thread first{[&] {
        while (!start.load(std::memory_order_acquire)) {
        }
        for (int index = 0; index < 1024; ++index) {
            *shared = index;
        }
    }};
    std::thread second{[&] {
        start.store(true, std::memory_order_release);
        for (int index = 0; index < 1024; ++index) {
            *shared = index + 1;
        }
    }};
    first.join();
    second.join();
    static_cast<void>(debug.deallocate(*block));
    return 0;
}

} // namespace

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
#if defined(VORIS_VMEM_M5_ASAN_UBSAN_VISIBILITY_PROBE)
    if (argc > 1 && std::string_view{argv[1]} == "ubsan") {
        return run_ubsan_probe();
    }
    return run_asan_probe();
#elif defined(VORIS_VMEM_M5_TSAN_VISIBILITY_PROBE)
    return run_tsan_probe();
#else
    static_cast<void>(argc);
    static_cast<void>(argv);
    return 0;
#endif
}
