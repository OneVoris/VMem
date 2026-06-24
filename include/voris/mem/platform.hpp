#pragma once

#include <cstddef>

namespace voris::mem {

enum class cpu_architecture {
    x86_64,
    arm64,
    unknown,
};

#if defined(__x86_64__) || defined(_M_X64)
inline constexpr cpu_architecture build_cpu_architecture = cpu_architecture::x86_64;
inline constexpr std::size_t cache_line_size = 64U;
#elif defined(__aarch64__) || defined(_M_ARM64)
inline constexpr cpu_architecture build_cpu_architecture = cpu_architecture::arm64;
inline constexpr std::size_t cache_line_size = 64U;
#else
inline constexpr cpu_architecture build_cpu_architecture = cpu_architecture::unknown;
inline constexpr std::size_t cache_line_size = 0U;
#endif

inline constexpr bool cache_line_assumption_available = cache_line_size != 0U;

static_assert(cache_line_size == 0U || (cache_line_size & (cache_line_size - 1U)) == 0U);

} // namespace voris::mem
