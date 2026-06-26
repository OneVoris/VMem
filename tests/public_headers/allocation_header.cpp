#include <voris/mem/allocation.hpp>

void vmem_allocation_header_compiles() noexcept
{
    (void)voris::mem::allocation{};
}
