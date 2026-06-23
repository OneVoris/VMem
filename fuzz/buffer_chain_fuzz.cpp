#include <voris/mem/buffer_chain.hpp>
#include <voris/mem/buffer_parser.hpp>
#include <voris/mem/system_resource.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace {

using stable_segment_storage = std::deque<std::array<std::byte, 16>>;

[[nodiscard]] std::uint64_t read_u64(std::span<const std::byte> input, std::size_t& offset) noexcept {
    std::uint64_t value{};
    for (std::size_t i = 0U; i < sizeof(value) && offset < input.size(); ++i, ++offset) {
        value = static_cast<std::uint64_t>((value << 8U) | static_cast<std::uint64_t>(input[offset]));
    }
    return value;
}

int run_case(std::span<const std::byte> input, std::uint64_t fallback_seed) {
    voris::mem::system_resource system;
    voris::mem::buffer_chain chain;
    std::vector<std::byte> model;
    stable_segment_storage stable_storage;
    std::size_t input_offset{};
    const auto seed = input.empty() ? fallback_seed : read_u64(input, input_offset);
    std::mt19937_64 rng{seed};

    for (std::size_t step = 0U; step < 256U; ++step) {
        const auto byte_or_random = [&]() -> std::uint64_t {
            if (input_offset < input.size()) {
                return static_cast<std::uint64_t>(input[input_offset++]);
            }
            return rng();
        };
        const auto op = byte_or_random() % 8U;
        if (op < 2U) {
            std::array<std::byte, 16> storage{};
            const auto count = static_cast<std::size_t>((byte_or_random() % storage.size()) + 1U);
            for (std::size_t i = 0U; i < count; ++i) {
                storage[i] = std::byte{static_cast<unsigned char>(byte_or_random() & 0xFFU)};
            }
            stable_storage.push_back(storage);
            voris::mem::const_buffer view{stable_storage.back().data(), count};
            auto updated = op == 0U ? chain.append(view) : chain.prepend(view);
            if (!updated) {
                return 1;
            }
            if (op == 0U) {
                model.insert(model.end(), view.data, view.data + view.size);
            } else {
                model.insert(model.begin(), view.data, view.data + view.size);
            }
        } else if (op == 2U && !model.empty()) {
            const auto count = static_cast<std::size_t>(byte_or_random() % (model.size() + 1U));
            if (!chain.consume(count)) {
                return 2;
            }
            model.erase(model.begin(), model.begin() + static_cast<std::ptrdiff_t>(count));
        } else if (op == 3U && !model.empty()) {
            const auto count = static_cast<std::size_t>(byte_or_random() % (model.size() + 1U));
            if (!chain.trim(count)) {
                return 3;
            }
            model.resize(model.size() - count);
        } else if (op == 4U) {
            if (chain.size() != model.size()) {
                return 4;
            }
            auto flat = chain.coalesce(
                voris::mem::resource_ref{system},
                model.size(),
                alignof(std::max_align_t),
                voris::mem::memory_tag{"m3.fuzz"});
            if (!flat || flat->size() != model.size()) {
                return 5;
            }
            if (!std::equal(model.begin(), model.end(), flat->const_view().data)) {
                return 6;
            }
        } else if (op == 5U && model.size() >= sizeof(std::uint16_t)) {
            const auto offset = static_cast<std::size_t>(byte_or_random() % (model.size() - 1U));
            auto parsed = voris::mem::peek_uint_be<std::uint16_t>(chain, offset);
            if (!parsed) {
                return 7;
            }
            const auto expected =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(model[offset]) << 8U) |
                                           static_cast<std::uint16_t>(model[offset + 1U]));
            if (*parsed != expected) {
                return 8;
            }
        } else if (op == 6U && !model.empty()) {
            const auto offset = static_cast<std::size_t>(byte_or_random() % model.size());
            const auto count = static_cast<std::size_t>(byte_or_random() % (model.size() - offset + 1U));
            auto sliced = chain.slice(offset, count);
            if (!sliced) {
                return 9;
            }
            auto flat = sliced->coalesce(
                voris::mem::resource_ref{system},
                count,
                alignof(std::max_align_t),
                voris::mem::memory_tag{"m3.fuzz.slice"});
            if (!flat) {
                return 10;
            }
            if (!std::equal(model.begin() + static_cast<std::ptrdiff_t>(offset),
                            model.begin() + static_cast<std::ptrdiff_t>(offset + count),
                            flat->const_view().data)) {
                return 11;
            }
        } else {
            const auto invalid = model.empty() ? 1U : model.size() + 1U;
            if (chain.consume(invalid) || chain.trim(invalid) || chain.slice(model.size(), 1U)) {
                return 12;
            }
        }
    }
    return 0;
}

