#include <voris/mem/buffer_chain.hpp>
#include <voris/mem/system_resource.hpp>

#include "buffer_gather_adapter.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{

using clock_type = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t elapsed_us(clock_type::time_point start, clock_type::time_point finish)
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count());
}

void print_line(const char *name, std::size_t segments, std::size_t operations, std::uint64_t micros)
{
    std::cout << name << ",segments=" << segments << ",ops=" << operations << ",micros=" << micros << '\n';
}

[[nodiscard]] voris::mem::const_buffer view_of(std::array<std::byte, 64> &storage) noexcept
{
    return voris::mem::const_buffer{storage.data(), storage.size()};
}

} // namespace

int main()
{
    constexpr std::array<std::size_t, 4> segment_counts{1U, 2U, 4U, 16U};
    constexpr std::size_t iterations = 4096U;
    std::vector<std::array<std::byte, 64>> storage(16U);
    for (std::size_t i = 0U; i < storage.size(); ++i)
    {
        storage[i].fill(std::byte{static_cast<unsigned char>(i + 1U)});
    }

    for (const auto segments : segment_counts)
    {
        auto start = clock_type::now();
        for (std::size_t iteration = 0U; iteration < iterations; ++iteration)
        {
            voris::mem::buffer_chain chain;
            for (std::size_t index = 0U; index < segments; ++index)
            {
                auto appended = chain.append(view_of(storage[index]));
                if (!appended)
                {
                    return 1;
                }
            }
        }
        auto finish = clock_type::now();
        print_line("buffer_chain_append", segments, iterations * segments, elapsed_us(start, finish));

        voris::mem::buffer_chain consume_chain;
        for (std::size_t index = 0U; index < segments; ++index)
        {
            auto appended = consume_chain.append(view_of(storage[index]));
            if (!appended)
            {
                return 2;
            }
        }
        start = clock_type::now();
        for (std::size_t iteration = 0U; iteration < iterations; ++iteration)
        {
            voris::mem::buffer_chain chain;
            for (std::size_t index = 0U; index < segments; ++index)
            {
                auto appended = chain.append(view_of(storage[index]));
                if (!appended)
                {
                    return 3;
                }
            }
            auto consumed = chain.consume((segments * storage[0].size()) / 2U);
            if (!consumed)
            {
                return 4;
            }
        }
        finish = clock_type::now();
        print_line("buffer_chain_consume", segments, iterations, elapsed_us(start, finish));

        voris::mem::buffer_chain gather_chain;
        for (std::size_t index = 0U; index < segments; ++index)
        {
            auto appended = gather_chain.append(view_of(storage[index]));
            if (!appended)
            {
                return 5;
            }
        }
        start = clock_type::now();
        for (std::size_t iteration = 0U; iteration < iterations; ++iteration)
        {
            auto gathered = voris::mem::detail::to_neutral_gather(gather_chain, segments);
            if (!gathered)
            {
                return 6;
            }
        }
        finish = clock_type::now();
        print_line("buffer_chain_gather", segments, iterations * segments, elapsed_us(start, finish));
    }

    return 0;
}
