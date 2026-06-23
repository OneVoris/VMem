#include <voris/mem/page_source.hpp>

void vmem_page_source_header_compiles() noexcept {
    (void)voris::mem::page_span{};
}
