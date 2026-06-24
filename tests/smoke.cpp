#include <voris/mem/version.hpp>

#undef NDEBUG

#include <cassert>

#if defined(NDEBUG)
#    error "VMem assert-style tests require assertions enabled"
#endif

int main() {
    assert(!voris::mem::version().empty());
    return 0;
}
