#pragma once

#include <voris/mem/buffer.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/shared_buffer.hpp>
#include <voris/mem/unique_buffer.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <expected>
#include <utility>
#include <vector>

namespace voris::mem
{

namespace detail
{

struct buffer_chain_segment
{
    const_buffer view{};
    unique_buffer unique{};
    shared_buffer shared{};

    [[nodiscard]] static buffer_chain_segment borrowed(const_buffer view) noexcept
    {
        return buffer_chain_segment{.view = view};
    }

    [[nodiscard]] static buffer_chain_segment owned(unique_buffer &&buffer) noexcept
    {
        buffer_chain_segment segment;
        segment.unique = std::move(buffer);
        segment.view = segment.unique.const_view();
        return segment;
    }

    [[nodiscard]] static buffer_chain_segment shared_owner(shared_buffer &&buffer) noexcept
    {
        buffer_chain_segment segment;
        segment.shared = std::move(buffer);
        segment.view = segment.shared.const_view();
        return segment;
    }
};

} // namespace detail

class buffer_chain
{
  public:
    buffer_chain() = default;

    buffer_chain(const buffer_chain &) = delete;
    buffer_chain &operator=(const buffer_chain &) = delete;

    buffer_chain(buffer_chain &&other) noexcept
    {
        move_from(std::move(other));
    }

    buffer_chain &operator=(buffer_chain &&other) noexcept
    {
        if (this != &other)
        {
            buffer_chain old{std::move(*this)};
            static_cast<void>(old);
            move_from(std::move(other));
        }
        return *this;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return total_size_;
    }

    [[nodiscard]] std::size_t segment_count() const noexcept
    {
        return using_spill() ? spill_segments_.size() : inline_count_;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return total_size_ == 0U;
    }

    [[nodiscard]] const_buffer segment(std::size_t index) const noexcept
    {
        if (using_spill())
        {
            return index < spill_segments_.size() ? spill_segments_[index].view : const_buffer{};
        }
        return index < inline_count_ ? inline_segments_[index].view : const_buffer{};
    }

    [[nodiscard]] std::expected<void, errc> append(const_buffer view)
    {
        if (view.size == 0U)
        {
            return {};
        }
        if (!view.valid())
        {
            return std::unexpected(errc::wrong_owner);
        }
        auto grown = checked_add(total_size_, view.size);
        if (!grown)
        {
            return std::unexpected(grown.error());
        }
        auto pushed = push_back(detail::buffer_chain_segment::borrowed(view));
        if (!pushed)
        {
            return std::unexpected(pushed.error());
        }
        total_size_ = *grown;
        return {};
    }

    [[nodiscard]] std::expected<void, errc> prepend(const_buffer view)
    {
        if (view.size == 0U)
        {
            return {};
        }
        if (!view.valid())
        {
            return std::unexpected(errc::wrong_owner);
        }
        auto grown = checked_add(total_size_, view.size);
        if (!grown)
        {
            return std::unexpected(grown.error());
        }
        auto pushed = push_front(detail::buffer_chain_segment::borrowed(view));
        if (!pushed)
        {
            return std::unexpected(pushed.error());
        }
        total_size_ = *grown;
        return {};
    }

    [[nodiscard]] std::expected<void, errc> append(unique_buffer &&buffer)
    {
        auto view = buffer.const_view();
        if (view.size == 0U)
        {
            return {};
        }
        auto grown = checked_add(total_size_, view.size);
        if (!grown)
        {
            return std::unexpected(grown.error());
        }
        auto pushed = push_back(detail::buffer_chain_segment::owned(std::move(buffer)));
        if (!pushed)
        {
            return std::unexpected(pushed.error());
        }
        total_size_ = *grown;
        return {};
    }

    [[nodiscard]] std::expected<void, errc> append(shared_buffer &&buffer)
    {
        auto view = buffer.const_view();
        if (view.size == 0U)
        {
            return {};
        }
        auto grown = checked_add(total_size_, view.size);
        if (!grown)
        {
            return std::unexpected(grown.error());
        }
        auto pushed = push_back(detail::buffer_chain_segment::shared_owner(std::move(buffer)));
        if (!pushed)
        {
            return std::unexpected(pushed.error());
        }
        total_size_ = *grown;
        return {};
    }

