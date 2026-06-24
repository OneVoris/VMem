#pragma once

#include <voris/mem/error.hpp>
#include <voris/mem/export.hpp>

#include <cstddef>
#include <expected>

namespace voris::mem {

struct page_span {
    void* data{};
    std::size_t size{};
};

struct page_source_options {
    bool prefer_huge_pages{};
    bool allow_huge_page_fallback{true};
};

class VORIS_VMEM_API os_page_source {
public:
    [[nodiscard]] std::expected<std::size_t, errc> page_size() const noexcept;
    [[nodiscard]] std::expected<std::size_t, errc> huge_page_size() const noexcept;
    [[nodiscard]] std::expected<page_span, errc> reserve(std::size_t size) noexcept;
    [[nodiscard]] std::expected<page_span, errc>
    reserve(std::size_t size, page_source_options options) noexcept;
    [[nodiscard]] std::expected<void, errc> commit(page_span span) noexcept;
    [[nodiscard]] std::expected<void, errc> decommit(page_span span) noexcept;
    [[nodiscard]] std::expected<void, errc> release(page_span span) noexcept;
};

} // namespace voris::mem
