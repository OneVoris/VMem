#pragma once

#include <voris/mem/checked_math.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/page_source.hpp>

#include <cstddef>
#include <expected>

namespace voris::mem
{

enum class page_allocation_kind
{
    chunk,
    direct,
};

struct page_allocation
{
    void *data{};
    std::size_t size{};
    page_allocation_kind kind{page_allocation_kind::chunk};
};

struct page_chunk_options
{
    std::size_t page_size{};
    std::size_t chunk_size{64 * 1024};
    std::size_t direct_threshold{64 * 1024};
};

template <typename PageSource> class basic_page_chunk_manager
{
  public:
    explicit basic_page_chunk_manager(PageSource &source, page_chunk_options options = {}) noexcept
        : source_(&source), options_(options)
    {
    }

    [[nodiscard]] std::expected<page_allocation, errc> allocate_span(std::size_t requested_size) noexcept
    {
        if (requested_size == 0)
        {
            return page_allocation{};
        }

        auto page_size = resolved_page_size();
        if (!page_size)
        {
            return std::unexpected(page_size.error());
        }

        auto chunk_size = align_up(options_.chunk_size, *page_size);
        if (!chunk_size)
        {
            return std::unexpected(chunk_size.error());
        }

        const bool use_direct = requested_size > *chunk_size || requested_size >= options_.direct_threshold;
        auto reserve_size = use_direct ? align_up(requested_size, *page_size) : chunk_size;
        if (!reserve_size)
        {
            return std::unexpected(reserve_size.error());
        }

        auto span = source_->reserve(*reserve_size);
        if (!span)
        {
            return std::unexpected(span.error());
        }

        auto committed = source_->commit(*span);
        if (!committed)
        {
            static_cast<void>(source_->release(*span));
            return std::unexpected(committed.error());
        }

        return page_allocation{
            .data = span->data,
            .size = span->size,
            .kind = use_direct ? page_allocation_kind::direct : page_allocation_kind::chunk,
        };
    }

    [[nodiscard]] std::expected<void, errc> release_span(page_allocation allocation) noexcept
    {
        if (allocation.data == nullptr && allocation.size == 0)
        {
            return {};
        }

        page_span span{allocation.data, allocation.size};
        auto decommitted = source_->decommit(span);
        auto released = source_->release(span);
        if (!released)
        {
            return std::unexpected(released.error());
        }
        if (!decommitted)
        {
            return std::unexpected(decommitted.error());
        }
        return {};
    }

  private:
    [[nodiscard]] std::expected<std::size_t, errc> resolved_page_size() noexcept
    {
        if (options_.page_size != 0)
        {
            if (!is_power_of_two(options_.page_size))
            {
                return std::unexpected(errc::invalid_alignment);
            }
            return options_.page_size;
        }

        auto discovered = source_->page_size();
        if (!discovered)
        {
            return std::unexpected(discovered.error());
        }
        if (!is_power_of_two(*discovered))
        {
            return std::unexpected(errc::invalid_alignment);
        }
        return *discovered;
    }

    PageSource *source_{};
    page_chunk_options options_{};
};

using page_chunk_manager = basic_page_chunk_manager<os_page_source>;

} // namespace voris::mem
