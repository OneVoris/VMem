#include <voris/mem/checked_math.hpp>

void vmem_checked_math_header_compiles() noexcept
{
    (void)voris::mem::is_power_of_two(1);
    (void)voris::mem::checked_sub(1, 0);
}
