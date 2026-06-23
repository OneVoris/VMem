#include <voris/mem/page_source.hpp>

#include <voris/mem/checked_math.hpp>

#include <cstddef>

#if defined(_WIN32)
#    define NOMINMAX
#    include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#    include <unistd.h>
#    if defined(__linux__)
#        include <sys/mman.h>
#    endif
#endif

namespace voris::mem {

namespace {

std::expected<std::size_t, errc> platform_page_size() noexcept {
#if defined(_WIN32)
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return static_cast<std::size_t>(info.dwPageSize);
#elif defined(__unix__) || defined(__APPLE__)
    const long value = sysconf(_SC_PAGESIZE);
    if (value <= 0) {
        return std::unexpected(errc::unsupported_platform);
    }
    return static_cast<std::size_t>(value);
#else
    return std::unexpected(errc::unsupported_platform);
#endif
}

} // namespace

std::expected<std::size_t, errc> os_page_source::page_size() const noexcept {
    return platform_page_size();
}

std::expected<page_span, errc> os_page_source::reserve(std::size_t size) noexcept {
    if (size == 0) {
        return page_span{};
    }

    auto page_size_value = platform_page_size();
    if (!page_size_value) {
        return std::unexpected(page_size_value.error());
    }

    auto reserve_size = align_up(size, *page_size_value);
    if (!reserve_size) {
        return std::unexpected(reserve_size.error());
    }

#if defined(__linux__)
    void* pointer =
        mmap(nullptr, *reserve_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pointer == MAP_FAILED) {
        return std::unexpected(errc::out_of_memory);
    }
    return page_span{pointer, *reserve_size};
#else
    return std::unexpected(errc::unsupported_platform);
#endif
}

std::expected<void, errc> os_page_source::commit(page_span span) noexcept {
    if (span.data == nullptr && span.size == 0) {
        return {};
    }
    if (span.data == nullptr) {
        return std::unexpected(errc::wrong_owner);
    }

    auto page_size_value = platform_page_size();
    if (!page_size_value) {
        return std::unexpected(page_size_value.error());
    }
    auto commit_size = align_up(span.size, *page_size_value);
    if (!commit_size) {
        return std::unexpected(commit_size.error());
    }

#if defined(__linux__)
    if (mprotect(span.data, *commit_size, PROT_READ | PROT_WRITE) != 0) {
        return std::unexpected(errc::out_of_memory);
    }
    return {};
#else
    return std::unexpected(errc::unsupported_platform);
#endif
}

std::expected<void, errc> os_page_source::decommit(page_span span) noexcept {
    if (span.data == nullptr && span.size == 0) {
        return {};
    }
    if (span.data == nullptr) {
        return std::unexpected(errc::wrong_owner);
    }

    auto page_size_value = platform_page_size();
    if (!page_size_value) {
        return std::unexpected(page_size_value.error());
    }
    auto decommit_size = align_up(span.size, *page_size_value);
    if (!decommit_size) {
        return std::unexpected(decommit_size.error());
    }

#if defined(__linux__)
    if (mprotect(span.data, *decommit_size, PROT_NONE) != 0) {
        return std::unexpected(errc::wrong_owner);
    }
    static_cast<void>(madvise(span.data, *decommit_size, MADV_DONTNEED));
    return {};
#else
    return std::unexpected(errc::unsupported_platform);
#endif
}

std::expected<void, errc> os_page_source::release(page_span span) noexcept {
    if (span.data == nullptr && span.size == 0) {
        return {};
    }
    if (span.data == nullptr) {
        return std::unexpected(errc::wrong_owner);
    }

    auto page_size_value = platform_page_size();
    if (!page_size_value) {
        return std::unexpected(page_size_value.error());
    }
    auto release_size = align_up(span.size, *page_size_value);
    if (!release_size) {
        return std::unexpected(release_size.error());
    }

#if defined(__linux__)
    if (munmap(span.data, *release_size) != 0) {
        return std::unexpected(errc::wrong_owner);
    }
    return {};
#else
    return std::unexpected(errc::unsupported_platform);
#endif
}

} // namespace voris::mem
