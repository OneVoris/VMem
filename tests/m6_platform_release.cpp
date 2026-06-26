#include <voris/mem/checked_math.hpp>
#include <voris/mem/page_source.hpp>
#include <voris/mem/platform.hpp>

#undef NDEBUG

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#if defined(NDEBUG)
#error "VMem assert-style tests require assertions enabled"
#endif

namespace
{

void exercise_committed_span(voris::mem::os_page_source &pages, voris::mem::page_span span)
{
    assert(span.data != nullptr);
    assert(span.size != 0U);
    assert(pages.commit(span));

    auto *bytes = static_cast<volatile unsigned char *>(span.data);
    bytes[0] = 0x5AU;
    bytes[span.size - 1U] = 0xA5U;

    assert(pages.decommit(span));
    assert(pages.release(span));
}

} // namespace

int main()
{
    using voris::mem::cache_line_size;
    using voris::mem::errc;
    using voris::mem::is_power_of_two;
    using voris::mem::os_page_source;
    using voris::mem::page_source_options;

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
    static_assert(cache_line_size == 64U);
    static_assert(is_power_of_two(cache_line_size));
    static_assert(cache_line_size >= alignof(std::max_align_t));
    static_assert(voris::mem::cache_line_assumption_available);
#else
    static_assert(cache_line_size == 0U);
    static_assert(!voris::mem::cache_line_assumption_available);
#endif

    os_page_source pages;
    auto page_size = pages.page_size();
    assert(page_size);
    assert(*page_size != 0U);
    assert(is_power_of_two(*page_size));

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    auto span = pages.reserve(*page_size + 1U);
    assert(span);
    assert(span->size >= *page_size + 1U);
    assert(reinterpret_cast<std::uintptr_t>(span->data) % *page_size == 0U);
    exercise_committed_span(pages, *span);

    page_source_options disabled{};
    assert(!disabled.prefer_huge_pages);
    assert(disabled.allow_huge_page_fallback);
    auto normal = pages.reserve(*page_size, disabled);
    assert(normal);
    exercise_committed_span(pages, *normal);

    auto huge_size = pages.huge_page_size();
    if (huge_size)
    {
        assert(*huge_size >= *page_size);
        assert(is_power_of_two(*huge_size));
    }
    else
    {
        assert(huge_size.error() == errc::unsupported_platform);
    }

    page_source_options prefer_huge{
        .prefer_huge_pages = true,
        .allow_huge_page_fallback = true,
    };
    auto preferred = pages.reserve(*page_size, prefer_huge);
    assert(preferred);
    exercise_committed_span(pages, *preferred);

    page_source_options require_huge{
        .prefer_huge_pages = true,
        .allow_huge_page_fallback = false,
    };
    auto required = pages.reserve(*page_size, require_huge);
    if (required)
    {
        exercise_committed_span(pages, *required);
    }
    else
    {
        assert(required.error() == errc::unsupported_platform || required.error() == errc::out_of_memory ||
               required.error() == errc::invalid_alignment);
    }
#else
    auto span = pages.reserve(*page_size);
    assert(!span);
    assert(span.error() == errc::unsupported_platform);
#endif

    auto impossible = pages.reserve(std::numeric_limits<std::size_t>::max());
    assert(!impossible);
    assert(impossible.error() == errc::size_overflow);

    return 0;
}
