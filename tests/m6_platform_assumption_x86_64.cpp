#include <cstddef>

#undef __x86_64__
#undef _M_X64
#undef __aarch64__
#undef _M_ARM64

#define _M_X64 1

#include <voris/mem/platform.hpp>

static_assert(voris::mem::build_cpu_architecture == voris::mem::cpu_architecture::x86_64);
static_assert(voris::mem::cache_line_size == 64U);
static_assert(voris::mem::cache_line_assumption_available);
static_assert(voris::mem::cache_line_size >= alignof(std::max_align_t));

void vmem_m6_platform_assumption_x86_64_compiles() noexcept
{
}
