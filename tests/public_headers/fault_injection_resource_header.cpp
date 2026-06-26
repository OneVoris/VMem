#include <voris/mem/fault_injection_resource.hpp>

void vmem_fault_injection_resource_header_compiles() noexcept
{
    (void)voris::mem::fault_injection_options{};
}
