#pragma once

#include <voris/mem/buffer.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>

#include <cstddef>
#include <expected>
#include <utility>

namespace voris::mem {

class unique_buffer {
public:
    constexpr unique_buffer() noexcept = default;

    unique_buffer(const unique_buffer&) = delete;
    unique_buffer& operator=(const unique_buffer&) = delete;

    constexpr unique_buffer(unique_buffer&& other) noexcept
        : resource_(other.resource_),
          block_(other.block_),
          size_(other.size_),
          has_resource_(other.has_resource_) {
        other.block_ = {};
        other.size_ = 0U;
        other.has_resource_ = false;
    }

    unique_buffer& operator=(unique_buffer&& other) noexcept {
        if (this != &other) {
            auto released = reset();
            if (!released) {
                return *this;
            }
            resource_ = other.resource_;
            block_ = other.block_;
            size_ = other.size_;
            has_resource_ = other.has_resource_;
            other.block_ = {};
            other.size_ = 0U;
            other.has_resource_ = false;
        }
        return *this;
    }

    ~unique_buffer() noexcept {
        static_cast<void>(reset());
    }

    [[nodiscard]] std::byte* data() noexcept {
        return static_cast<std::byte*>(block_.data);
    }

    [[nodiscard]] const std::byte* data() const noexcept {
        return static_cast<const std::byte*>(block_.data);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return block_.size;
    }

    [[nodiscard]] std::size_t alignment() const noexcept {
        return block_.alignment;
    }

    [[nodiscard]] mutable_buffer mutable_view() noexcept {
        return mutable_buffer{data(), size_};
    }

    [[nodiscard]] const_buffer const_view() const noexcept {
        return const_buffer{data(), size_};
    }

    [[nodiscard]] std::expected<void, errc> resize(std::size_t size) noexcept {
        if (size > block_.size) {
            return std::unexpected(errc::size_overflow);
        }
        size_ = size;
        return {};
    }

    [[nodiscard]] std::expected<void, errc> reset() noexcept {
        if (!has_resource_) {
            block_ = {};
            size_ = 0U;
            return {};
        }
        auto released = resource_.deallocate(block_);
        if (!released) {
            return std::unexpected(released.error());
        }
        block_ = {};
        size_ = 0U;
        has_resource_ = false;
        return {};
    }

private:
    friend std::expected<unique_buffer, errc>
    make_unique_buffer(resource_ref, std::size_t, std::size_t, memory_tag) noexcept;

    unique_buffer(resource_ref resource, allocation block) noexcept
        : resource_(resource),
          block_(block),
          has_resource_(true) {}

    resource_ref resource_{};
    allocation block_{};
    std::size_t size_{};
    bool has_resource_{};
};

[[nodiscard]] inline std::expected<unique_buffer, errc>
make_unique_buffer(resource_ref resource,
                   std::size_t capacity,
                   std::size_t alignment,
                   memory_tag tag = default_memory_tag) noexcept {
    auto validated_alignment = align_up(0U, alignment);
    if (!validated_alignment) {
        return std::unexpected(validated_alignment.error());
    }
    auto allocated = resource.allocate(capacity, alignment, tag);
    if (!allocated) {
        return std::unexpected(allocated.error());
    }
    return unique_buffer{resource, *allocated};
}

} // namespace voris::mem
