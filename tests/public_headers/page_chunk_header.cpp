#include <voris/mem/page_chunk.hpp>

void vmem_page_chunk_header_compiles() noexcept {
    (void)voris::mem::page_allocation{};
}