int run_many_borrowed_segments_regression() {
    voris::mem::system_resource system;
    voris::mem::buffer_chain chain;
    stable_segment_storage stable_storage;
    std::vector<std::byte> model;

    const std::byte* first_segment_data{};
    for (std::size_t index = 0U; index < 160U; ++index) {
        std::array<std::byte, 16> storage{};
        storage[0] = std::byte{static_cast<unsigned char>(index & 0xFFU)};
        stable_storage.push_back(storage);
        if (index == 0U) {
            first_segment_data = stable_storage.front().data();
        }
        if (stable_storage.front().data() != first_segment_data) {
            return 30;
        }
        voris::mem::const_buffer view{stable_storage.back().data(), 1U};
        const bool append_to_back = (index % 2U) == 0U;
        auto updated = append_to_back ? chain.append(view) : chain.prepend(view);
        if (!updated) {
            return 31;
        }
        if (append_to_back) {
            model.push_back(view.data[0]);
        } else {
            model.insert(model.begin(), view.data[0]);
        }
    }

    auto flat = chain.coalesce(
        voris::mem::resource_ref{system},
        model.size(),
        alignof(std::max_align_t),
        voris::mem::memory_tag{"m3.fuzz.regression"});
    if (!flat || flat->size() != model.size()) {
        return 32;
    }
    if (!std::equal(model.begin(), model.end(), flat->const_view().data)) {
        return 33;
    }

    auto sliced = chain.slice(17U, 129U);
    if (!sliced) {
        return 34;
    }
    auto flat_slice = sliced->coalesce(
        voris::mem::resource_ref{system},
        129U,
        alignof(std::max_align_t),
        voris::mem::memory_tag{"m3.fuzz.regression.slice"});
    if (!flat_slice || flat_slice->size() != 129U) {
        return 35;
    }
    if (!std::equal(model.begin() + 17, model.begin() + 146, flat_slice->const_view().data)) {
        return 36;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const int regression = run_many_borrowed_segments_regression();
    if (regression != 0) {
        std::cerr << "buffer_chain_fuzz_regression failed,code=" << regression << '\n';
        return regression;
    }

    if (argc > 1) {
        std::vector<std::byte> input;
        for (int arg = 1; arg < argc; ++arg) {
            std::string_view token{argv[arg]};
            if (token.starts_with("seed=")) {
                std::uint64_t seed{};
                const auto digits = token.substr(5U);
                const auto* first = digits.data();
                const auto* last = first + digits.size();
                const auto parsed = std::from_chars(first, last, seed);
                if (parsed.ec != std::errc{} || parsed.ptr != last) {
                    return 20;
                }
                const int result = run_case({}, seed);
                if (result != 0) {
                    std::cerr << "buffer_chain_fuzz_input failed,seed=" << seed
                              << ",code=" << result << '\n';
                    return result;
                }
                continue;
            }
            for (const char c : token) {
                input.push_back(std::byte{static_cast<unsigned char>(c)});
            }
        }
        const int result = run_case(input, 0xD1CEB00CU);
        if (result != 0) {
            std::cerr << "buffer_chain_fuzz_input failed,bytes=" << input.size()
                      << ",code=" << result << '\n';
            return result;
        }
        std::cout << "buffer_chain_fuzz_input,bytes=" << input.size() << ",status=ok\n";
        return 0;
    }

    for (std::uint64_t seed = 1U; seed <= 32U; ++seed) {
        const int result = run_case({}, seed * 0x9E3779B97F4A7C15ULL);
        if (result != 0) {
            std::cerr << "buffer_chain_fuzz_smoke failed,seed=" << seed
                      << ",code=" << result << '\n';
            return result;
        }
    }
    std::cout << "buffer_chain_fuzz_smoke,seeds=32,status=ok\n";
    return 0;
}
