#include <voris/mem/platform.hpp>

static_assert(voris::mem::cache_line_size == 0U ||
              (voris::mem::cache_line_size & (voris::mem::cache_line_size - 1U)) == 0U);

void vmem_platform_header_compiles() noexcept
{
    (void)voris::mem::build_cpu_architecture;
    (void)voris::mem::cache_line_assumption_available;
}
