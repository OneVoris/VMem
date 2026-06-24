#include <voris/mem/page_source.hpp>
#include <voris/mem/platform.hpp>
#include <voris/mem/system_resource.hpp>
#include <voris/mem/tag.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using clock_type = std::chrono::steady_clock;

std::uint64_t elapsed_us(clock_type::time_point start, clock_type::time_point finish) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count());
}

void print_line(const char* name, std::size_t operations, std::uint64_t micros) {
    std::cout << name << ",ops=" << operations << ",micros=" << micros << '\n';
}

} // namespace

int main() {
    constexpr std::size_t page_iterations = 128U;
    constexpr std::size_t allocation_iterations = 4096U;

    std::cout << "m6_platform_assumptions"
              << ",cache_line_size=" << voris::mem::cache_line_size
              << ",available=" << (voris::mem::cache_line_assumption_available ? 1 : 0)
              << '\n';

    voris::mem::os_page_source pages;
    auto page_size = pages.page_size();
    if (!page_size) {
        return 1;
    }

    auto start = clock_type::now();
    for (std::size_t index = 0; index < page_iterations; ++index) {
        auto span = pages.reserve(*page_size);
        if (!span || !pages.commit(*span)) {
            return 1;
        }
        auto* bytes = static_cast<volatile unsigned char*>(span->data);
        bytes[0] = static_cast<unsigned char>(index);
        if (!pages.decommit(*span) || !pages.release(*span)) {
            return 1;
        }
    }
    auto finish = clock_type::now();
    print_line("m6_page_source_roundtrip", page_iterations, elapsed_us(start, finish));

    voris::mem::page_source_options prefer_huge{
        .prefer_huge_pages = true,
        .allow_huge_page_fallback = true,
    };
    start = clock_type::now();
    for (std::size_t index = 0; index < 8U; ++index) {
        auto span = pages.reserve(*page_size, prefer_huge);
        if (!span || !pages.commit(*span)) {
            return 1;
        }
        if (!pages.decommit(*span) || !pages.release(*span)) {
            return 1;
        }
    }
    finish = clock_type::now();
    print_line("m6_huge_page_prefer_fallback", 8U, elapsed_us(start, finish));

    voris::mem::system_resource system;
    start = clock_type::now();
    for (std::size_t index = 0; index < allocation_iterations; ++index) {
        auto block = system.allocate(voris::mem::make_allocation_request(64U, 64U));
        if (!block || !system.deallocate(*block)) {
            return 1;
        }
    }
    finish = clock_type::now();
    print_line("m6_system_resource_aligned_64",
               allocation_iterations,
               elapsed_us(start, finish));

    return system.usage().active_allocations == 0U ? 0 : 1;
}
