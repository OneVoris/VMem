#include <voris/mem/usage.hpp>

void vmem_usage_header_compiles() noexcept {
    (void)voris::mem::usage_snapshot{};
}
