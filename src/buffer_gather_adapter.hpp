#pragma once

#include <voris/mem/buffer_chain.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <vector>

#if defined(_WIN32)
#    if !defined(NOMINMAX)
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#elif defined(__unix__) || defined(__APPLE__)
#    include <sys/uio.h>
#endif

namespace voris::mem::detail {

struct neutral_gather_entry {
    const void* data{};
    std::size_t size{};
};

[[nodiscard]] inline std::expected<std::vector<neutral_gather_entry>, errc>
to_neutral_gather(const buffer_chain& chain, std::size_t max_segments) {
    if (chain.segment_count() > max_segments) {
        return std::unexpected(errc::budget_exceeded);
    }
    std::vector<neutral_gather_entry> entries;
    try {
        entries.reserve(chain.segment_count());
        for (std::size_t index = 0U; index < chain.segment_count(); ++index) {
            auto segment = chain.segment(index);
            entries.push_back(neutral_gather_entry{segment.data, segment.size});
        }
    } catch (...) {
        return std::unexpected(errc::out_of_memory);
    }
    return entries;
}

#if defined(_WIN32)
[[nodiscard]] inline std::expected<std::vector<WSABUF>, errc>
to_windows_wsabuf(const buffer_chain& chain, std::size_t max_segments) {
    if (chain.segment_count() > max_segments) {
        return std::unexpected(errc::budget_exceeded);
    }
    std::vector<WSABUF> entries;
    try {
        entries.reserve(chain.segment_count());
        for (std::size_t index = 0U; index < chain.segment_count(); ++index) {
            auto segment = chain.segment(index);
            if (segment.size > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())) {
                return std::unexpected(errc::size_overflow);
            }
            entries.push_back(WSABUF{
                .len = static_cast<ULONG>(segment.size),
                .buf = const_cast<CHAR*>(reinterpret_cast<const CHAR*>(segment.data)),
            });
        }
    } catch (...) {
        return std::unexpected(errc::out_of_memory);
    }
    return entries;
}
#endif

#if defined(__unix__) || defined(__APPLE__)
[[nodiscard]] inline std::expected<std::vector<iovec>, errc>
to_posix_iovec(const buffer_chain& chain, std::size_t max_segments) {
    if (chain.segment_count() > max_segments) {
        return std::unexpected(errc::budget_exceeded);
    }
    std::vector<iovec> entries;
    try {
        entries.reserve(chain.segment_count());
        for (std::size_t index = 0U; index < chain.segment_count(); ++index) {
            auto segment = chain.segment(index);
            entries.push_back(iovec{
                .iov_base = const_cast<std::byte*>(segment.data),
                .iov_len = segment.size,
            });
        }
    } catch (...) {
        return std::unexpected(errc::out_of_memory);
    }
    return entries;
}
#endif

} // namespace voris::mem::detail