    [[nodiscard]] std::expected<void, errc> consume(std::size_t count)
    {
        if (count > total_size_)
        {
            return std::unexpected(errc::size_overflow);
        }
        auto remaining_total = checked_sub(total_size_, count);
        if (!remaining_total)
        {
            return std::unexpected(remaining_total.error());
        }
        total_size_ = *remaining_total;
        while (count > 0U && segment_count() > 0U)
        {
            auto &front = segment_at(0U);
            if (count < front.view.size)
            {
                front.view.data += count;
                auto remaining_view = checked_sub(front.view.size, count);
                if (!remaining_view)
                {
                    return std::unexpected(remaining_view.error());
                }
                front.view.size = *remaining_view;
                return {};
            }
            auto remaining_count = checked_sub(count, front.view.size);
            if (!remaining_count)
            {
                return std::unexpected(remaining_count.error());
            }
            count = *remaining_count;
            pop_front();
        }
        return {};
    }

    [[nodiscard]] std::expected<void, errc> trim(std::size_t count)
    {
        if (count > total_size_)
        {
            return std::unexpected(errc::size_overflow);
        }
        auto remaining_total = checked_sub(total_size_, count);
        if (!remaining_total)
        {
            return std::unexpected(remaining_total.error());
        }
        total_size_ = *remaining_total;
        while (count > 0U && segment_count() > 0U)
        {
            auto last_index = checked_sub(segment_count(), 1U);
            if (!last_index)
            {
                return std::unexpected(last_index.error());
            }
            auto &back = segment_at(*last_index);
            if (count < back.view.size)
            {
                auto remaining_view = checked_sub(back.view.size, count);
                if (!remaining_view)
                {
                    return std::unexpected(remaining_view.error());
                }
                back.view.size = *remaining_view;
                return {};
            }
            auto remaining_count = checked_sub(count, back.view.size);
            if (!remaining_count)
            {
                return std::unexpected(remaining_count.error());
            }
            count = *remaining_count;
            pop_back();
        }
        return {};
    }

    [[nodiscard]] std::expected<buffer_chain, errc> slice(std::size_t offset, std::size_t count) const
    {
        auto end_offset = checked_add(offset, count);
        if (!end_offset || *end_offset > total_size_)
        {
            return std::unexpected(errc::size_overflow);
        }
        buffer_chain output;
        std::size_t skipped{};
        for (std::size_t index = 0U; index < segment_count() && output.size() < count; ++index)
        {
            auto current = segment(index);
            auto segment_end = checked_add(skipped, current.size);
            if (!segment_end)
            {
                return std::unexpected(segment_end.error());
            }
            if (*segment_end <= offset)
            {
                skipped = *segment_end;
                continue;
            }
            std::size_t local_offset{};
            if (offset > skipped)
            {
                auto checked_local_offset = checked_sub(offset, skipped);
                if (!checked_local_offset)
                {
                    return std::unexpected(checked_local_offset.error());
                }
                local_offset = *checked_local_offset;
            }
            auto available = checked_sub(current.size, local_offset);
            if (!available)
            {
                return std::unexpected(available.error());
            }
            auto needed = checked_sub(count, output.size());
            if (!needed)
            {
                return std::unexpected(needed.error());
            }
            const auto take = *available < *needed ? *available : *needed;
            auto part = current.slice(local_offset, take);
            if (!part)
            {
                return std::unexpected(part.error());
            }
            auto appended = output.append(*part);
            if (!appended)
            {
                return std::unexpected(appended.error());
            }
            skipped = *segment_end;
        }
        return output;
    }

    [[nodiscard]] std::expected<unique_buffer, errc> coalesce(resource_ref resource, std::size_t max_size,
                                                              std::size_t alignment,
                                                              memory_tag tag = default_memory_tag) const noexcept
    {
        if (total_size_ > max_size)
        {
            return std::unexpected(errc::budget_exceeded);
        }
        auto output = make_unique_buffer(resource, total_size_, alignment, tag);
        if (!output)
        {
            return std::unexpected(output.error());
        }
        auto resized = output->resize(total_size_);
        if (!resized)
        {
            return std::unexpected(resized.error());
        }
        std::byte *cursor = output->data();
        for (std::size_t index = 0U; index < segment_count(); ++index)
        {
            auto current = segment(index);
            if (current.size == 0U)
            {
                continue;
            }
            std::memcpy(cursor, current.data, current.size);
            cursor += current.size;
        }
        return output;
    }

