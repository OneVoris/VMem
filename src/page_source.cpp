#include <voris/mem/page_source.hpp>

#include <voris/mem/checked_math.hpp>

#include <cerrno>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
#    define NOMINMAX
#    include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#    include <unistd.h>
#    include <sys/mman.h>
#    if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#        define MAP_ANONYMOUS MAP_ANON
#    endif
#endif

namespace voris::mem {

namespace {

struct huge_page_registry {
    std::mutex mutex{};
    std::unordered_map<void*, std::size_t> spans{};
};

huge_page_registry& huge_registry() {
    static huge_page_registry registry;
    return registry;
}

bool remember_huge_span(void* pointer, std::size_t size) noexcept {
    try {
        auto& registry = huge_registry();
        std::lock_guard lock{registry.mutex};
        const auto [_, inserted] = registry.spans.emplace(pointer, size);
        return inserted;
    } catch (...) {
        return false;
    }
}

bool is_recorded_huge_span(page_span span) noexcept {
    auto& registry = huge_registry();
    std::lock_guard lock{registry.mutex};
    const auto found = registry.spans.find(span.data);
    return found != registry.spans.end() && found->second == span.size;
}

bool forget_huge_span(page_span span) noexcept {
    auto& registry = huge_registry();
    std::lock_guard lock{registry.mutex};
    const auto found = registry.spans.find(span.data);
    if (found == registry.spans.end() || found->second != span.size) {
        return false;
    }
    registry.spans.erase(found);
    return true;
}

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

std::expected<std::size_t, errc> linux_huge_page_size_from_proc_meminfo() noexcept {
#if defined(__linux__) && defined(MAP_HUGETLB)
    constexpr std::size_t fallback_huge_page_size = 2U * 1024U * 1024U;
    FILE* file = std::fopen("/proc/meminfo", "r");
    if (file == nullptr) {
        return fallback_huge_page_size;
    }

    char line[256]{};
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        constexpr char key[] = "Hugepagesize:";
        if (std::strncmp(line, key, sizeof(key) - 1U) != 0) {
            continue;
        }

        char* cursor = line + sizeof(key) - 1U;
        while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
            ++cursor;
        }

        errno = 0;
        char* end = nullptr;
        const auto kibibytes = std::strtoull(cursor, &end, 10);
        std::fclose(file);
        if (cursor == end || errno == ERANGE || kibibytes == 0U) {
            return fallback_huge_page_size;
        }

        auto bytes = checked_mul(static_cast<std::size_t>(kibibytes), 1024U);
        if (!bytes || !is_power_of_two(*bytes)) {
            return fallback_huge_page_size;
        }
        return *bytes;
    }

    std::fclose(file);
    return fallback_huge_page_size;
#else
    return std::unexpected(errc::unsupported_platform);
#endif
}

std::expected<std::size_t, errc> platform_huge_page_size() noexcept {
#if defined(_WIN32)
    const auto value = GetLargePageMinimum();
    if (value == 0U) {
        return std::unexpected(errc::unsupported_platform);
    }
    if (!is_power_of_two(static_cast<std::size_t>(value))) {
        return std::unexpected(errc::invalid_alignment);
    }
    return static_cast<std::size_t>(value);
#elif defined(__linux__) && defined(MAP_HUGETLB)
    return linux_huge_page_size_from_proc_meminfo();
#else
    return std::unexpected(errc::unsupported_platform);
#endif
}

std::expected<page_span, errc> reserve_regular_pages(std::size_t reserve_size) noexcept {
#if defined(_WIN32)
    void* pointer = VirtualAlloc(nullptr, reserve_size, MEM_RESERVE, PAGE_NOACCESS);
    if (pointer == nullptr) {
        return std::unexpected(errc::out_of_memory);
    }
    return page_span{pointer, reserve_size};
#elif defined(__linux__) || defined(__APPLE__)
    void* pointer =
        mmap(nullptr, reserve_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pointer == MAP_FAILED) {
        return std::unexpected(errc::out_of_memory);
    }
    return page_span{pointer, reserve_size};
#else
    static_cast<void>(reserve_size);
    return std::unexpected(errc::unsupported_platform);
#endif
}

std::expected<page_span, errc> reserve_huge_pages(std::size_t size) noexcept {
    auto huge_size = platform_huge_page_size();
    if (!huge_size) {
        return std::unexpected(huge_size.error());
    }

    auto reserve_size = align_up(size, *huge_size);
    if (!reserve_size) {
        return std::unexpected(reserve_size.error());
    }

#if defined(_WIN32)
    void* pointer = VirtualAlloc(
        nullptr, *reserve_size, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    if (pointer == nullptr) {
        return std::unexpected(errc::out_of_memory);
    }
    if (!remember_huge_span(pointer, *reserve_size)) {
        static_cast<void>(VirtualFree(pointer, 0, MEM_RELEASE));
        return std::unexpected(errc::out_of_memory);
    }
    return page_span{pointer, *reserve_size};
#elif defined(__linux__) && defined(MAP_HUGETLB)
    void* pointer = mmap(nullptr,
                         *reserve_size,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                         -1,
                         0);
    if (pointer == MAP_FAILED) {
        return std::unexpected(errc::out_of_memory);
    }
    if (!remember_huge_span(pointer, *reserve_size)) {
        static_cast<void>(munmap(pointer, *reserve_size));
        return std::unexpected(errc::out_of_memory);
    }
    return page_span{pointer, *reserve_size};
#else
    return std::unexpected(errc::unsupported_platform);
#endif
}

} // namespace

