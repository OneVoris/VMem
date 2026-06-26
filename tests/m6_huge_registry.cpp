#include "page_source_test_hooks.hpp"

#undef NDEBUG

#include <cassert>
#include <cstddef>

#if defined(NDEBUG)
#error "VMem assert-style tests require assertions enabled"
#endif

int main()
{
    alignas(4096) std::byte storage[4096]{};
    const auto span = voris::mem::page_span{storage, sizeof(storage)};

    voris::mem::test_hooks::clear_huge_span_registry_for_test();

    assert(voris::mem::test_hooks::remember_huge_span_for_test(span.data, span.size));
    assert(!voris::mem::test_hooks::remember_huge_span_for_test(span.data, span.size));
    assert(voris::mem::test_hooks::is_huge_span_recorded_for_test(span));

    assert(voris::mem::test_hooks::remove_then_restore_huge_span_for_test(span));
    assert(voris::mem::test_hooks::is_huge_span_recorded_for_test(span));

    voris::mem::test_hooks::clear_huge_span_registry_for_test();
    return 0;
}
