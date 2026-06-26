#pragma once

#include <voris/mem/page_source.hpp>

#include <cstddef>

#if defined(VORIS_VMEM_ENABLE_PAGE_SOURCE_TEST_HOOKS)

namespace voris::mem::test_hooks
{

void clear_huge_span_registry_for_test() noexcept;
bool remember_huge_span_for_test(void *pointer, std::size_t size) noexcept;
bool is_huge_span_recorded_for_test(page_span span) noexcept;
bool remove_then_restore_huge_span_for_test(page_span span) noexcept;

} // namespace voris::mem::test_hooks

#endif