#if defined(VORIS_VMEM_ENABLE_PAGE_SOURCE_TEST_HOOKS)

namespace test_hooks {

void clear_huge_span_registry_for_test() noexcept {
    auto& registry = huge_registry();
    std::lock_guard lock{registry.mutex};
    registry.spans.clear();
}

bool remember_huge_span_for_test(void* pointer, std::size_t size) noexcept {
    return remember_huge_span(pointer, size);
}

bool is_huge_span_recorded_for_test(page_span span) noexcept {
    return is_recorded_huge_span(span);
}

bool remove_then_restore_huge_span_for_test(page_span span) noexcept {
    if (!forget_huge_span(span)) {
        return false;
    }
    return remember_huge_span(span.data, span.size);
}

} // namespace test_hooks

#endif

std::expected<std::size_t, errc> os_page_source::page_size() const noexcept {
    return platform_page_size();
}

std::expected<std::size_t, errc> os_page_source::huge_page_size() const noexcept {
    return platform_huge_page_size();
}

std::expected<page_span, errc> os_page_source::reserve(std::size_t size) noexcept {
    return reserve(size, page_source_options{});
}

std::expected<page_span, errc>
os_page_source::reserve(std::size_t size, page_source_options options) noexcept {
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

    if (options.prefer_huge_pages) {
        auto huge_span = reserve_huge_pages(size);
        if (huge_span || !options.allow_huge_page_fallback) {
            return huge_span;
        }
    }

    return reserve_regular_pages(*reserve_size);
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

#if defined(_WIN32)
    if (is_recorded_huge_span(page_span{span.data, *commit_size})) {
        return {};
    }
    void* committed = VirtualAlloc(span.data, *commit_size, MEM_COMMIT, PAGE_READWRITE);
    if (committed == nullptr) {
        return std::unexpected(errc::out_of_memory);
    }
    DWORD old_protection{};
    if (VirtualProtect(span.data, *commit_size, PAGE_READWRITE, &old_protection) == 0) {
        return std::unexpected(errc::wrong_owner);
    }
    return {};
#elif defined(__linux__) || defined(__APPLE__)
    if (is_recorded_huge_span(page_span{span.data, *commit_size})) {
        return {};
    }
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

#if defined(_WIN32)
    if (is_recorded_huge_span(page_span{span.data, *decommit_size})) {
        return {};
    }
    DWORD old_protection{};
    if (VirtualProtect(span.data, *decommit_size, PAGE_NOACCESS, &old_protection) == 0) {
        return std::unexpected(errc::wrong_owner);
    }
    if (VirtualFree(span.data, *decommit_size, MEM_DECOMMIT) == 0) {
        return std::unexpected(errc::wrong_owner);
    }
    return {};
#elif defined(__linux__) || defined(__APPLE__)
    if (is_recorded_huge_span(page_span{span.data, *decommit_size})) {
        return {};
    }
    if (mprotect(span.data, *decommit_size, PROT_NONE) != 0) {
        return std::unexpected(errc::wrong_owner);
    }
#    if defined(__linux__)
    static_cast<void>(madvise(span.data, *decommit_size, MADV_DONTNEED));
#    elif defined(__APPLE__) && defined(MADV_FREE)
    static_cast<void>(madvise(span.data, *decommit_size, MADV_FREE));
#    endif
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

#if defined(_WIN32)
    {
        auto& registry = huge_registry();
        std::lock_guard lock{registry.mutex};
        const auto found = registry.spans.find(span.data);
        if (found != registry.spans.end()) {
            if (found->second != *release_size) {
                return std::unexpected(errc::wrong_owner);
            }
            if (VirtualFree(span.data, 0, MEM_RELEASE) == 0) {
                return std::unexpected(errc::wrong_owner);
            }
            registry.spans.erase(found);
            return {};
        }
    }
    if (VirtualFree(span.data, 0, MEM_RELEASE) == 0) {
        return std::unexpected(errc::wrong_owner);
    }
    return {};
#elif defined(__linux__) || defined(__APPLE__)
    {
        auto& registry = huge_registry();
        std::lock_guard lock{registry.mutex};
        const auto found = registry.spans.find(span.data);
        if (found != registry.spans.end()) {
            if (found->second != *release_size) {
                return std::unexpected(errc::wrong_owner);
            }
            if (munmap(span.data, *release_size) != 0) {
                return std::unexpected(errc::wrong_owner);
            }
            registry.spans.erase(found);
            return {};
        }
    }
    if (munmap(span.data, *release_size) != 0) {
        return std::unexpected(errc::wrong_owner);
    }
    return {};
#else
    return std::unexpected(errc::unsupported_platform);
#endif
}

} // namespace voris::mem
