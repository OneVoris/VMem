#include <voris/mem/tag.hpp>

void vmem_tag_header_compiles() noexcept
{
    (void)voris::mem::memory_tag{"public-header"};
}
