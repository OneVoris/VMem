#include <voris/mem/error.hpp>

void vmem_error_header_compiles() noexcept {
    (void)voris::mem::to_string(voris::mem::errc::out_of_memory);
}
