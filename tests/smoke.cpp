#include <voris/mem/version.hpp>

#undef NDEBUG

#include <cassert>

#if defined(NDEBUG)
#    error "VMem assert-style tests require assertions enabled"
#endif

int main() {
    assert(voris::mem::version() == "0.1.0");
    return 0;
}
