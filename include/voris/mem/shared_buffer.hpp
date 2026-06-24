#pragma once

#include <voris/mem/buffer.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace voris::mem {

struct shared_buffer_options {
    std::size_t owner_generation{1U};
    std::size_t max_refcount{std::numeric_limits<std::size_t>::max()};
    bool allow_cross_thread_final_release{true};
};

namespace detail {

struct alignas(std::max_align_t) shared_buffer_control {
    std::atomic<std::size_t> refcount{1U};
    std::size_t size{};
    std::size_t capacity{};
    std::size_t payload_offset{};
    std::size_t payload_alignment{};
    std::size_t owner_generation{};
    std::size_t max_refcount{};
    bool final_release_uses_remote{};
    resource_ref resource{};
    allocation block{};
};

static_assert(std::is_trivially_destructible_v<shared_buffer_control>);

} // namespace detail

class shared_buffer {
public:
    constexpr shared_buffer() noexcept = default;

    shared_buffer(const shared_buffer&) = delete;
    shared_buffer& operator=(const shared_buffer&) = delete;

    constexpr shared_buffer(shared_buffer&& other) noexcept
        : control_(other.control_),
          handle_generation_(other.handle_generation_) {
        other.control_ = nullptr;
        other.handle_generation_ = 0U;
    }

    shared_buffer& operator=(shared_buffer&& other) noexcept {
        if (this != &other) {
            auto released = reset();
            if (!released) {
                return *this;
            }
            control_ = other.control_;
            handle_generation_ = other.handle_generation_;
            other.control_ = nullptr;
            other.handle_generation_ = 0U;
        }
        return *this;
    }

    ~shared_buffer() noexcept {
        static_cast<void>(reset());
    }

    [[nodiscard]] std::expected<shared_buffer, errc> clone() const noexcept {
        if (control_ == nullptr) {
            return shared_buffer{};
        }
        if (handle_generation_ != control_->owner_generation) {
            return std::unexpected(errc::wrong_owner);
        }
        auto current = control_->refcount.load(std::memory_order_acquire);
        for (;;) {
            if (current >= control_->max_refcount) {
                return std::unexpected(errc::size_overflow);
            }
            auto next = checked_add(current, 1U);
            if (!next) {
                return std::unexpected(next.error());
            }
            if (control_->refcount.compare_exchange_weak(
                    current, *next, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return shared_buffer{control_, handle_generation_};
            }
        }
    }

    [[nodiscard]] std::byte* data() noexcept {
        return control_ == nullptr ? nullptr : reinterpret_cast<std::byte*>(control_) + control_->payload_offset;
    }

    [[nodiscard]] const std::byte* data() const noexcept {
        return control_ == nullptr ? nullptr : reinterpret_cast<const std::byte*>(control_) + control_->payload_offset;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return control_ == nullptr ? 0U : control_->size;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return control_ == nullptr ? 0U : control_->capacity;
    }

    [[nodiscard]] std::size_t alignment() const noexcept {
        return control_ == nullptr ? 0U : control_->payload_alignment;
    }

    [[nodiscard]] std::size_t owner_generation() const noexcept {
        return handle_generation_;
    }

    [[nodiscard]] std::size_t use_count() const noexcept {
        return control_ == nullptr ? 0U : control_->refcount.load(std::memory_order_acquire);
    }

    [[nodiscard]] mutable_buffer mutable_view() noexcept {
        return mutable_buffer{data(), size()};
    }

    [[nodiscard]] const_buffer const_view() const noexcept {
        return const_buffer{data(), size()};
    }

    [[nodiscard]] std::expected<void, errc> resize(std::size_t size) noexcept {
        if (control_ == nullptr) {
            return size == 0U ? std::expected<void, errc>{}
                              : std::expected<void, errc>{std::unexpected(errc::size_overflow)};
        }
        if (size > control_->capacity) {
            return std::unexpected(errc::size_overflow);
        }
        control_->size = size;
        return {};
    }

    [[nodiscard]] std::expected<void, errc> reset() noexcept {
        auto* control = control_;
        if (control == nullptr) {
            return {};
        }
        if (handle_generation_ != control->owner_generation) {
            return std::unexpected(errc::wrong_owner);
        }
        auto current = control->refcount.load(std::memory_order_acquire);
        for (;;) {
            if (current == 0U) {
                return std::unexpected(errc::wrong_owner);
            }
            if (current == 1U) {
                break;
            }
            auto next = checked_sub(current, 1U);
            if (!next) {
                return std::unexpected(next.error());
            }
            if (control->refcount.compare_exchange_weak(
                    current, *next, std::memory_order_acq_rel, std::memory_order_acquire)) {
                control_ = nullptr;
                handle_generation_ = 0U;
                return {};
            }
        }
        if (current == 0U) {
            return std::unexpected(errc::wrong_owner);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        const auto block = control->block;
        auto resource = control->resource;
        const bool use_remote = control->final_release_uses_remote;
        auto released = use_remote ? resource.remote_deallocate(block) : resource.deallocate(block);
        if (!released) {
            return std::unexpected(released.error());
        }
        control_ = nullptr;
        handle_generation_ = 0U;
        return {};
    }

#if defined(VORIS_VMEM_ENABLE_TEST_HOOKS)
    void test_override_handle_generation(std::size_t generation) noexcept {
        handle_generation_ = generation;
    }
#endif

private:
    friend std::expected<shared_buffer, errc>
    make_shared_buffer(resource_ref,
                       std::size_t,
                       std::size_t,
                       memory_tag,
                       shared_buffer_options) noexcept;

    constexpr shared_buffer(detail::shared_buffer_control* control,
                            std::size_t handle_generation) noexcept
        : control_(control),
          handle_generation_(handle_generation) {}

    detail::shared_buffer_control* control_{};
    std::size_t handle_generation_{};
};

[[nodiscard]] inline std::expected<shared_buffer, errc>
make_shared_buffer(resource_ref resource,
                   std::size_t capacity,
                   std::size_t alignment,
                   memory_tag tag = default_memory_tag,
                   shared_buffer_options options = {}) noexcept {
    auto validated_alignment = align_up(0U, alignment);
    if (!validated_alignment) {
        return std::unexpected(validated_alignment.error());
    }
    if (options.max_refcount == 0U) {
        return std::unexpected(errc::size_overflow);
    }
    const auto traits = resource.traits();
    if (options.allow_cross_thread_final_release &&
        traits.thread_safety != resource_thread_safety::thread_safe &&
        !traits.supports_remote_deallocate) {
        return std::unexpected(errc::wrong_owner);
    }

    const auto payload_offset = align_up(sizeof(detail::shared_buffer_control), alignment);
    if (!payload_offset) {
        return std::unexpected(payload_offset.error());
    }
    const auto total_size = checked_add(*payload_offset, capacity);
    if (!total_size) {
        return std::unexpected(total_size.error());
    }
    const auto block_alignment = std::max(alignment, alignof(detail::shared_buffer_control));
    auto allocated = resource.allocate(*total_size, block_alignment, tag);
    if (!allocated) {
        return std::unexpected(allocated.error());
    }
    auto* control = ::new (allocated->data) detail::shared_buffer_control{};
    control->capacity = capacity;
    control->payload_offset = *payload_offset;
    control->payload_alignment = alignment;
    control->owner_generation = options.owner_generation;
    control->max_refcount = options.max_refcount;
    control->final_release_uses_remote =
        traits.thread_safety != resource_thread_safety::thread_safe &&
        traits.supports_remote_deallocate;
    control->resource = resource;
    control->block = *allocated;
    return shared_buffer{control, options.owner_generation};
}

} // namespace voris::mem