  private:
    static constexpr std::size_t inline_capacity = 4U;

    void clear_segments() noexcept
    {
        for (std::size_t index = 0U; index < inline_count_; ++index)
        {
            inline_segments_[index] = detail::buffer_chain_segment{};
        }
        inline_count_ = 0U;
        spill_segments_.clear();
        total_size_ = 0U;
    }

    void move_from(buffer_chain &&other) noexcept
    {
        inline_segments_ = std::move(other.inline_segments_);
        inline_count_ = other.inline_count_;
        spill_segments_ = std::move(other.spill_segments_);
        total_size_ = other.total_size_;
        other.inline_count_ = 0U;
        other.spill_segments_.clear();
        other.total_size_ = 0U;
    }

    [[nodiscard]] bool using_spill() const noexcept
    {
        return !spill_segments_.empty();
    }

    void spill_inline()
    {
        if (using_spill())
        {
            return;
        }
        auto spill_capacity = checked_add(inline_capacity, 1U);
        if (!spill_capacity)
        {
            return;
        }
        spill_segments_.reserve(*spill_capacity);
        for (std::size_t index = 0U; index < inline_count_; ++index)
        {
            spill_segments_.push_back(std::move(inline_segments_[index]));
        }
        inline_count_ = 0U;
    }

    [[nodiscard]] std::expected<void, errc> push_back(detail::buffer_chain_segment &&segment)
    {
        if (!using_spill() && inline_count_ < inline_capacity)
        {
            inline_segments_[inline_count_++] = std::move(segment);
            return {};
        }
        try
        {
            spill_inline();
            spill_segments_.push_back(std::move(segment));
        }
        catch (...)
        {
            return std::unexpected(errc::out_of_memory);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, errc> push_front(detail::buffer_chain_segment &&segment)
    {
        if (!using_spill() && inline_count_ < inline_capacity)
        {
            for (std::size_t index = inline_count_; index > 0U; --index)
            {
                auto previous = checked_sub(index, 1U);
                if (!previous)
                {
                    return std::unexpected(previous.error());
                }
                inline_segments_[index] = std::move(inline_segments_[*previous]);
            }
            inline_segments_[0] = std::move(segment);
            ++inline_count_;
            return {};
        }
        try
        {
            spill_inline();
            spill_segments_.insert(spill_segments_.begin(), std::move(segment));
        }
        catch (...)
        {
            return std::unexpected(errc::out_of_memory);
        }
        return {};
    }

    detail::buffer_chain_segment &segment_at(std::size_t index) noexcept
    {
        return using_spill() ? spill_segments_[index] : inline_segments_[index];
    }

    void pop_front()
    {
        if (using_spill())
        {
            spill_segments_.erase(spill_segments_.begin());
            return;
        }
        if (inline_count_ == 0U)
        {
            return;
        }
        for (std::size_t index = 1U; index < inline_count_; ++index)
        {
            auto previous = checked_sub(index, 1U);
            if (!previous)
            {
                return;
            }
            inline_segments_[*previous] = std::move(inline_segments_[index]);
        }
        auto last_index = checked_sub(inline_count_, 1U);
        if (!last_index)
        {
            return;
        }
        inline_segments_[*last_index] = detail::buffer_chain_segment{};
        --inline_count_;
    }

    void pop_back()
    {
        if (using_spill())
        {
            spill_segments_.pop_back();
            return;
        }
        if (inline_count_ == 0U)
        {
            return;
        }
        auto last_index = checked_sub(inline_count_, 1U);
        if (!last_index)
        {
            return;
        }
        inline_segments_[*last_index] = detail::buffer_chain_segment{};
        --inline_count_;
    }

    std::array<detail::buffer_chain_segment, inline_capacity> inline_segments_{};
    std::size_t inline_count_{};
    std::vector<detail::buffer_chain_segment> spill_segments_{};
    std::size_t total_size_{};
};

} // namespace voris::mem
