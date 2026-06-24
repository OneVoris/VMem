#include <voris/mem/debug_resource.hpp>
#include <voris/mem/system_resource.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::uint64_t read_seed(std::span<const std::byte> input) noexcept {
    std::uint64_t seed{0xC0FFEE5EEDULL};
    for (std::byte byte : input) {
        seed = (seed << 5U) ^ (seed >> 2U) ^ static_cast<unsigned char>(byte);
    }
    return seed;
}

int run_case(std::span<const std::byte> input, std::uint64_t fallback_seed) {
    const auto seed = input.empty() ? fallback_seed : read_seed(input);
    std::mt19937_64 rng{seed};
    voris::mem::system_resource system;

    {
        voris::mem::debug_resource debug{
            voris::mem::resource_ref{system},
            voris::mem::debug_resource_options{
                .redzone_size = 16U,
                .preserve_sanitizer_diagnostics = false,
            },
        };
        std::array<voris::mem::debug_allocation, 8> blocks{};
        for (std::size_t index = 0U; index < blocks.size(); ++index) {
            const auto size = 1U + static_cast<std::size_t>(rng() % 96U);
            auto block = debug.allocate_block(voris::mem::make_allocation_request(size, 16U));
            if (!block) {
                return 1;
            }
            blocks[index] = *block;
        }

        const auto corrupted_index = static_cast<std::size_t>(rng() % blocks.size());
        auto& corrupted = blocks[corrupted_index];
        auto* payload = static_cast<std::byte*>(corrupted.block.data);
        if ((rng() & 1U) == 0U) {
            payload[corrupted.block.size] = std::byte{0x00};
        } else {
            payload[-1] = std::byte{0x00};
        }
        auto corrupted_release = debug.deallocate_block(corrupted);
        if (corrupted_release || corrupted_release.error() != voris::mem::errc::wrong_owner) {
            return 2;
        }

        for (std::size_t index = 0U; index < blocks.size(); ++index) {
            if (index == corrupted_index) {
                continue;
            }
            if (!debug.deallocate_block(blocks[index])) {
                return 3;
            }
        }

        auto snapshot = debug.debug_snapshot();
        if (snapshot.redzone_failure_count != 1U || snapshot.usage.active_allocations != 1U) {
            return 4;
        }
    }

    {
        voris::mem::debug_resource debug{voris::mem::resource_ref{system}};
        auto block = debug.allocate_block(voris::mem::make_allocation_request(32U, 16U));
        if (!block || !debug.deallocate_block(*block)) {
            return 5;
        }
        auto double_release = debug.deallocate_block(*block);
        if (double_release || double_release.error() != voris::mem::errc::wrong_owner) {
            return 6;
        }
    }

    {
        voris::mem::debug_resource first{voris::mem::resource_ref{system}};
        voris::mem::debug_resource second{voris::mem::resource_ref{system}};
        auto foreign = second.allocate(voris::mem::make_allocation_request(40U, 16U));
        if (!foreign) {
            return 7;
        }
        auto wrong_owner = first.deallocate(*foreign);
        if (wrong_owner || wrong_owner.error() != voris::mem::errc::wrong_owner) {
            return 8;
        }
        if (!second.deallocate(*foreign)) {
            return 9;
        }
    }

    {
        voris::mem::debug_resource debug{voris::mem::resource_ref{system}};
        auto block = debug.allocate_block(voris::mem::make_allocation_request(48U, 16U));
        if (!block) {
            return 10;
        }
        auto stale = *block;
        if (!debug.deallocate_block(*block)) {
            return 11;
        }
        auto stale_release = debug.deallocate_block(stale);
        if (stale_release || stale_release.error() != voris::mem::errc::wrong_owner) {
            return 12;
        }
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        std::vector<std::byte> input;
        for (int arg = 1; arg < argc; ++arg) {
            for (char c : std::string_view{argv[arg]}) {
                input.push_back(std::byte{static_cast<unsigned char>(c)});
            }
        }
        const int result = run_case(input, 0U);
        if (result != 0) {
            std::cerr << "allocator_corruption_fuzz_input failed,code=" << result << '\n';
            return result;
        }
        std::cout << "allocator_corruption_fuzz_input,bytes=" << input.size()
                  << ",status=ok\n";
        return 0;
    }

    for (std::uint64_t seed = 1U; seed <= 32U; ++seed) {
        const int result = run_case({}, seed * 0xD6E8FEB86659FD93ULL);
        if (result != 0) {
            std::cerr << "allocator_corruption_fuzz_smoke failed,seed=" << seed
                      << ",code=" << result << '\n';
            return result;
        }
    }
    std::cout << "allocator_corruption_fuzz_smoke,seeds=32,status=ok\n";
    return 0;
}
