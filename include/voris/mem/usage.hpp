#pragma once

#include <concepts>
#include <cstddef>
#include <memory>

namespace voris::mem {

struct usage_snapshot {
    std::size_t active_bytes{};
    std::size_t reserved_bytes{};
    std::size_t active_allocations{};
    std::size_t total_allocations{};
    std::size_t total_deallocations{};
    std::size_t failed_allocations{};
};

template <typename Counter>
concept usage_counter = requires(const Counter& counter) {
    { counter.snapshot() } noexcept -> std::same_as<usage_snapshot>;
};

class usage_counter_view {
public:
    using snapshot_fn = usage_snapshot (*)(const void*) noexcept;

    constexpr usage_counter_view() noexcept = default;

    template <usage_counter Counter>
    explicit usage_counter_view(const Counter& counter) noexcept
        : state_(std::addressof(counter)),
          snapshot_([](const void* state) noexcept -> usage_snapshot {
              return static_cast<const Counter*>(state)->snapshot();
          }) {}

    [[nodiscard]] usage_snapshot snapshot() const noexcept {
        if (snapshot_ == nullptr) {
            return usage_snapshot{};
        }
        return snapshot_(state_);
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return snapshot_ != nullptr;
    }

private:
    const void* state_{};
    snapshot_fn snapshot_{};
};

} // namespace voris::mem
